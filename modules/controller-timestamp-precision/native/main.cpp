// Guarded SYSTEM SERVICE private-memory helper. Module scripts control startup and ownership.
// Common process/ptrace helpers adapted from diagnostics/hal-prediction-no-shizuku-20261011/native-test/main.cpp.
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
    std::istringstream lines(maps);std::string line;Map result;
    while(std::getline(lines,line)) {
        unsigned long lo,hi,off;char perm[5]{};int used=0;
        if(sscanf(line.c_str(),"%lx-%lx %4s %lx %*s %*s %n",&lo,&hi,perm,&off,&used)!=4) continue;
        std::string path=line.substr(used);
        if(path!="/vendor/lib64/hw/vendor.samsung.hardware.secxrcontroller@1.0-impl.so")continue;
        if(strcmp(perm,"r-xp")||off!=0x16000) continue;
        require(!result.base,"Ambiguous runtime mapping");
        require(lo>=off&&hi>lo,"Invalid runtime mapping");
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

#include "freeze.inc"
#include "control.inc"

