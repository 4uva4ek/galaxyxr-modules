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
struct Profile {
    const char *name, *package, *sha;
    uintptr_t function, length, cave, end, enabled;
    std::vector<uintptr_t> sites;
    std::vector<uint32_t> original, patched;
    std::vector<unsigned char> payload;
};
#include "profiles.generated.h"
#include "compatibility.h"
static void require(bool ok,const char* msg) { if(!ok) throw std::runtime_error(msg); }
static std::string proc(pid_t pid,const char* f) { return "/proc/"+std::to_string(pid)+"/"+f; }
static std::string read(const std::string& path) {
    std::ifstream f(path); require(bool(f),"Cannot read file");
    return {std::istreambuf_iterator<char>(f),{}};
}
static std::string starttime(pid_t pid) {
    auto text=read(proc(pid,"stat"));auto end=text.rfind(')');require(end!=std::string::npos,"Bad process stat");
    std::istringstream fields(text.substr(end+2));std::string value;
    for(int i=0;i<20;++i)require(bool(fields>>value),"Short process stat");
    return value;
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
    require(result.base,"Private OpenXR runtime RX mapping not found");return result;
}
struct Memory {
    int fd;pid_t pid;
    explicit Memory(pid_t p):fd(open(proc(p,"mem").c_str(),O_RDONLY|O_CLOEXEC)),pid(p) {require(fd>=0,"Cannot read memory");}
    ~Memory(){close(fd);}
    void get(uintptr_t a,void* b,size_t n) {require(pread(fd,b,n,a)==static_cast<ssize_t>(n),"Memory read failed");}
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
            DIR* d=opendir(proc(pid,"task").c_str());require(d,"Cannot list threads");std::vector<pid_t> fresh;
            while(auto* e=readdir(d)) {pid_t t=atoi(e->d_name);if(t<=0)continue;
                if(std::none_of(threads.begin(),threads.end(),[t](const Thread& x){return x.id==t;}))fresh.push_back(t);}
            closedir(d);if(fresh.empty())return;
            for(pid_t t:fresh) {
                if(ptrace(PTRACE_SEIZE,t,nullptr,nullptr)==-1){if(errno==ESRCH)continue;throw std::runtime_error("PTRACE_SEIZE denied");}
                threads.push_back({t,false,0});require(ptrace(PTRACE_INTERRUPT,t,nullptr,nullptr)==0,"Interrupt failed");
                int s=0;for(;;){pid_t got=waitpid(t,&s,__WALL|WNOHANG);require(got>=0,"Wait failed");if(got==t)break;
                    require(std::chrono::steady_clock::now()<deadline,"Thread stop timeout");usleep(1000);}
                require(WIFSTOPPED(s),"Thread exited while attaching");auto& item=threads.back();item.stopped=true;
                if((s>>16)==0)item.signal=WSTOPSIG(s);
                require((s>>16)==PTRACE_EVENT_STOP&&WSTOPSIG(s)==SIGTRAP,"Unexpected thread stop");
            }
        }
        throw std::runtime_error("Thread list did not stabilize");
    }
};
struct State {bool hooked,resident;uint32_t enabled;};
// A four-instruction transaction must not split an in-flight consumePose.
// Conservatively inspect live stack words as well as PC/LR (including PAC).
// This may refuse a safe operation, but never treats an unreadable stack as idle.
static void requireQuiescent(pid_t pid,Memory& mem,const Frozen& frozen,const Map& m,const Profile& p) {
    auto inside=[&](uint64_t value){value&=0x0000ffffffffffffULL;
        return (value>=m.base+p.function&&value<m.base+p.function+p.length)||
               (value>=m.base+p.cave&&value<m.base+p.end);};
    const auto maps=read(proc(pid,"maps"));
    for(const auto& t:frozen.threads) {
        user_pt_regs regs{};iovec io{&regs,sizeof(regs)};
        require(ptrace(PTRACE_GETREGSET,t.id,reinterpret_cast<void*>(NT_PRSTATUS),&io)==0&&io.iov_len==sizeof(regs),"Cannot verify stopped thread registers; no change");
        require(!inside(regs.pc)&&!inside(regs.regs[30]),"consumePose is in flight; retry after tracking is idle");
        std::istringstream lines(maps);std::string line;uintptr_t end=0;
        while(std::getline(lines,line)){unsigned long lo,hi;char perm[5]{};
            if(sscanf(line.c_str(),"%lx-%lx %4s",&lo,&hi,perm)==3&&perm[0]=='r'&&lo<=regs.sp&&regs.sp<hi){end=hi;break;}}
        require(end&&end-regs.sp<=8*1024*1024,"Cannot bound live stack; no change");
        auto stack=mem.bytes(regs.sp,end-regs.sp);
        for(size_t i=0;i+8<=stack.size();i+=8){uint64_t word;memcpy(&word,stack.data()+i,8);
            require(!inside(word),"consumePose return address is live; retry after tracking is idle");}
    }
}
struct App {std::string package;unsigned long uid;};
static std::vector<App> thirdPartyApps(){
    int fds[2];require(pipe2(fds,O_CLOEXEC)==0,"Package pipe failed");pid_t child=fork();
    require(child>=0,"Package fork failed");
    if(child==0){
        // Binder shellCommand forwards all three descriptors to system_server.
        // Root-only watcher files are not valid stdin/stderr for that service.
        int nullfd=open("/dev/null",O_RDWR);if(nullfd<0)_exit(126);
        dup2(nullfd,STDIN_FILENO);dup2(nullfd,STDERR_FILENO);close(nullfd);
        dup2(fds[1],STDOUT_FILENO);close(fds[0]);close(fds[1]);
        execl("/system/bin/cmd","cmd","package","list","packages","-3","-U",static_cast<char*>(nullptr));_exit(127);}
    close(fds[1]);std::string output;char b[4096];ssize_t n;
    while((n=::read(fds[0],b,sizeof(b)))>0)output.append(b,n);
    close(fds[0]);int status=0;require(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"Cannot verify third-party packages");
    std::vector<App> apps;std::istringstream lines(output);std::string line;
    while(std::getline(lines,line)){auto split=line.find(" uid:");if(line.rfind("package:",0)||split==std::string::npos)continue;
        auto name=line.substr(8,split-8);unsigned long uid=strtoul(line.c_str()+split+5,nullptr,10);
        if(uid>=10000&&!name.empty())apps.push_back({name,uid});}
    require(!apps.empty(),"No third-party packages found");return apps;
}
static bool thirdParty(pid_t pid,const std::string& name,const std::vector<App>& apps){
    auto status=read(proc(pid,"status"));auto pos=status.find("Uid:");if(pos==std::string::npos)return false;
    unsigned long uid=strtoul(status.c_str()+pos+4,nullptr,10);
    return std::any_of(apps.begin(),apps.end(),[&](const App& a){return a.package==name&&a.uid==uid;});
}
static void noLegacyHook(pid_t pid,Memory& mem) {
    // Conflict detection only: unknown APK versions do not select runtime offsets.
    std::istringstream lines(read(proc(pid,"maps")));std::string line;
    while(std::getline(lines,line)){
        unsigned long lo,hi,off;char perm[5]{};int used=0;
        if(sscanf(line.c_str(),"%lx-%lx %4s %lx %*s %*s %n",&lo,&hi,perm,&off,&used)!=4)continue;
        std::string path=line.substr(used);
        if(strcmp(perm,"r-xp")||off||path.find("/lib/arm64/libvrlink_scene.so")==std::string::npos)continue;
        auto digest=hash(path);uintptr_t begin=0,size=0;
        if(digest=="a1f1907ee91dc30dcdb4e3924cf3cff6e78302131fa835d9eb40f3fd4946ebde"){begin=0x1070cc;size=0xa78;}
        if(digest=="377547082a561b7c1f9eb3cfe9314f94b2e57602f3dea917fa8a27526d626f5b"){begin=0x107d30;size=0xa9c;}
        if(size){auto disk=read(path);require(disk.size()>=begin+size&&hi-lo>=begin+size,"Legacy mapping truncated");
            auto bytes=mem.bytes(lo+begin,size);require(!memcmp(bytes.data(),disk.data()+begin,size),"Old app correction still active; restore it first");}
    }
}
static State validate(Memory& mem,const Map& m,const Profile& p,const std::string& disk) {
    require(m.base+p.end<=m.end,"RX mapping too short");
    auto function=mem.bytes(m.base+p.function,p.length);int patched=0;
    require(gxr_consumer_state(function.data())>=0,"Unknown consumePose change or partial correction");
    for(size_t i=0;i<p.sites.size();++i) {
        uint32_t word;memcpy(&word,function.data()+p.sites[i]-p.function,4);
        require(word==p.original[i]||word==p.patched[i],"Unknown callsite");patched+=word==p.patched[i];
        memcpy(function.data()+p.sites[i]-p.function,&p.original[i],4);
    }
    require(!patched||patched==static_cast<int>(p.sites.size()),"Partial hook; restart Steam Link");
    uint32_t gate;memcpy(&gate,function.data()+0x1b3860-p.function,4);
    require(gate==0x3400038a||gate==0xd503201f,"Unknown extrapolation gate");
    const uint32_t stockGate=0x3400038a;
    memcpy(function.data()+0x1b3860-p.function,&stockGate,4);
    uint32_t oldCall;mem.get(m.base+0x1d9e18,&oldCall,4);
    require(oldCall==0x94025406,"Legacy velocity hook present; restart app before updating");
    require(!memcmp(function.data(),disk.data()+p.function,p.length),"Live function differs from pinned file");
    auto cave=mem.bytes(m.base+p.cave,p.end-p.cave);
    bool zero=std::all_of(cave.begin(),cave.end(),[](unsigned char c){return c==0;});uint32_t enabled=0;
    if(!zero) {
        memcpy(&enabled,cave.data()+p.enabled,4);require(enabled<=1,"Unknown activation word");
        memset(cave.data()+p.enabled,0,4);
        require(!memcmp(cave.data(),p.payload.data(),p.payload.size())&&
                std::all_of(cave.begin()+p.payload.size(),cave.end(),[](unsigned char c){return c==0;}),"Unknown RX tail contents");
    }
    require(!patched||(!zero&&enabled==1),"Hooked code without active payload; restart Steam Link");
    require(patched||!enabled,"Active payload without hooks; restart Steam Link");
    return {bool(patched),!zero,enabled};
}
int main(int argc,char**argv) {
    try {
        if(argc==2&&std::string(argv[1])=="list"){
            const auto apps=thirdPartyApps();DIR* d=opendir("/proc");require(d,"Cannot list processes");
            while(auto* e=readdir(d)){pid_t pid=atoi(e->d_name);if(pid<=1)continue;
                try{std::string name=read(proc(pid,"cmdline")).c_str();
                    if(!thirdParty(pid,name,apps))continue;
                    const auto maps=read(proc(pid,"maps"));
                    if(maps.find("/system/lib64/libopenxr_android.so")!=std::string::npos)
                        printf("%d %s %s\n",pid,starttime(pid).c_str(),name.c_str());
                }catch(const std::exception&){} }
            closedir(d);return 0;
        }
        require(argc==3,"Usage: gxr-openxr-velocity PID status|on|off");char* end=nullptr;long number=strtol(argv[1],&end,10);
        require(end&&!*end&&number>1&&number<INT32_MAX,"Invalid PID");pid_t pid=number;
        std::string mode=argv[2];require(mode=="status"||mode=="on"||mode=="off","Invalid mode");
        std::string package=read(proc(pid,"cmdline")).c_str();
        require(thirdParty(pid,package,thirdPartyApps()),"Target is not a verified third-party main app process");
        if(mode=="on"&&package=="com.valvesoftware.steamlinkvr"){
            require((access("/data/adb/modules/gxr_controller_velocity_frame",F_OK)!=0||
                    access("/data/adb/modules/gxr_controller_velocity_frame/disable",F_OK)==0)&&
                    access("/dev/gxr_controller_velocity_frame/worker",F_OK)!=0,"Disable old module and wait for worker exit first");
        }
        if(mode=="on"){
            require(hash("/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so")==
                    "80d45131558b28107cf7e3c9a2c483bce4e2207b934e4f25e951f0fc79d1cd36","Controller producer firmware changed");
        }
        const auto started=starttime(pid);
        auto m=mapping(pid);std::string digest=hash(m.file);const Profile* selected=nullptr;
        for(const auto& p:PROFILES)if(digest==p.sha){require(!selected,"Ambiguous profile");selected=&p;}
        require(selected,"Unsupported library hash");const auto& p=*selected;auto disk=read(m.file);
        require(disk.size()>=p.end,"Pinned file truncated");Memory mem(pid);auto before=validate(mem,m,p,disk);
        if(mode=="status"){printf("%s pid=%d profile=%s\n",before.hooked?"CORRECTED":"STOCK",pid,p.name);return 0;}
        signal(SIGINT,SIG_IGN);signal(SIGTERM,SIG_IGN);signal(SIGHUP,SIG_IGN);
        if(mode=="on"){noLegacyHook(pid,mem);}
        Frozen frozen;frozen.freeze(pid);auto again=mapping(pid);
        require(again.base==m.base&&again.file==m.file&&again.end==m.end,"Mapping changed");
        require(read(proc(pid,"cmdline")).c_str()==package&&starttime(pid)==started,"Process identity changed");before=validate(mem,m,p,disk);
        bool on=mode=="on";
        if(on)noLegacyHook(pid,mem);
        if(on!=before.hooked) {
            requireQuiescent(pid,mem,frozen,m,p);
            // Payload remains after off: stopped threads may hold a return address into it.
            // Rollback restores reachability and activation, never erases reachable code.
            bool complete=before.resident;
            try {
                if(on&&!complete){mem.put(m.base+p.cave,p.payload.data(),p.payload.size());
                    require(mem.bytes(m.base+p.cave,p.payload.size())==p.payload,"Payload mismatch");complete=true;}
                uint32_t disabled=0;mem.put(m.base+p.cave+p.enabled,&disabled,4);
                for(size_t i=0;i<p.sites.size();++i){uint32_t w=on?p.patched[i]:p.original[i];mem.put(m.base+p.sites[i],&w,4);}
                uint32_t enabled=on;mem.put(m.base+p.cave+p.enabled,&enabled,4);
                require(validate(mem,m,p,disk).hooked==on,"Final verification failed");
            } catch(...) {
                try {
                    if(complete){uint32_t zero=0;mem.put(m.base+p.cave+p.enabled,&zero,4);}
                    for(size_t i=0;i<p.sites.size();++i){uint32_t w=before.hooked?p.patched[i]:p.original[i];mem.put(m.base+p.sites[i],&w,4);}
                    if(complete)mem.put(m.base+p.cave+p.enabled,&before.enabled,4);
                }catch(...){fprintf(stderr,"ROLLBACK FAILED: close/restart Steam Link; no on-disk app or system files changed\n");}
                throw;
            }
        }
        printf("%s pid=%d profile=%s threads=%zu verified private memory only\n",on?"CORRECTED":"STOCK",pid,p.name,frozen.threads.size());
        return 0;
    }catch(const std::exception& e){fprintf(stderr,"ERROR: %s (%s)\n",e.what(),strerror(errno));return 1;}
}
