// Explicitly authorized manual SYSTEM SERVICE memory experiment. No startup, file writes, or remote calls.
#include <sys/ptrace.h>
#include <sys/wait.h>
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
        if(path!="/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so")continue;
        if(strcmp(perm,"r-xp")||off!=0x10000) continue;
        require(!result.base,"Ambiguous runtime mapping");
        result={lo-off,hi,path};
    }
    require(result.base,"Private controller service RX mapping not found");return result;
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

#include "patch.h"
#include "validation.h"
static bool validate(Memory& mem,const Map& m,const std::string& disk){
 require(m.base+0x22000<=m.end,"RX tail absent");
 auto live=mem.bytes(m.base+0x10000,0x12000);
 int state=validate_rx(live.data(),reinterpret_cast<const unsigned char*>(disk.data())+0x10000);
 require(state>=0,"Unknown RX bytes outside the Haptic-owned function or invalid Prediction hook/payload");
 return state==1;
}
int main(int argc,char**argv){try{
 require(argc==3,"Usage: gxr-hal-lead30 PID status|on|off");char* end=nullptr;long n=strtol(argv[1],&end,10);
 require(end&&!*end&&n>1&&n<INT32_MAX,"PID");pid_t pid=n;std::string mode=argv[2];
 require(mode=="on"||mode=="off"||mode=="status","Mode");
 auto identity=read(proc(pid,"cmdline"));require(std::string(identity.c_str())=="com.sec.android.secxrcontrollerservice","Wrong process");
 auto stamp=starttime(pid);auto m=mapping(pid);
 require(hash(m.file)=="80d45131558b28107cf7e3c9a2c483bce4e2207b934e4f25e951f0fc79d1cd36","Wrong firmware");
 auto disk=read(m.file);require(disk.size()>=0x22000,"Short disk file");Memory mem(pid);bool before=validate(mem,m,disk);
 if(mode=="status"){printf("%s pid=%d start=%s private memory only\n",before?"HAL_PLUS_30MS":"STOCK",pid,stamp.c_str());return 0;}
 signal(SIGINT,SIG_IGN);signal(SIGTERM,SIG_IGN);signal(SIGHUP,SIG_IGN);
 Frozen frozen;frozen.freeze(pid);
 require(starttime(pid)==stamp&&read(proc(pid,"cmdline"))==identity,"Identity changed");
 auto again=mapping(pid);require(again.base==m.base&&again.end==m.end,"Mapping changed");
 before=validate(mem,m,disk);bool on=mode=="on";
 if(on!=before){
  try{
   if(on)mem.put(m.base+CAVE,PAYLOAD,sizeof(PAYLOAD));
   uint32_t word=on?PATCHED:ORIGINAL;mem.put(m.base+SITE,&word,4);
   require(validate(mem,m,disk)==on,"Verification failed");
  }catch(...){
   uint32_t word=before?PATCHED:ORIGINAL;
   try{mem.put(m.base+SITE,&word,4);}catch(...){fprintf(stderr,"Restore failed: full headset reboot required\n");}
   throw;
  }
 }
 printf("%s pid=%d start=%s threads=%zu verified; no persistent activation\n",on?"HAL_PLUS_30MS":"STOCK",pid,stamp.c_str(),frozen.threads.size());return 0;
}catch(const std::exception&e){fprintf(stderr,"ERROR %s (%s)\n",e.what(),strerror(errno));return 1;}}
