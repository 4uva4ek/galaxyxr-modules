// Only modifies a private executable mapping through ptrace. No remote calls,
// agents, file writes, SELinux changes, or startup integration.
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <chrono>
#include "expected.h"
#include "tuning.h"
#include <sys/uio.h>
#include <elf.h>
#include <asm/ptrace.h>
static constexpr uintptr_t TUNE = 0x12870;

static constexpr char PACKAGE[] = "com.sec.android.secxrcontrollerservice";
static constexpr char LIBRARY[] = "/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so";
static constexpr uintptr_t FUNCTION = 0x122ac, SITE = 0x1238c;
static constexpr uint32_t STOCK = 0x94000162, MAIN = 0x52800020;
static void require(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
static std::string proc(pid_t pid, const char* file) { return "/proc/" + std::to_string(pid) + "/" + file; }
static std::string read(const std::string& path) {
    std::ifstream f(path); require(bool(f), ("Cannot read " + path).c_str());
    return {std::istreambuf_iterator<char>(f), {}};
}
static uintptr_t mapping(pid_t pid) {
    std::istringstream in(read(proc(pid, "maps"))); std::string line;
    uintptr_t found = 0;
    while (std::getline(in, line)) {
        unsigned long lo, hi, off; char perms[5]{}; int used=0;
        if (sscanf(line.c_str(), "%lx-%lx %4s %lx %*s %*s %n", &lo, &hi, perms, &off, &used) != 4) continue;
        if (line.substr(used) != LIBRARY || strcmp(perms, "r-xp")) continue;
        require(!found, "Ambiguous executable mapping");
        require(off == 0x10000 && hi - lo >= 0x11210, "Unexpected mapping layout");
        found = lo - off;
    }
    require(found, "Pinned JNI library not mapped"); return found;
}
struct Memory {
    int fd;
    explicit Memory(pid_t pid): fd(open(proc(pid,"mem").c_str(), O_RDONLY | O_CLOEXEC)) { require(fd >= 0, "Cannot read process memory"); }
    ~Memory() { close(fd); }
    void get(uintptr_t address, void* buffer, size_t size) {
        require(pread(fd, buffer, size, address) == static_cast<ssize_t>(size), "Memory read failed");
    }
};
static uint32_t validate(Memory& mem, uintptr_t base) {
    std::vector<unsigned char> bytes(sizeof(EXPECTED)); mem.get(base + FUNCTION, bytes.data(), bytes.size());
    uint32_t instruction; memcpy(&instruction, bytes.data() + SITE - FUNCTION, 4);
    require(instruction == STOCK || instruction == MAIN, "Unknown instruction; refusing modification");
    bool tuned = !memcmp(bytes.data()+TUNE-FUNCTION,TUNING,sizeof(TUNING));
    if(tuned) {
        require(instruction==MAIN,"Partial tuning patch");
        memcpy(bytes.data()+TUNE-FUNCTION,EXPECTED+TUNE-FUNCTION,sizeof(TUNING));
    }
    memcpy(bytes.data() + SITE - FUNCTION, &STOCK, 4);
    require(!memcmp(bytes.data(), EXPECTED, bytes.size()), "Function differs from pinned stock code");
    return tuned?2:(instruction==MAIN?1:0);
}
struct Frozen {
    struct Thread { pid_t id; bool stopped; int signal; };
    std::vector<Thread> threads;
    ~Frozen() {
        for (auto it=threads.rbegin(); it!=threads.rend(); ++it) {
            if (it->stopped && ptrace(PTRACE_DETACH, it->id, nullptr, reinterpret_cast<void*>(static_cast<intptr_t>(it->signal))) == -1 && errno != ESRCH)
                fprintf(stderr, "Detach %d failed: %s; tracer exit releases remaining tracees\n", it->id, strerror(errno));
        }
    }
    void freeze(pid_t pid) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        for (int round=0; round<8; ++round) {
            DIR* dir=opendir(proc(pid,"task").c_str()); require(dir, "Cannot list threads");
            std::vector<pid_t> fresh;
            while (auto* entry=readdir(dir)) {
                pid_t tid=atoi(entry->d_name); if (tid<=0) continue;
                bool known=false; for (const auto& t:threads) if(t.id==tid) known=true;
                if(!known) fresh.push_back(tid);
            }
            closedir(dir); if(fresh.empty()) return;
            // Interrupt every new thread first, then collect the stops. Waiting
            // for each thread in turn kept the already stopped ones (33-77 in
            // the controller service) frozen for a polling sleep per thread.
            const size_t first=threads.size();
            for(pid_t tid:fresh) {
                if(ptrace(PTRACE_SEIZE,tid,nullptr,nullptr)==-1) { if(errno==ESRCH) continue; throw std::runtime_error("PTRACE_SEIZE denied"); }
                threads.push_back({tid,false,0});
                require(ptrace(PTRACE_INTERRUPT,tid,nullptr,nullptr)==0, "PTRACE_INTERRUPT failed");
            }
            for(size_t waiting=threads.size()-first; waiting;) {
                bool progressed=false;
                for(size_t i=first; i<threads.size(); ++i) {
                    auto& t=threads[i]; if(t.stopped) continue;
                    int status=0;
                    pid_t got=waitpid(t.id,&status,__WALL|WNOHANG);
                    require(got>=0,"waitpid failed");
                    if(got!=t.id) continue;
                    require(WIFSTOPPED(status),"Thread exited while attaching");
                    t.stopped=true; --waiting; progressed=true;
                    if ((status>>16)==0) t.signal=WSTOPSIG(status);
                    require((status>>16)==PTRACE_EVENT_STOP && WSTOPSIG(status)==SIGTRAP,
                            "Unexpected stop; no patch applied");
                }
                if(waiting && !progressed) {
                    require(std::chrono::steady_clock::now()<deadline,"Thread stop timed out"); usleep(100);
                }
            }
        }
        throw std::runtime_error("Thread list did not stabilize");
    }
};
#include "transaction.h"
int main(int argc, char** argv) {
    try {
        require(argc==3,"Usage: gxr-main PID status|on|off|toggle");
        char* end=nullptr; long number=strtol(argv[1],&end,10);
        require(end && !*end && number>1 && number<INT32_MAX,"Invalid PID"); pid_t pid=number;
        std::string mode=argv[2]; require(mode=="status"||mode=="on"||mode=="off"||mode=="toggle","Invalid mode");
        require(read(proc(pid,"cmdline")).c_str()==std::string(PACKAGE),"Wrong target process");
        auto disk=read(LIBRARY);
        require(disk.size()==176016 && !memcmp(disk.data()+FUNCTION,EXPECTED,sizeof(EXPECTED)),"Disk function mismatch");
        uintptr_t base=mapping(pid); Memory mem(pid); uint32_t before=validate(mem,base);
        if(mode=="status") { printf("%s pid=%d site=%lx profile=step2-amplitude-stock\n",before==2?"MAIN":(before==1?"LEGACY_MAIN":"STOCK"),pid,base+SITE); return 0; }
        // Already in the requested state: no write, so no reason to stop the service.
        if(mode!="toggle" && before==(mode=="on"?2u:0u)) {
            printf("%s pid=%d: profile=step2-amplitude-stock; private memory verified; already in this state, no threads stopped\n",
                   before?"MAIN":"STOCK",pid);
            return 0;
        }
        // Short critical section: ignore terminal termination, never use EXITKILL.
        signal(SIGINT,SIG_IGN); signal(SIGTERM,SIG_IGN); signal(SIGHUP,SIG_IGN);
        Frozen frozen; frozen.freeze(pid);
        require(mapping(pid)==base,"Mapping changed during attach"); before=validate(mem,base);
        uint32_t after=mode=="toggle"?(before?0:2):(mode=="on"?2:0);
        if(before!=after) change(pid,mem,base,frozen,after);
        printf("%s pid=%d: profile=step2-amplitude-stock; private memory verified; %zu threads released on exit\n",
               after?"MAIN":"STOCK",pid,frozen.threads.size());
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"ERROR: %s (errno=%d %s)\n",e.what(),errno,strerror(errno)); return 1; }
}
