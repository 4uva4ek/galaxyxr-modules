// Author: 4uva4ek. Based on CLD's 2026-10-17 measurements.
// Data-only HAL log gate and SCHED_OTHER Steam Link thread nice values.
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
static const char* HAL="/vendor/bin/hw/vendor.samsung.hardware.secxrcontroller@1.0-service";
static const char* LIB="/vendor/lib64/hw/xrcontroller.kxr.default.so";
static const char* HASH="2c62026ec25be4dc20ef4dd0d20a31efcb09e30d967a68ebcfad03b5a8296743";
static const char* APP="com.valvesoftware.steamlinkvr";
static const std::string STATE="/dev/gxr_stream_cpu_relief";
static std::string mod;
static bool safe_mod=false;
static volatile sig_atomic_t stopping=0;
static void stop(int){stopping=1;}
static void need(bool b,const std::string& s){if(!b)throw std::runtime_error(s);}
static bool exists(const std::string& p){return access(p.c_str(),F_OK)==0;}
static std::string readfile(const std::string& p){std::ifstream f(p);need(bool(f),"Cannot read "+p);return {std::istreambuf_iterator<char>(f),{}};}
static void save(const std::string& p,const std::string& s){
    std::string tmp=p+".tmp"; int fd=open(tmp.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    need(fd>=0,"Cannot create state file");
    bool ok=write(fd,s.data(),s.size())==ssize_t(s.size())&&fsync(fd)==0;
    close(fd);need(ok,"State write failed");need(rename(tmp.c_str(),p.c_str())==0,"State rename failed");
}
static std::string proc(int p,const std::string& leaf){return "/proc/"+std::to_string(p)+"/"+leaf;}
struct Identity{int pid=0;unsigned long long start=0;};
static unsigned long long starttime(int pid){
    auto s=readfile(proc(pid,"stat"));auto end=s.rfind(')');need(end!=s.npos,"Bad stat");
    std::istringstream in(s.substr(end+2));std::string x;for(int i=0;i<20;i++)need(bool(in>>x),"Short stat");return std::stoull(x);
}
static bool alive(Identity x){try{return x.pid>1&&starttime(x.pid)==x.start;}catch(...){return false;}}
static std::vector<int> ids(const std::string& p){
    DIR* d=opendir(p.c_str());need(d,"Cannot enumerate "+p);std::vector<int> out;
    while(auto* e=readdir(d)){char* end=nullptr;long n=strtol(e->d_name,&end,10);if(!*end&&n>1&&n<2147483647)out.push_back(int(n));}closedir(d);return out;
}
static std::string cmd(int pid){auto s=readfile(proc(pid,"cmdline"));return s.substr(0,s.find('\0'));}
static Identity find(const char* name){Identity found;for(int p:ids("/proc")){try{if(cmd(p)!=name)continue;}catch(...){continue;}need(!found.pid,"Ambiguous target");found={p,starttime(p)};}return found;}
static std::string hash(const char* p){
    int fds[2];need(pipe2(fds,O_CLOEXEC)==0,"hash pipe");pid_t c=fork();need(c>=0,"hash fork");
    if(!c){dup2(fds[1],1);close(fds[0]);close(fds[1]);execl("/system/bin/sha256sum","sha256sum",p,(char*)nullptr);_exit(127);}
    close(fds[1]);std::string s;char b[256];ssize_t n;while((n=read(fds[0],b,sizeof b))>0)s.append(b,n);close(fds[0]);int status=0;
    while(waitpid(c,&status,0)<0){need(errno==EINTR,"hash wait");}
    need(WIFEXITED(status)&&WEXITSTATUS(status)==0&&s.size()>=64,"hash failed");return s.substr(0,64);
}
struct Memory{
    int fd=-1;uint64_t base=0;Identity target;
    explicit Memory(Identity t):target(t){
        need(alive(t)&&cmd(t.pid)==HAL,"HAL identity changed");
        struct stat file{};need(stat(LIB,&file)==0,"HAL library unavailable");
        std::string maps=readfile(proc(t.pid,"maps"));std::istringstream lines(maps);std::string line;
        while(std::getline(lines,line)){
            unsigned long long lo,hi,off,ino;char perm[5]{};int used=0;
            if(sscanf(line.c_str(),"%llx-%llx %4s %llx %*s %llu %n",&lo,&hi,perm,&off,&ino,&used)!=5)continue;
            if(line.substr(used)!=LIB||strcmp(perm,"r-xp")||off)continue;
            need(!base&&ino==file.st_ino,"HAL mapping identity mismatch");base=lo;
        }
        need(base,"HAL RX mapping missing");
        lines.clear();lines.str(maps);bool data=false;
        while(std::getline(lines,line)){
            unsigned long long lo,hi,off,ino;char perm[5]{};int used=0;
            if(sscanf(line.c_str(),"%llx-%llx %4s %llx %*s %llu %n",&lo,&hi,perm,&off,&ino,&used)!=5)continue;
            if(line.substr(used)==LIB&&!strcmp(perm,"rw-p")&&ino==file.st_ino&&base+0x22bf18>=lo&&base+0x22bf28<=hi&&off+base+0x22bf18-lo==0x229f18)data=true;
        }
        need(data,"HAL private data mapping mismatch");
        fd=open(proc(t.pid,"mem").c_str(),O_RDWR|O_CLOEXEC);need(fd>=0,"Cannot open HAL memory");
        if(!alive(t)){close(fd);fd=-1;throw std::runtime_error("HAL changed while opening memory");}
    }
    ~Memory(){if(fd>=0)close(fd);}
    uint32_t gate(){uint64_t bytes[2]{};need(pread(fd,bytes,16,base+0x22bf18)==16,"HAL read failed");
        need((bytes[0]>>32)==0&&bytes[1]==base+0x1a9054,"HAL adjacent callback guard mismatch");return uint32_t(bytes[0]);}
    void set(uint32_t from,uint32_t to){need(alive(target)&&gate()==from,"HAL gate conflict");need(pwrite(fd,&to,4,base+0x22bf18)==4,"HAL write failed");need(gate()==to,"HAL readback failed");}
};
struct Thread{Identity app,tid;int original=0;};
struct Journal{Identity owner,hal;uint64_t base=0;std::vector<Thread> threads;};
static std::string serialize(const Journal& j){
    std::ostringstream o;o<<j.owner.pid<<' '<<j.owner.start<<' '<<j.hal.pid<<' '<<j.hal.start<<' '<<j.base<<'\n';
    for(auto& t:j.threads)o<<t.app.pid<<' '<<t.app.start<<' '<<t.tid.pid<<' '<<t.tid.start<<' '<<t.original<<'\n';return o.str();
}
static Journal load(){Journal j;std::istringstream in(readfile(STATE+"/journal"));need(bool(in>>j.owner.pid>>j.owner.start>>j.hal.pid>>j.hal.start>>j.base),"Invalid ownership journal");
    in>>std::ws;while(in.peek()!=EOF){Thread t;need(bool(in>>t.app.pid>>t.app.start>>t.tid.pid>>t.tid.start>>t.original),"Invalid thread journal");need(t.original>=-20&&t.original<=19&&t.tid.pid>1&&t.app.pid>1,"Invalid thread fields");j.threads.push_back(t);need(j.threads.size()<=4096,"Oversized journal");in>>std::ws;}return j;}
static void record(const Journal& j){save(STATE+"/journal",serialize(j));}
static bool threadalive(const Thread& t){return alive(t.app)&&alive(t.tid)&&exists(proc(t.app.pid,"task/"+std::to_string(t.tid.pid)));}
static int priority(int tid){errno=0;int v=getpriority(PRIO_PROCESS,tid);need(errno==0,"getpriority failed");return v;}
static bool selected(const std::string& s){return s=="SVLRXThreadUDP\n"||s=="SVLMediaThread\n"||s=="NDK MediaCodec_\n"||s=="CodecLooper\n"||s=="Thread-9\n";}
static void verify(const Journal& j){
    need(alive(j.hal),"HAL restarted; automatic retry blocked");Memory m(j.hal);need(m.base==j.base&&m.gate()==0,"HAL health check failed");
    for(auto& t:j.threads)if(threadalive(t))need(priority(t.tid.pid)==-20&&sched_getscheduler(t.tid.pid)==SCHED_OTHER,"Thread priority changed externally");
}
static void tune(Journal& j){
    Identity app=find(APP);if(!app.pid)return;
    auto old=j.threads.size();j.threads.erase(std::remove_if(j.threads.begin(),j.threads.end(),[](const Thread& t){return !threadalive(t);}),j.threads.end());if(old!=j.threads.size())record(j);
    for(int tid:ids(proc(app.pid,"task"))){
        Thread t{app,{tid,0},0};try{t.tid.start=starttime(tid);if(!selected(readfile(proc(tid,"comm"))))continue;}catch(...){continue;}
        if(sched_getscheduler(tid)!=SCHED_OTHER)continue;
        if(std::any_of(j.threads.begin(),j.threads.end(),[&](const Thread& a){return a.tid.pid==tid&&a.tid.start==t.tid.start;}))continue;
        t.original=priority(tid);if(t.original==-20)continue; // externally owned priority, never restore it
        need(j.threads.size()<4096,"Too many tracked threads");j.threads.push_back(t);record(j); // before mutation
        if(!threadalive(t))continue;
        need(sched_getscheduler(tid)==SCHED_OTHER,"Thread policy changed before apply");
        if(setpriority(PRIO_PROCESS,tid,-20)){if(!threadalive(t))continue;throw std::runtime_error("setpriority denied");}
        if(threadalive(t))need(priority(tid)==-20,"Priority readback failed");
    }
}
static bool restore(Journal& j){
    bool ok=true;
    for(auto& t:j.threads){try{if(!threadalive(t))continue;int now=priority(t.tid.pid);if(now==t.original)continue;
        need(now==-20&&sched_getscheduler(t.tid.pid)==SCHED_OTHER,"Priority restore conflict");
        need(setpriority(PRIO_PROCESS,t.tid.pid,t.original)==0,"Priority restoration denied");
        if(threadalive(t))need(priority(t.tid.pid)==t.original,"Priority restoration readback failed");
    }catch(const std::exception& e){fprintf(stderr,"DISABLED - Restore error: %s\n",e.what());ok=false;}}
    try{if(alive(j.hal)){need(hash(LIB)==HASH,"HAL hash changed before restore");Memory m(j.hal);need(m.base==j.base,"HAL base changed");auto v=m.gate();if(v==0)m.set(0,255);else need(v==255,"HAL restoration conflict");}}
    catch(const std::exception& e){fprintf(stderr,"DISABLED - Restore error: %s\n",e.what());ok=false;}
    if(ok){unlink((STATE+"/journal").c_str());unlink((mod+"/state/active.lease").c_str());}
    return ok;
}
static bool allowed(){return !exists(mod+"/disable")&&!exists(mod+"/remove");}
static void failure(const std::string& s){save(mod+"/disable","");save(mod+"/state/failure",s+"\n");fprintf(stderr,"DISABLED - Error: %s\n",s.c_str());}
int main(int argc,char** argv){
    try{
        need(argc==3,"Usage: gxr-stream-cpu MODULE_DIR run|status|restore");mod=argv[1];std::string mode=argv[2];
        need(getuid()==0,"Root required");umask(077);
        need(mod=="/data/adb/modules/gxr_stream_cpu_relief","Unexpected module location");
        safe_mod=true;
        struct stat st{};
        for(auto& p:std::vector<std::string>{mod,mod+"/state",STATE}){if(lstat(p.c_str(),&st))need(errno==ENOENT&&mkdir(p.c_str(),0700)==0,"Cannot create state directory");else need(S_ISDIR(st.st_mode)&&st.st_uid==0,"Unsafe state directory");}
        if(mode=="status"){
            if(exists(mod+"/state/failure"))printf("Last failure: %s",readfile(mod+"/state/failure").c_str());
            if(!exists(STATE+"/journal")){need(!exists(mod+"/state/active.lease"),"Interrupted lease: runtime restoration unconfirmed");puts("DISABLED - No module-owned live changes; checkbox ON requires standard KernelSU startup.");return 0;}
            auto j=load();verify(j);need(alive(j.owner),"Owner exited: live changes may remain; run Action off");
            char path[4096]{};ssize_t n=readlink(proc(j.owner.pid,"exe").c_str(),path,sizeof(path)-1);need(n>0&&std::string(path,size_t(n))==mod+"/bin/gxr-stream-cpu","Owner executable mismatch");
            printf("%s - Verified LogLevel=0; %zu owned Steam Link threads (exited threads may await pruning). %s\n",allowed()?"ENABLED":"DISABLED",j.threads.size(),allowed()?"Absent/new app threads are polled every 2 seconds.":"Restoration pending.");return 0;
        }
        need(mode=="run"||mode=="restore","Unknown command");
        int lock=open((STATE+"/lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);need(lock>=0,"Lock open failed");
        if(flock(lock,LOCK_EX|LOCK_NB)){puts("DISABLED - Waiting: module worker owns the lock");return 2;}
        if(mode=="restore"){
            if(exists(STATE+"/journal")){auto j=load();need(restore(j),"Restoration incomplete; leave disabled and fully power-cycle");}
            else need(!exists(mod+"/state/active.lease"),"Interrupted lease without RAM journal; leave disabled and fully power-cycle");
            puts("DISABLED - Owned changes restored (or target exited)");return 0;
        }
        if(!allowed())return 0;
        need(!exists(mod+"/state/active.lease")&&!exists(mod+"/state/failure")&&!exists(STATE+"/journal"),"Same-boot failure/interruption: automatic retry blocked");
        need(hash(LIB)==HASH,"Unsupported controller library hash");
        Journal j;j.owner={getpid(),starttime(getpid())};j.hal=find(HAL);need(j.hal.pid,"Controller service absent");
        Memory memory(j.hal);j.base=memory.base;need(memory.gate()==255,"External LogLevel change active; not adopted or overwritten");
        signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
        save(mod+"/state/active.lease",readfile("/proc/sys/kernel/random/boot_id"));record(j);
        int code=0;
        try{
            if(allowed()&&!stopping)memory.set(255,0);
            while(allowed()&&!stopping){verify(j);tune(j);sleep(2);}
            if(stopping)failure("Worker interrupted; disabled pending review");
        }catch(const std::exception& e){failure(e.what());code=1;}
        if(!restore(j)){failure("Restoration incomplete; leave disabled and fully power-cycle");code=1;}
        return code;
    }catch(const std::exception& e){
        fprintf(stderr,"DISABLED - Error: %s\n",e.what());
        if(safe_mod&&argc==3&&std::string(argv[2])!="status")try{failure(e.what());}catch(...){}
        return 1;
    }
}
