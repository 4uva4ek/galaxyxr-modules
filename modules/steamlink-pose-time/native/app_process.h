// App-only ptrace patch. Never opens a device file for writing or touches system services.
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <elf.h>
#include <asm/ptrace.h>
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
#include <algorithm>
#include "../../openxr-velocity-frame/native/package_query.h"
#include "../../openxr-velocity-frame/native/process_identity.h"
static void require(bool ok,const char* msg) { if(!ok) throw std::runtime_error(msg); }
static void requireProcess(bool ok,const char* msg) { if(!ok) throw ProcessAccessError(msg); }
static std::string proc(pid_t pid,const char* f) { return "/proc/"+std::to_string(pid)+"/"+f; }
static std::string read(const std::string& path) {
    std::ifstream f(path);
    if(!f&&path.rfind("/proc/",0)==0)throw ProcessAccessError("Cannot read process file");
    require(bool(f),"Cannot read file");
    // Bulk reads also support procfs files whose reported size is zero.
    // Byte-at-a-time iterators caused avoidable allocation/copy CPU bursts.
    std::string result;char buffer[16384];
    while(f.read(buffer,sizeof(buffer))||f.gcount())result.append(buffer,static_cast<size_t>(f.gcount()));
    require(!f.bad(),"File read failed");return result;
}
static std::string starttime(pid_t pid) {
    auto identity=readProcessIdentity(proc(pid,"stat"),{});
    if(identity.state==ProcessState::Gone)throw ProcessGone("Target exited");
    return identity.started;
}
static void verifyIdentity(pid_t pid,const std::string& expected) {
    if(readProcessIdentity(proc(pid,"stat"),expected).state==ProcessState::Gone)
        throw ProcessGone("Target exited or PID was reused");
}
static std::string hash(const std::string& path) {
    int p[2]; require(pipe2(p,O_CLOEXEC)==0,"pipe failed");
    pid_t child=fork();
    if(child<0) { close(p[0]);close(p[1]);throw std::runtime_error("fork failed"); }
    if(child==0) {
        dup2(p[1],STDOUT_FILENO);close(p[0]);close(p[1]);
        execl("/system/bin/sha256sum","sha256sum",path.c_str(),static_cast<char*>(nullptr));_exit(127);
    }
    close(p[1]);std::string out;char buf[512];ssize_t n;
    while((n=::read(p[0],buf,sizeof(buf)))>0) out.append(buf,n);
    close(p[0]);int status=0;require(waitpid(child,&status,0)==child,"hash wait failed");
    require(WIFEXITED(status)&&WEXITSTATUS(status)==0&&out.size()>=64,"sha256sum failed");
    return out.substr(0,64);
}
struct Map { uintptr_t base=0,end=0;std::string file; };
static Map mapping(pid_t pid) {
    const std::string maps=read(proc(pid,"maps"));
    require(maps.find("libgxr_controller_velocity_frame.so")==std::string::npos&&
            maps.find("libgxr_controller_hal_pose.so")==std::string::npos,"Old correction layer mapped; use layer-free APK");
    std::istringstream lines(maps);std::string line;Map result;
    while(std::getline(lines,line)) {
        unsigned long lo,hi,off;char perm[5]{};int used=0;
        if(sscanf(line.c_str(),"%lx-%lx %4s %lx %*s %*s %n",&lo,&hi,perm,&off,&used)!=4) continue;
        std::string path=line.substr(used);
        if(path!="/system/lib64/libopenxr_android.so")continue;
        if(strcmp(perm,"r-xp")||off!=0xfd000) continue;
        require(!result.base,"Ambiguous runtime mapping");
        result={lo-off,hi,path};
    }
    requireProcess(result.base,"Private OpenXR runtime RX mapping not found");return result;
}
struct Memory {
    int fd;pid_t pid;
    explicit Memory(pid_t p):fd(open(proc(p,"mem").c_str(),O_RDONLY|O_CLOEXEC)),pid(p) {requireProcess(fd>=0,"Cannot read memory");}
    ~Memory(){close(fd);}
    void get(uintptr_t a,void* b,size_t n) {requireProcess(pread(fd,b,n,a)==static_cast<ssize_t>(n),"Memory read failed");}
    std::vector<unsigned char> bytes(uintptr_t a,size_t n) {std::vector<unsigned char>b(n);get(a,b.data(),n);return b;}
    void put(uintptr_t a,const void* b,size_t n) {
        const auto* src=static_cast<const unsigned char*>(b);
        while(n) {
            uintptr_t aligned=a&~uintptr_t(7);size_t offset=a-aligned,take=std::min(n,8-offset);
            uint64_t word;get(aligned,&word,8);memcpy(reinterpret_cast<char*>(&word)+offset,src,take);
            require(ptrace(PTRACE_POKETEXT,pid,reinterpret_cast<void*>(aligned),reinterpret_cast<void*>(word))==0,"Memory write denied");
            uint64_t check;get(aligned,&check,8);require(check==word,"Memory write readback failed");
            a+=take;src+=take;n-=take;
        }
    }
};
// All threads stopped before any mutation. No EXITKILL or remote function calls.
struct Frozen {
    struct Thread {pid_t id;bool stopped;int signal;};std::vector<Thread> threads;
    ~Frozen(){for(auto i=threads.rbegin();i!=threads.rend();++i) if(i->stopped)
        if(ptrace(PTRACE_DETACH,i->id,nullptr,reinterpret_cast<void*>(static_cast<intptr_t>(i->signal)))==-1&&errno!=ESRCH)
            fprintf(stderr,"Detach failed; tracer exit releases remaining threads\n");}
    void freeze(pid_t pid) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
        for(int round=0;round<8;++round) {
            DIR* d=opendir(proc(pid,"task").c_str());requireProcess(d,"Cannot list threads");std::vector<pid_t> fresh;
            while(auto* e=readdir(d)) {pid_t t=atoi(e->d_name);if(t<=0)continue;
                if(std::none_of(threads.begin(),threads.end(),[t](const Thread& x){return x.id==t;}))fresh.push_back(t);}
            closedir(d);if(fresh.empty())return;
            for(pid_t t:fresh) {
                if(ptrace(PTRACE_SEIZE,t,nullptr,nullptr)==-1){if(errno==ESRCH)continue;throw ProcessAccessError("PTRACE_SEIZE denied");}
                threads.push_back({t,false,0});requireProcess(ptrace(PTRACE_INTERRUPT,t,nullptr,nullptr)==0,"Interrupt failed");
                int s=0;for(;;){pid_t got=waitpid(t,&s,__WALL|WNOHANG);requireProcess(got>=0,"Wait failed");if(got==t)break;
                    require(std::chrono::steady_clock::now()<deadline,"Thread stop timeout");usleep(1000);}
                requireProcess(WIFSTOPPED(s),"Thread exited while attaching");auto& item=threads.back();item.stopped=true;
                if((s>>16)==0)item.signal=WSTOPSIG(s);
                require((s>>16)==PTRACE_EVENT_STOP&&WSTOPSIG(s)==SIGTRAP,"Unexpected thread stop");
            }
        }
        throw std::runtime_error("Thread list did not stabilize");
    }
};
struct App {std::string package;unsigned long uid;};
static std::vector<App> thirdPartyApps(){
    const auto output=queryPackages();
    std::vector<App> apps;std::istringstream lines(output);std::string line;
    while(std::getline(lines,line)){auto split=line.find(" uid:");if(line.rfind("package:",0)||split==std::string::npos)continue;
        auto name=line.substr(8,split-8);unsigned long uid=strtoul(line.c_str()+split+5,nullptr,10);
        if(uid>=10000&&!name.empty())apps.push_back({name,uid});}
    require(output.empty()||!apps.empty(),"Unrecognized third-party package response");return apps;
}
static bool thirdParty(pid_t pid,const std::string& name,const std::vector<App>& apps){
    // Reject unrelated processes before opening their status files. Keep the
    // UID check for every eligible name; a name alone never authorizes attach.
    if(std::none_of(apps.begin(),apps.end(),[&](const App& a){return a.package==name;}))return false;
    auto status=read(proc(pid,"status"));auto pos=status.find("Uid:");if(pos==std::string::npos)return false;
    unsigned long uid=strtoul(status.c_str()+pos+4,nullptr,10);
    return std::any_of(apps.begin(),apps.end(),[&](const App& a){return a.package==name&&a.uid==uid;});
}
