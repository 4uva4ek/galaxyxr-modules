// Author: 4uva4ek. Based on CLD's 2026-10-17 measurements.
// Data-only controller HAL log gate; owned by Controller Suite.
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
static const std::string STATE="/dev/gxr_controller_log_relief";
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
struct Journal{Identity owner,hal;uint64_t base=0;};
static std::string serialize(const Journal& j){
    std::ostringstream o;o<<j.owner.pid<<' '<<j.owner.start<<' '<<j.hal.pid<<' '<<j.hal.start<<' '<<j.base<<'\n';return o.str();
}
static Journal load(){Journal j;std::istringstream in(readfile(STATE+"/journal"));
    need(bool(in>>j.owner.pid>>j.owner.start>>j.hal.pid>>j.hal.start>>j.base),"Invalid ownership journal");
    in>>std::ws;need(in.eof()&&j.owner.pid>1&&j.hal.pid>1&&j.base,"Invalid HAL journal");return j;
}
static void record(const Journal& j){save(STATE+"/journal",serialize(j));}
static void verify(const Journal& j){
    need(alive(j.hal),"HAL restarted; automatic retry blocked");Memory m(j.hal);
    need(m.base==j.base&&m.gate()==0,"HAL health check failed");
}
static bool restore(Journal& j){
    try{if(alive(j.hal)){need(hash(LIB)==HASH,"HAL hash changed before restore");Memory m(j.hal);need(m.base==j.base,"HAL base changed");
        auto v=m.gate();if(v==0)m.set(0,255);else need(v==255,"HAL restoration conflict");}
        unlink((STATE+"/journal").c_str());unlink((mod+"/state/active.lease").c_str());return true;
    }catch(const std::exception& e){fprintf(stderr,"DISABLED - Restore error: %s\n",e.what());return false;}
}
static bool allowed(){return !exists(mod+"/disable")&&!exists(mod+"/remove");}
static void failure(const std::string& s){save(mod+"/disable","");save(mod+"/state/failure",s+"\n");fprintf(stderr,"DISABLED - Error: %s\n",s.c_str());}
int main(int argc,char** argv){
    try{
        need(argc==3,"Usage: gxr-controller-log MODULE_DIR run|status|restore");mod=argv[1];std::string mode=argv[2];
        need(getuid()==0,"Root required");umask(077);
        need(mod=="/data/adb/modules/gxr_controller_log_relief","Unexpected module location");
        safe_mod=true;
        struct stat st{};
        for(auto& p:std::vector<std::string>{mod,mod+"/state",STATE}){if(lstat(p.c_str(),&st))need(errno==ENOENT&&mkdir(p.c_str(),0700)==0,"Cannot create state directory");else need(S_ISDIR(st.st_mode)&&st.st_uid==0,"Unsafe state directory");}
        if(mode=="status"){
            if(exists(mod+"/state/failure"))printf("Last failure: %s",readfile(mod+"/state/failure").c_str());
            if(!exists(STATE+"/journal")){need(!exists(mod+"/state/active.lease"),"Interrupted lease: runtime restoration unconfirmed");puts("DISABLED - No module-owned live changes; checkbox ON requires standard KernelSU startup.");return 0;}
            auto j=load();verify(j);need(alive(j.owner),"Owner exited: live changes may remain; run Action off");
            char path[4096]{};ssize_t n=readlink(proc(j.owner.pid,"exe").c_str(),path,sizeof(path)-1);need(n>0&&std::string(path,size_t(n))==mod+"/bin/gxr-controller-log","Owner executable mismatch");
            printf("%s - Verified controller HAL LogLevel=0. No application priorities are changed.\n",allowed()?"ENABLED":"DISABLED");return 0;
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
            while(allowed()&&!stopping){verify(j);sleep(2);}
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
