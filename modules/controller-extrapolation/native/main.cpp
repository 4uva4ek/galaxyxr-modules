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
#include "../../openxr-velocity-frame/native/compatibility.h"

static constexpr char PACKAGE[] = "com.valvesoftware.steamlinkvr";
static constexpr char LIBRARY[] = "/system/lib64/libopenxr_android.so";
static constexpr uintptr_t FUNCTION = 0x1b359c, SITE = 0x1b3860;
static constexpr uint32_t STOCK = 0x3400038a, EXTRAPOLATION = 0xd503201f;
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
        require(off == 0xfd000 && hi - lo >= FUNCTION - off + sizeof(EXPECTED), "Unexpected mapping layout");
        found = lo - off;
    }
    require(found, "Pinned OpenXR runtime not mapped"); return found;
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
    const int state=gxr_consumer_state(bytes.data());
    require(state>=0,"Unknown consumePose change or partial velocity correction");
    if(state&1) {
        std::vector<unsigned char> cave(0x520);mem.get(base+0x272ae0,cave.data(),cave.size());
        require(gxr_payload_valid(cave.data(),true),"Velocity correction payload is not the supported preprediction version");
    }
    return state&2?EXTRAPOLATION:STOCK;
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
            for(pid_t tid:fresh) {
                if(ptrace(PTRACE_SEIZE,tid,nullptr,nullptr)==-1) { if(errno==ESRCH) continue; throw std::runtime_error("PTRACE_SEIZE denied"); }
                threads.push_back({tid,false,0});
                require(ptrace(PTRACE_INTERRUPT,tid,nullptr,nullptr)==0, "PTRACE_INTERRUPT failed");
                int status=0;
                for (;;) {
                    pid_t got=waitpid(tid,&status,__WALL|WNOHANG);
                    require(got>=0,"waitpid failed");
                    if(got==tid) break;
                    require(std::chrono::steady_clock::now()<deadline,"Thread stop timed out"); usleep(1000);
                }
                require(WIFSTOPPED(status),"Thread exited while attaching");
                auto& t=threads.back(); t.stopped=true;
                if ((status>>16)==0) t.signal=WSTOPSIG(status);
                require((status>>16)==PTRACE_EVENT_STOP && WSTOPSIG(status)==SIGTRAP,
                        "Unexpected stop; no patch applied");
            }
        }
        throw std::runtime_error("Thread list did not stabilize");
    }
};
int main(int argc, char** argv) {
    try {
        require(argc==3,"Usage: gxr-extrapolation PID status|on|off|toggle");
        char* end=nullptr; long number=strtol(argv[1],&end,10);
        require(end && !*end && number>1 && number<INT32_MAX,"Invalid PID"); pid_t pid=number;
        std::string mode=argv[2]; require(mode=="status"||mode=="on"||mode=="off"||mode=="toggle","Invalid mode");
        require(read(proc(pid,"cmdline")).c_str()==std::string(PACKAGE),"Wrong target process");
        auto disk=read(LIBRARY);
        require(disk.size()==2862448 && !memcmp(disk.data()+FUNCTION,EXPECTED,sizeof(EXPECTED)),"Disk runtime function mismatch");
        uintptr_t base=mapping(pid); Memory mem(pid); uint32_t before=validate(mem,base);
        if(mode=="status") { printf("%s pid=%d site=%lx\n",before==EXTRAPOLATION?"EXTRAPOLATION":"STOCK",pid,base+SITE); return 0; }
        // Short critical section: ignore terminal termination, never use EXITKILL.
        signal(SIGINT,SIG_IGN); signal(SIGTERM,SIG_IGN); signal(SIGHUP,SIG_IGN);
        Frozen frozen; frozen.freeze(pid);
        require(mapping(pid)==base,"Mapping changed during attach"); before=validate(mem,base);
        uint32_t after=mode=="toggle"?(before==EXTRAPOLATION?STOCK:EXTRAPOLATION):(mode=="on"?EXTRAPOLATION:STOCK);
        if(before!=after) {
            uintptr_t aligned=(base+SITE)&~uintptr_t(7);
            uint64_t word; mem.get(aligned,&word,8); uint64_t modified=word;
            memcpy(reinterpret_cast<char*>(&modified)+(base+SITE-aligned),&after,4);
            require(ptrace(PTRACE_POKETEXT,pid,reinterpret_cast<void*>(aligned),reinterpret_cast<void*>(modified))==0,"PTRACE_POKETEXT denied");
            try { require(validate(mem,base)==after,"Patch readback failed"); }
            catch(...) {
                if(ptrace(PTRACE_POKETEXT,pid,reinterpret_cast<void*>(aligned),reinterpret_cast<void*>(word))!=0)
                    fprintf(stderr,"RESTORE FAILED: reboot required\n");
                throw;
            }
        }
        printf("%s pid=%d: verified one instruction in private memory; %zu threads released on exit\n",after==EXTRAPOLATION?"EXTRAPOLATION":"STOCK",pid,frozen.threads.size());
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"ERROR: %s (errno=%d %s)\n",e.what(),errno,strerror(errno)); return 1; }
}
