// Private SystemUI memory only. No remote calls, mounts or original file writes.
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <array>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <csignal>
#if !defined(GXR_SCHEMA_CANDIDATE) && !defined(GXR_DEPTH_CANDIDATE) && !defined(GXR_WAIT_FIX)
#define GXR_WAIT_FIX
#endif
#if defined(GXR_WAIT_FIX)
#include "wait_expected.h"
namespace depth {
#include "expected.h"
static constexpr uintptr_t FUNCTION=0x104b974, END=0x104ba8c;
static constexpr uintptr_t SITE1=0x104b9c4, SITE2=0x104b9d8;
static constexpr uint32_t STOCK1=0xf9403d00, STOCK2=0xf9403d02;
}
#elif !defined(GXR_SCHEMA_CANDIDATE)
#include "expected.h"
#else
#include "schema_expected.h"
#include "schema_code.h"
#endif
#include "../../controller-whitening/native/freeze.inc"
#if defined(GXR_WAIT_FIX) && !defined(GXR_BASELINE_ONLY)
#define GXR_PANELS_EMBEDDED
#include "panels.cpp"
#define GXR_COMBINED_PANELS
#endif

static constexpr char LIB[] = "/system_ext/lib64/libImpress-jni-vulkan.so";
#if defined(GXR_WAIT_FIX)
static constexpr uintptr_t FUNCTION=0x104afe8, END=0x104b070;
static constexpr uintptr_t SITE1=0x104b044, SITE2=0x104b058, SECOND=12;
static constexpr uint32_t STOCK1=0x94157353, STOCK2=0x9415734e;
#if defined(GXR_COMBINED_PANELS)
static constexpr char DESCRIPTION[]="single swapchain wait, selected-depth release/submit and panel coalescing";
#else
static constexpr char DESCRIPTION[]="single swapchain wait and selected-depth release/submit";
#endif
#elif !defined(GXR_SCHEMA_CANDIDATE)
static constexpr uintptr_t FUNCTION = 0x104b974, END = 0x104ba8c;
static constexpr uintptr_t SITE1 = 0x104b9c4, SITE2 = 0x104b9d8, SECOND = 12;
static constexpr uint32_t STOCK1=0xf9403d00, STOCK2=0xf9403d02;
static constexpr char DESCRIPTION[]="selected depth release/submit";
#else
static constexpr uintptr_t FUNCTION = 0xa04598, END = 0xa04968;
static constexpr uintptr_t SITE1 = 0xa04650, SITE2 = 0xa047bc, SECOND = 64;
static constexpr uint32_t STOCK1=0x942e5734, STOCK2=0x942e56dd;
static constexpr char DESCRIPTION[]="schema-validation fast paths";
#endif
static constexpr uintptr_t CAVE = 0x15b8f10;
static volatile sig_atomic_t interrupted = 0;
static void stop(int) { interrupted = 1; }
static void need(bool yes, const char* why) { if (!yes) throw std::runtime_error(why); }
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    need(bool(f), "Cannot read identity file");
    return {std::istreambuf_iterator<char>(f), {}};
}
static std::string proc(pid_t pid, const char* f) {
    return "/proc/" + std::to_string(pid) + "/" + f;
}
static uintptr_t mapping(pid_t pid) {
    const auto cmd = readFile(proc(pid, "cmdline"));
    need(std::string(cmd.c_str()) == "com.android.systemui", "Wrong process");
    std::istringstream in(readFile(proc(pid, "maps")));
    uintptr_t base = 0; std::string line;
    while (std::getline(in, line)) {
        unsigned long lo, hi, off; char perms[5]{}; int used = 0;
        if (sscanf(line.c_str(), "%lx-%lx %4s %lx %*s %*s %n", &lo, &hi, perms, &off, &used) != 4) continue;
        if (line.substr(used) != LIB || strcmp(perms, "r-xp")) continue;
        need(!base && off == 0 && hi-lo == 0x15b9000, "Unexpected executable mapping");
        base = lo;
    }
    need(base != 0, "Pinned library absent");
    return base;
}
struct Memory {
    int fd;
    explicit Memory(pid_t pid):fd(open(proc(pid,"mem").c_str(), O_RDONLY|O_CLOEXEC)) {
        need(fd >= 0, "Cannot read target memory");
    }
    ~Memory() { close(fd); }
    void get(uintptr_t addr, void* out, size_t n) {
        need(pread(fd,out,n,addr) == static_cast<ssize_t>(n), "Target read failed");
    }
};
[[maybe_unused]] static uint32_t branch(uintptr_t from, uintptr_t to) {
    const int64_t delta = static_cast<int64_t>(to)-static_cast<int64_t>(from);
    need(delta % 4 == 0 && delta >= -(1LL<<27) && delta < (1LL<<27), "Branch out of range");
    return 0x14000000 | (static_cast<uint32_t>(delta/4)&0x03ffffff);
}
static auto thunk() {
#if defined(GXR_WAIT_FIX)
    return std::array<uint32_t,6>{0xf9407500, 0xf9400000, branch(CAVE+8,depth::SITE1+4),
            0xf9407502, 0xf9400042, branch(CAVE+20,depth::SITE2+4)};
#elif !defined(GXR_SCHEMA_CANDIDATE)
    // Selected depth pointer is set by SwitchSwapchainLayers for both modes.
    // x8 and flags retain their original value; x0/x2 are the original outputs.
    return std::array<uint32_t,6>{0xf9407500, 0xf9400000, branch(CAVE+8,SITE1+4),
            0xf9407502, 0xf9400042, branch(CAVE+20,SITE2+4)};
#else
    return SCHEMA_CODE;
#endif
}
static uint32_t route(uintptr_t site,uintptr_t target) {
#if defined(GXR_WAIT_FIX)
    (void)site; (void)target;
    return 0xd503201f; // NOP: inner acquire already waited on both selected images.
#elif !defined(GXR_SCHEMA_CANDIDATE)
    return branch(site,target);
#else
    return branch(site,target)|0x80000000; // BL; the thunk RET/fallback preserves vendor return.
#endif
}
static bool validate(Memory& m, uintptr_t base) {
    std::array<unsigned char,END-FUNCTION> code{};
    decltype(thunk()) cave{};
    m.get(base+FUNCTION,code.data(),code.size());
    m.get(base+CAVE,cave.data(),sizeof(cave));
    uint32_t a,b;
    memcpy(&a,code.data()+SITE1-FUNCTION,4);
    memcpy(&b,code.data()+SITE2-FUNCTION,4);
    const bool patched = a == route(SITE1,CAVE) && b == route(SITE2,CAVE+SECOND);
    need(patched || (a==STOCK1 && b==STOCK2), "Unknown/partial patch");
    if (patched) {
        a=STOCK1; b=STOCK2;
        memcpy(code.data()+SITE1-FUNCTION,&a,4);
        memcpy(code.data()+SITE2-FUNCTION,&b,4);
    }
    need(memcmp(code.data(),EXPECTED,code.size())==0, "Function identity mismatch");
#if defined(GXR_WAIT_FIX)
    std::array<unsigned char,depth::END-depth::FUNCTION> depthCode{};
    m.get(base+depth::FUNCTION,depthCode.data(),depthCode.size());
    uint32_t da,db;
    memcpy(&da,depthCode.data()+depth::SITE1-depth::FUNCTION,4);
    memcpy(&db,depthCode.data()+depth::SITE2-depth::FUNCTION,4);
    need(da==(patched?branch(depth::SITE1,CAVE):depth::STOCK1) &&
         db==(patched?branch(depth::SITE2,CAVE+12):depth::STOCK2),
         "Unknown/partial combined depth patch");
    da=depth::STOCK1; db=depth::STOCK2;
    memcpy(depthCode.data()+depth::SITE1-depth::FUNCTION,&da,4);
    memcpy(depthCode.data()+depth::SITE2-depth::FUNCTION,&db,4);
    need(memcmp(depthCode.data(),depth::EXPECTED,depthCode.size())==0,"Depth function identity mismatch");
#endif
    const decltype(thunk()) zero{};
    need(cave == (patched ? thunk() : zero), "Code padding mismatch");
    return patched;
}
static void change(pid_t pid, bool enable) {
    uintptr_t base=mapping(pid);
    Memory mem(pid);
    (void)validate(mem,base);
    Frozen frozen;
    frozen.freeze(pid);
    need(mapping(pid)==base,"Mapping changed");
    bool before=validate(mem,base);
    frozen.requireNoPC(base+CAVE,base+CAVE+sizeof(thunk()));
    frozen.requireNoPC(base+FUNCTION,base+END);
#if defined(GXR_WAIT_FIX)
    frozen.requireNoPC(base+depth::FUNCTION,base+depth::END);
#endif
    if (before==enable) {
        need(frozen.release(),"Detach failed");
        return;
    }
    struct Word { uintptr_t addr; uint64_t old, next; };
    std::vector<Word> words;
    auto add=[&](uintptr_t offset,const void* data,size_t n) {
        auto* bytes=static_cast<const unsigned char*>(data);
        for(uintptr_t addr=(base+offset)&~uintptr_t(7);addr<base+offset+n;addr+=8) {
            Word w{addr,0,0}; mem.get(addr,&w.old,8); w.next=w.old;
            for(size_t j=0;j<8;j++)
                if(addr+j>=base+offset && addr+j<base+offset+n)
                    reinterpret_cast<unsigned char*>(&w.next)[j]=bytes[addr+j-base-offset];
            words.push_back(w);
        }
    };
    const auto body=enable?thunk():decltype(thunk()){};
    const uint32_t a=enable?route(SITE1,CAVE):STOCK1;
    const uint32_t b=enable?route(SITE2,CAVE+SECOND):STOCK2;
    add(CAVE,body.data(),sizeof(body)); add(SITE1,&a,4); add(SITE2,&b,4);
#if defined(GXR_WAIT_FIX)
    const uint32_t da=enable?branch(depth::SITE1,CAVE):depth::STOCK1;
    const uint32_t db=enable?branch(depth::SITE2,CAVE+12):depth::STOCK2;
    add(depth::SITE1,&da,4); add(depth::SITE2,&db,4);
#endif
    try {
        for(const auto& w:words)
            need(ptrace(PTRACE_POKETEXT,pid,reinterpret_cast<void*>(w.addr),
                        reinterpret_cast<void*>(w.next))==0,"Patch write failed");
        need(validate(mem,base)==enable,"Readback failed");
    } catch (...) {
        bool restored=true;
        for(const auto& w:words) {
            if(ptrace(PTRACE_POKETEXT,pid,reinterpret_cast<void*>(w.addr),
                      reinterpret_cast<void*>(w.old))!=0) restored=false;
            uint64_t check=0;
            try { mem.get(w.addr,&check,8); if(check!=w.old) restored=false; } catch (...) { restored=false; }
        }
        if(!restored) fprintf(stderr,"RESTORE FAILED: leave disabled; fully power off and reboot\n");
        throw;
    }
    need(frozen.release(),"Detach failed; full power cycle required");
}
static bool validate_all(pid_t pid) {
    Memory mem(pid); bool active=validate(mem,mapping(pid));
#ifdef GXR_COMBINED_PANELS
    gxr_panels::Patch panel(pid); gxr_panels::Memory pmem(pid);
    need(panel.validate(pmem)==active,"Mixed combined patch state; use off to restore");
#endif
    return active;
}
static void change_baseline_retry(pid_t pid,bool enable) {
    for(int n=0;;n++) {
        try { change(pid,enable); return; }
        catch(const std::exception& e) {
            if(n==9 || std::string(e.what())!="PC in patch window") throw;
            usleep(20000);
        }
    }
}
static void change_all(pid_t pid,bool enable) {
#ifdef GXR_COMBINED_PANELS
    if(enable) {
        gxr_panels::Patch panel(pid); gxr_panels::Memory pmem(pid);
        Memory mem(pid);
        bool baseline_before=validate(mem,mapping(pid));
        bool panels_before=panel.validate(pmem);
        need(baseline_before==panels_before,"Mixed combined patch state; use off to restore");
        if(baseline_before) return;
        try {
            change_baseline_retry(pid,true);
            gxr_panels::change_retry(panel,pid,true);
            need(validate_all(pid),"Combined enable readback failed");
        } catch(...) {
            // Restore both components even when only one applied. Never hide
            // a cleanup failure or leave the old two fixes silently active.
            bool restored=true;
            try { gxr_panels::change_retry(panel,pid,false); } catch(...) { restored=false; }
            try { change_baseline_retry(pid,false); } catch(...) { restored=false; }
            if(!restored) fprintf(stderr,"RESTORE FAILED: full power cycle required\n");
            throw;
        }
    } else {
        // Off also accepts a known mixed state after an interrupted activation.
        std::string errors;
        try {
            gxr_panels::Patch panel(pid);
            gxr_panels::change_retry(panel,pid,false);
        }
        catch(const std::exception& e) { errors+="Panels: "+std::string(e.what()); }
        try { change_baseline_retry(pid,false); }
        catch(const std::exception& e) { errors+=" Baseline: "+std::string(e.what()); }
        if(!errors.empty()) throw std::runtime_error(errors);
        need(!validate_all(pid),"Combined restoration readback failed");
    }
#else
    change_baseline_retry(pid,enable);
#endif
}
int main(int argc,char** argv) {
    try {
        need(argc==3,"Usage: gxr-systemui PID status|on|off|test|test-ui");
        char* end=nullptr; long value=strtol(argv[1],&end,10);
        need(end && !*end && value>1 && value<INT32_MAX,"Invalid PID");
        const pid_t pid=value;
        const std::string mode=argv[2];
        need(mode=="status"||mode=="on"||mode=="off"||mode=="test"||mode=="test-ui","Invalid mode");
        std::ifstream disk(LIB,std::ios::binary);
        need(bool(disk),"Cannot read library");
        disk.seekg(0,std::ios::end);
        need(disk.tellg()==23615760,"Unsupported library size");
        std::array<unsigned char,sizeof(EXPECTED)> original{};
        disk.seekg(FUNCTION); disk.read(reinterpret_cast<char*>(original.data()),original.size());
        need(bool(disk) && !memcmp(original.data(),EXPECTED,sizeof(EXPECTED)),"Unsupported library");
#if defined(GXR_WAIT_FIX)
        std::array<unsigned char,sizeof(depth::EXPECTED)> originalDepth{};
        disk.seekg(depth::FUNCTION);
        disk.read(reinterpret_cast<char*>(originalDepth.data()),originalDepth.size());
        need(bool(disk) && !memcmp(originalDepth.data(),depth::EXPECTED,sizeof(depth::EXPECTED)),
             "Unsupported depth function");
#endif
        std::array<unsigned char,sizeof(thunk())> padding{};
        disk.seekg(CAVE); disk.read(reinterpret_cast<char*>(padding.data()),padding.size());
        need(bool(disk) && padding==decltype(padding){},"Disk padding mismatch");
        if(mode=="off") {
            change_all(pid,false);
            printf("DISABLED - %s; pid=%d\n",DESCRIPTION,pid);
            return 0;
        }
        bool active=validate_all(pid);
        if(mode=="test"||mode=="test-ui") {
            need(!active,"Test requires stock state");
            signal(SIGINT,stop); signal(SIGTERM,stop); signal(SIGHUP,stop);
            need(prctl(PR_SET_TIMERSLACK,1UL,0UL,0UL,0UL)==0,"Cannot bound test timer slack");
            change_all(pid,true);
            printf("ENABLED - bounded %s test\n",DESCRIPTION); fflush(stdout);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(mode=="test-ui"?60:20);
            while(!interrupted && std::chrono::steady_clock::now()<deadline) usleep(100000);
            change_all(pid,false);
            puts("DISABLED - original instructions and padding restored");
            return 0;
        }
        if(mode!="status") change_all(pid,mode=="on");
        active=validate_all(pid);
        printf("%s - %s; pid=%d\n",active?"ENABLED":"DISABLED",DESCRIPTION,pid);
        return 0;
    } catch(const std::exception& e) {
        fprintf(stderr,"ERROR: %s; verify stock state before retry. Full power cycle clears private patches.\n",e.what());
        return 1;
    }
}
