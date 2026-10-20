// Standalone candidate: private COW code and private RAM padding only.
#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <array>
#include <cstring>
#include <csignal>
#ifndef GXR_PANELS_EMBEDDED
#include "../../controller-whitening/native/freeze.inc"
#endif
namespace gxr_panels {
#include "panel_code.h"
static void need(bool b,const char* s) { if(!b) throw std::runtime_error(s); }
static constexpr uintptr_t SC=0x50e2540, OC=0x4d5aa40, DATA=0x5137540;
static constexpr size_t STATE_SIZE=64+64*32;
static volatile sig_atomic_t stop_requested=0;
static void stop(int) { stop_requested=1; }
static std::string readfile(const std::string& p) {
    std::ifstream f(p); need(bool(f),"Cannot read identity");
    return {std::istreambuf_iterator<char>(f),{}};
}
static std::string proc(int pid,const char* file) { return "/proc/"+std::to_string(pid)+"/"+file; }
static uint32_t branch(uintptr_t from,uintptr_t to,bool link=false) {
    auto d=static_cast<int64_t>(to)-static_cast<int64_t>(from);
    need(d%4==0 && d>=-(1LL<<27) && d<(1LL<<27),"Branch out of range");
    return (link?0x94000000:0x14000000)|(static_cast<uint32_t>(d/4)&0x3ffffff);
}
struct Memory {
    int fd;
    explicit Memory(int pid):fd(open(proc(pid,"mem").c_str(),O_RDONLY|O_CLOEXEC)) { need(fd>=0,"Cannot read memory"); }
    ~Memory() { close(fd); }
    std::vector<unsigned char> get(uintptr_t a,size_t n) {
        std::vector<unsigned char> v(n);
        need(pread(fd,v.data(),n,a)==static_cast<ssize_t>(n),"Memory read failed"); return v;
    }
    uint64_t word(uintptr_t a) { auto v=get(a,8); uint64_t w; memcpy(&w,v.data(),8); return w; }
};
struct Image { uintptr_t lo,hi; std::string perms,path; };
static std::vector<Image> maps(int pid) {
    need(std::string(readfile(proc(pid,"cmdline")).c_str())=="com.android.systemui","Wrong target");
    std::vector<Image> r; std::istringstream in(readfile(proc(pid,"maps"))); std::string l;
    while(std::getline(in,l)) {
        unsigned long lo,hi,off; char perm[5]{}; int used=0;
        if(sscanf(l.c_str(),"%lx-%lx %4s %lx %*s %*s %n",&lo,&hi,perm,&off,&used)==4)
            r.push_back({lo,hi,perm,l.substr(used)});
    }
    return r;
}
static uintptr_t base(const std::vector<Image>& m,const char* name) {
    std::string path="/system_ext/lib64/"+std::string(name);
    for(const auto& x:m) if(x.path==path) return x.lo;
    throw std::runtime_error("Library absent");
}
static void region(const std::vector<Image>& m,uintptr_t a,size_t n,const char* perm) {
    bool found=false; for(const auto& x:m) if(x.lo<=a && a+n<=x.hi && x.perms==perm) found=true;
    need(found,"Unexpected mapping permissions/bounds");
}
struct Block { uintptr_t addr; std::vector<unsigned char> stock,patch; };
template<size_t N> static std::vector<unsigned char> bytes(const unsigned char (&a)[N]) { return {a,a+N}; }
static void q(std::vector<unsigned char>& b,size_t o,uint64_t n) { need(o+8<=b.size(),"Payload offset"); memcpy(b.data()+o,&n,8); }
static void w(std::vector<unsigned char>& b,size_t o,uint32_t n) { need(o+4<=b.size(),"Payload offset"); memcpy(b.data()+o,&n,4); }
struct Patch {
    uintptr_t s,o,i,data; std::vector<Block> blocks;
    explicit Patch(int pid) {
        auto m=maps(pid);
        s=base(m,"libspaceflinger_jni_vulkan.so"); o=base(m,"libsysui_jni_soong_vulkan.so");
        i=base(m,"libImpress-jni-vulkan.so"); data=s+DATA;
        region(m,s+SC,256+sizeof(PAYLOAD),"r-xp");
        region(m,o+OC,sizeof(OWNER),"r-xp"); region(m,data,STATE_SIZE,"rw-p");
        auto sc=bytes(SPACE), oc=bytes(OWNER);
        q(sc,OFF_frame_config,data); q(sc,OFF_frame_target,s+SC+256+OFF_panel_frame);
        w(sc,OFF_frame_return,branch(s+SC+OFF_frame_return,s+0x4d3ee08));
        q(sc,OFF_mirror_config,data); q(sc,OFF_mirror_target,s+SC+256+OFF_panel_mirror);
        q(oc,OFF_owner_config,data); q(oc,OFF_owner_target,s+SC+256+OFF_panel_owner);
        w(oc,OFF_owner_return,branch(o+OC+OFF_owner_return,o+0x4ce92cc));
        sc.resize(256); sc.insert(sc.end(),PAYLOAD,PAYLOAD+sizeof(PAYLOAD));
        blocks.push_back({s+SC,std::vector<unsigned char>(sc.size()),sc});
        blocks.push_back({o+OC,std::vector<unsigned char>(oc.size()),oc});
        auto site=[&](uintptr_t a,auto original,uintptr_t target,bool link) {
            auto b=original; w(b,0,branch(a,target,link)); blocks.push_back({a,original,b});
        };
        site(s+0x4d3ee04,bytes(FRAME_EXPECTED),s+SC+OFF_frame_hook,false);
        site(s+0x4d15490,bytes(MIRROR_EXPECTED),s+SC+OFF_mirror_hook,true);
        site(o+0x4ce92c8,bytes(OWNER_EXPECTED),o+OC+OFF_owner_hook,false);
    }
    bool validate(Memory& mem) {
        const auto last=mem.get(blocks.back().addr,blocks.back().stock.size());
        bool active=last==blocks.back().patch;
        need(active || last==blocks.back().stock,"Unknown owner hook");
        for(const auto& b:blocks) need(mem.get(b.addr,b.stock.size())==(active?b.patch:b.stock),"Unknown/partial panel patch");
        auto state=mem.get(data,STATE_SIZE);
        if(active) need(mem.word(data)==i+0x12ebfe8 && mem.word(data+8)==o+0x4d4e7e0,"Unknown panel state");
        else need(state==std::vector<unsigned char>(STATE_SIZE),"Unowned RAM padding");
        return active;
    }
    void change(int pid,bool enable) {
        Memory mem(pid); (void)validate(mem);
        Frozen f; f.freeze(pid);
        Patch current(pid); need(current.s==s && current.o==o && current.i==i,"Mapping changed");
        bool before=validate(mem);
        for(const auto& b:blocks) f.requireNoPC(b.addr,b.addr+b.stock.size());
        need(!before || mem.word(data+32)==0,"Panel callback active; retry after detach");
        if(before==enable) { need(f.release(),"Detach failed"); return; }
        struct Word {uintptr_t addr; uint64_t old,next;}; std::vector<Word> words;
        auto add=[&](uintptr_t addr,const std::vector<unsigned char>& b) {
            for(uintptr_t a=addr&~uintptr_t(7);a<addr+b.size();a+=8) {
                Word v{a,mem.word(a),0}; v.next=v.old;
                for(size_t j=0;j<8;j++) if(a+j>=addr && a+j<addr+b.size())
                    reinterpret_cast<unsigned char*>(&v.next)[j]=b[a+j-addr];
                words.push_back(v);
            }
        };
        std::vector<unsigned char> state(STATE_SIZE);
        if(enable) { q(state,0,i+0x12ebfe8); q(state,8,o+0x4d4e7e0); }
        add(data,state);
        for(const auto& b:blocks) add(b.addr,enable?b.patch:b.stock);
        try {
            for(const auto& v:words)
                need(ptrace(PTRACE_POKETEXT,pid,reinterpret_cast<void*>(v.addr),
                            reinterpret_cast<void*>(v.next))==0,"Panel write failed");
            need(validate(mem)==enable,"Panel readback failed");
        } catch(...) {
            bool ok=true;
            for(const auto& v:words) {
                if(ptrace(PTRACE_POKETEXT,pid,reinterpret_cast<void*>(v.addr),reinterpret_cast<void*>(v.old))) ok=false;
                if(mem.word(v.addr)!=v.old) ok=false;
            }
            if(!ok) fprintf(stderr,"RESTORE FAILED; fully power off and reboot\n");
            throw;
        }
        need(f.release(),"Detach failed");
    }
};
static void change_retry(Patch& p,int pid,bool on) {
    for(int n=0;;n++) {
        try { p.change(pid,on); return; }
        catch(const std::exception& e) {
            std::string why=e.what();
            if(n==9 || (why!="Panel callback active; retry after detach" && why.find("PC")==std::string::npos)) throw;
            usleep(20000);
        }
    }
}
[[maybe_unused]] static int run(int argc,char** argv) {
    try {
        need(argc==3,"Usage: gxr-panels PID status|on|off|test|test-ui");
        char* end; long id=strtol(argv[1],&end,10);
        need(!*end && id>1 && id<INT32_MAX,"Invalid PID"); int pid=id;
        std::string mode=argv[2];
        need(mode=="status"||mode=="on"||mode=="off"||mode=="test"||mode=="test-ui","Invalid mode");
        Patch p(pid); Memory mem(pid); bool active=p.validate(mem);
        if(mode=="test"||mode=="test-ui") {
            need(!active,"Test requires stock panel code");
            signal(SIGINT,stop); signal(SIGTERM,stop); signal(SIGHUP,stop);
            need(prctl(PR_SET_TIMERSLACK,1UL,0UL,0UL,0UL)==0,"Cannot bound timer");
            change_retry(p,pid,true); puts("ENABLED - bounded panel coalescing test"); fflush(stdout);
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(mode=="test-ui"?60:20);
            while(!stop_requested && std::chrono::steady_clock::now()<deadline) usleep(100000);
            printf("Counters: epoch=%llu deferred=%llu resolved=%llu misses=%llu\n",
                   (unsigned long long)mem.word(p.data+24),(unsigned long long)mem.word(p.data+40),
                   (unsigned long long)mem.word(p.data+48),(unsigned long long)mem.word(p.data+56));
            for(int n=0;n<64;n++) {
                uintptr_t e=p.data+64+n*32;
                if(mem.word(e)) printf("Owner[%d]: controller=%llx entity=%llx seen=%llu pending=%llu\n",
                    n,(unsigned long long)mem.word(e),(unsigned long long)mem.word(e+8),
                    (unsigned long long)mem.word(e+16),(unsigned long long)mem.word(e+24));
            }
            change_retry(p,pid,false); puts("DISABLED - original instructions and padding restored");
        } else {
            if(mode!="status") change_retry(p,pid,mode=="on");
            printf("%s - panel coalescing; pid=%d\n",p.validate(mem)?"ENABLED":"DISABLED",pid);
        }
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"ERROR: %s\n",e.what()); return 1; }
}
} // namespace gxr_panels
#ifndef GXR_PANELS_EMBEDDED
int main(int argc,char** argv) { return gxr_panels::run(argc,argv); }
#endif
