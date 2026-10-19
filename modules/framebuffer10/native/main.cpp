// Color-corrected framebuffer session. Original system files are never written.
// KernelSU disable/remove stops the session; interrupted leases prevent retries.
#include <sys/system_properties.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>
#include <sys/xattr.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <algorithm>
static const char* DIR="/dev/gxr-framebuffer10";
static const char* MODULE="/data/adb/modules/gxr_framebuffer10";
static const char* KEY="cpm.10bit_framebuffers";
static const char* LIB="/system/lib64/libvulkan_compositor.so";
static const char* PAYLOAD="/data/adb/modules/gxr_framebuffer10/payload/libvulkan_compositor.so";
static const char* STOCK="54f9c32c6a80198dff06ae8cda56a0f81d87b660c67f62bc452725f241e0733e";
static const char* PATCH="636be1dc30af00906fdf23238f9a37ea87139b534a01d954ef063f7a063d59e2";
#include "payload_patch.inc"
static volatile sig_atomic_t interrupted=0;
static void stop_signal(int) { interrupted=1; }
static uint64_t real_ms() { timespec t{}; if(clock_gettime(CLOCK_BOOTTIME,&t)) _exit(90); return uint64_t(t.tv_sec)*1000+t.tv_nsec/1000000; }
struct Result { int code; std::string text; };
static Result command(std::vector<std::string> args, unsigned timeout=4000) {
    int p[2]; if(pipe2(p,O_CLOEXEC)) return {126,"pipe failed"};
    pid_t child=fork();
    if(child<0) {close(p[0]);close(p[1]);return {126,"fork failed"};}
    if(!child) {
        dup2(p[1],1);dup2(p[1],2);close(p[0]);close(p[1]);
        std::vector<char*> av; for(auto& a:args) av.push_back(a.data());av.push_back(nullptr);
        execv(av[0],av.data());_exit(127);
    }
    close(p[1]);fcntl(p[0],F_SETFL,O_NONBLOCK);
    uint64_t end=real_ms()+timeout;std::string out;int status=0;bool done=false,expired=false;
    while(!done) {
        char b[16384];ssize_t n;
        while((n=read(p[0],b,sizeof(b)))>0) {if(out.size()+size_t(n)>8*1024*1024) {expired=true;break;} out.append(b,n);}
        if(waitpid(child,&status,WNOHANG)==child) {done=true;break;}
        if(expired || real_ms()>=end) {kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){} expired=true;break;}
        pollfd q{p[0],POLLIN,0};poll(&q,1,10);
    }
    char b[16384];ssize_t n;while((n=read(p[0],b,sizeof(b)))>0 && out.size()+size_t(n)<=8*1024*1024) out.append(b,n);
    close(p[0]);return {expired?124:WIFEXITED(status)?WEXITSTATUS(status):128,out};
}
static std::string readfile(const std::string& p) {
    int f=open(p.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return "";
    std::string s;char b[4096];ssize_t n;while((n=read(f,b,sizeof(b)))>0 && s.size()<1024*1024)s.append(b,n);close(f);return s;
}
static bool digest(const char* p,const char* expected){auto r=command({"/system/bin/sha256sum",p});return r.code==0&&r.text.rfind(expected,0)==0;}
static std::string linkname(const std::string& p){char b[256];ssize_t n=readlink(p.c_str(),b,sizeof(b));return n>0?std::string(b,size_t(n)):"";}
// The package ships no vendor code. The payload is generated here from the
// device's own stock library with the project edit script (payload_patch.inc):
// per shader slot a new header, then COPY/SKIP counts of stock instructions and
// INSERTed project instructions. Any mismatch fails closed.
static bool readAll(const char* p,std::string& s){
    int f=open(p,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return false;
    struct stat st{};if(fstat(f,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>256*1024*1024){close(f);return false;}
    s.resize(size_t(st.st_size));size_t pos=0;
    while(pos<s.size()){ssize_t n=read(f,&s[pos],s.size()-pos);if(n<=0){close(f);return false;}pos+=size_t(n);}
    close(f);return true;
}
static bool applyPatch(const std::string& src,std::string& out,std::string& why){
    const uint32_t* p=kPayloadPatch;const size_t total=sizeof(kPayloadPatch)/4;
    if(total<2||p[0]!=0x30314246){why="edit script header";return false;}
    out=src;size_t k=2;
    for(uint32_t slot=0;slot<p[1];slot++){
        if(k+3>total){why="edit script truncated";return false;}
        size_t start=p[k],len=p[k+1],count=p[k+2];k+=3;
        if(start%4||len%4||len<20||start+len+4>src.size()||k+count>total||count<6){why="slot bounds";return false;}
        const uint32_t* ops=p+k;k+=count;
        std::vector<uint32_t> in(len/4);memcpy(in.data(),src.data()+start,len);
        uint32_t lenWord;memcpy(&lenWord,src.data()+start+len,4);
        if(in[0]!=0x07230203||lenWord!=len){why="stock shader slot";return false;}
        std::vector<size_t> at;for(size_t w=5;w<in.size();){size_t n=in[w]>>16;if(!n||w+n>in.size()){why="stock instruction stream";return false;}at.push_back(w);w+=n;}
        std::vector<uint32_t> res(ops,ops+5);size_t i=0,o=5;
        for(;;){
            if(o>=count){why="edit script stream";return false;}
            uint32_t kind=ops[o]>>28,n=ops[o]&0x0fffffff;o++;
            if(kind==0)break;
            if(kind==1||kind==2){
                if(i+n>at.size()){why="copy past end";return false;}
                if(kind==1){size_t a=at[i],b=i+n<at.size()?at[i+n]:in.size();res.insert(res.end(),in.begin()+long(a),in.begin()+long(b));}
                i+=n;
            }else if(kind==3){
                if(o+n>count){why="insert past end";return false;}
                res.insert(res.end(),ops+o,ops+o+n);o+=n;
            }else{why="edit operation";return false;}
        }
        if(i!=at.size()||o!=count||res.size()*4!=len){why="slot result size";return false;}
        memcpy(&out[start],res.data(),len);
    }
    if(k!=total){why="edit script trailing data";return false;}
    return true;
}
static int buildPayload(){
    if(geteuid()!=0){puts("DISABLED - root required to generate payload");return 1;}
    if(digest(PAYLOAD,PATCH)){puts("PAYLOAD OK - generated color library present and verified");return 0;}
    if(!digest(LIB,STOCK)){puts("DISABLED - stock compositor library is not the supported build; payload not generated");return 2;}
    std::string src,out,why;
    if(!readAll(LIB,src)){puts("DISABLED - cannot read stock compositor library");return 3;}
    if(!applyPatch(src,out,why)){printf("DISABLED - payload edit script rejected: %s\n",why.c_str());return 3;}
    std::string dir=std::string(MODULE)+"/payload",tmp=std::string(PAYLOAD)+".tmp";
    mkdir(dir.c_str(),0755);unlink(tmp.c_str());
    int f=open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(f<0){puts("DISABLED - cannot create payload file");return 3;}
    size_t pos=0;while(pos<out.size()){ssize_t n=write(f,out.data()+pos,out.size()-pos);if(n<=0){close(f);unlink(tmp.c_str());puts("DISABLED - payload write failed");return 3;}pos+=size_t(n);}
    bool ok=fsync(f)==0&&fchown(f,0,0)==0&&fchmod(f,0444)==0&&
        fsetxattr(f,"security.selinux","u:object_r:system_lib_file:s0",sizeof("u:object_r:system_lib_file:s0"),0)==0;
    close(f);
    if(!ok||!digest(tmp.c_str(),PATCH)){unlink(tmp.c_str());puts("DISABLED - generated payload failed verification");return 3;}
    if(rename(tmp.c_str(),PAYLOAD)){unlink(tmp.c_str());puts("DISABLED - cannot install generated payload");return 3;}
    int d=open(dir.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(d>=0){fsync(d);close(d);}
    puts("PAYLOAD OK - color library generated from the stock library and verified");return 0;
}
// Host/offline check of the same edit script: apply to a stock copy.
static int applyFile(const char* in,const char* outPath){
    std::string src,out,why;if(!readAll(in,src)){puts("cannot read input");return 1;}
    if(!applyPatch(src,out,why)){printf("rejected: %s\n",why.c_str());return 1;}
    int f=open(outPath,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0644);if(f<0)return 1;
    size_t pos=0;while(pos<out.size()){ssize_t n=write(f,out.data()+pos,out.size()-pos);if(n<=0){close(f);return 1;}pos+=size_t(n);}
    close(f);puts("APPLIED - verify the SHA-256 against build.json");return 0;
}
static bool sameFile(const char* a,const char* b){struct stat x{},y{};return !stat(a,&x)&&!stat(b,&y)&&x.st_dev==y.st_dev&&x.st_ino==y.st_ino;}
static bool mounted(){std::istringstream in(readfile("/proc/self/mountinfo"));std::string l;while(std::getline(in,l)){std::istringstream row(l);std::string a,b,c,d,e;row>>a>>b>>c>>d>>e;if(e==LIB)return true;}return false;}
static int libraryHelper(bool bind){
    if(geteuid()!=0||linkname("/proc/self/ns/mnt")!=linkname("/proc/1/ns/mnt"))return 1;
    if(bind){
        if(mounted()||!digest(LIB,STOCK)||!digest(PAYLOAD,PATCH))return 2;
        if(mount(PAYLOAD,LIB,nullptr,MS_BIND,nullptr))return 3;
        if(mount(nullptr,LIB,nullptr,MS_BIND|MS_REMOUNT|MS_RDONLY,nullptr)){umount2(LIB,MNT_DETACH);return 4;}
        return sameFile(PAYLOAD,LIB)?0:5;
    }
    if(mounted()){
        if(!sameFile(PAYLOAD,LIB))return 6; // Never unmount an unrelated replacement.
        if(umount2(LIB,0)&&umount2(LIB,MNT_DETACH))return 7;
    }
    return !mounted()&&digest(LIB,STOCK)?0:8;
}
static bool patchedMapping(int pid){
    struct stat st{};if(stat(PAYLOAD,&st))return false;
    std::istringstream in(readfile("/proc/"+std::to_string(pid)+"/maps"));std::string l;
    while(std::getline(in,l)){
        unsigned ma=0,mi=0;unsigned long long ino=0;char perms[8]{};
        if(sscanf(l.c_str(),"%*s %7s %*s %x:%x %llu",perms,&ma,&mi,&ino)==4 && strchr(perms,'x') && ino==st.st_ino && ma==major(st.st_dev)&&mi==minor(st.st_dev))return true;
    }
    return false;
}
static int sfpid(){auto r=command({"/system/bin/pidof","surfaceflinger"});return r.code?0:atoi(r.text.c_str());}
static std::string mappingDetails(int pid){
    struct stat st{};int rc=stat(PAYLOAD,&st);int error=errno;
    std::ostringstream out;out<<"pid="<<pid<<" stat_rc="<<rc<<" errno="<<error<<" device="<<major(st.st_dev)<<":"<<minor(st.st_dev)<<" inode="<<st.st_ino<<" namespace="<<linkname("/proc/"+std::to_string(pid)+"/ns/mnt")<<"\n";
    auto maps=readfile("/proc/"+std::to_string(pid)+"/maps");out<<"maps_bytes="<<maps.size()<<"\n";
    std::istringstream rows(maps);std::string row;
    while(std::getline(rows,row))if(row.find("libvulkan_compositor")!=std::string::npos)out<<row<<"\n";
    out<<"matched="<<patchedMapping(pid)<<"\n";return out.str();
}
static int mappingFixture(){
    int f=open(PAYLOAD,O_RDONLY|O_CLOEXEC);if(f<0)return 1;
    void* p=mmap(nullptr,4096,PROT_READ|PROT_EXEC,MAP_PRIVATE,f,0);close(f);
    if(p==MAP_FAILED){perror("mmap fixture");return 1;}
    bool found=patchedMapping(getpid());printf("%s",mappingDetails(getpid()).c_str());
    munmap(p,4096);printf("MAPPING FIXTURE ONLY detected=%d; no code executed or device state changed\n",found);return found?0:1;
}
static bool cameraShaders(const std::string& s,bool requireCamera=true){
    if(s.empty()||s.find("Shaders:")==std::string::npos)return false;
    auto end=s.find("\nCapture");auto primary=s.substr(0,end);
    std::istringstream in(primary);std::string l;bool found=false;
    while(std::getline(in,l))if(l.find("Shaders:")!=std::string::npos&&l.find("passthrough")!=std::string::npos){
        if(l.find("passthrough_uber_uber_constants_frag")==std::string::npos && l.find("passthrough_uber_uber_uniforms_frag")==std::string::npos)return false;
        if(l.find("SRGB_INVERSE_EOTF")==std::string::npos)return false;
        found=true;
    }
    return found||!requireCamera;
}
static void save(const std::string& name,const std::string& s) {
    std::string p=std::string(DIR)+"/"+name;
    int f=open(p.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
    if(f<0) throw std::runtime_error("cannot save volatile status");
    size_t pos=0;while(pos<s.size()){ssize_t n=write(f,s.data()+pos,s.size()-pos);if(n<=0){close(f);throw std::runtime_error("status write failed");}pos+=size_t(n);}close(f);
}
static std::string identity(int pid) {
    auto s=readfile("/proc/"+std::to_string(pid)+"/stat");auto end=s.rfind(')');
    if(end==std::string::npos)return "";
    std::istringstream fields(s.substr(end+2));std::string v;
    for(int i=0;i<=19;i++)if(!(fields>>v))return "";
    return std::to_string(pid)+":"+v;
}
static bool formats(const std::string& text,int fmt,bool hwc) {
    std::istringstream in(text);std::string line;unsigned count=0,h=0;
    while(std::getline(in,line)) {
        if(line.find("+ name:Display framebuffer,")!=std::string::npos) {
            ++count;if(line.find("req fmt:"+std::to_string(fmt)+",")==std::string::npos)return false;
        }
        if(line.rfind("layer:",0)==0 && line.find("format:")!=std::string::npos) {
            ++h;
            if(fmt==43 && line.find("1010102")==std::string::npos && line.find("101010_2")==std::string::npos && line.find("RGBA_10_10_10_2")==std::string::npos)return false;
            if(fmt==1 && line.find("RGBA_8888")==std::string::npos)return false;
        }
    }
    return count>=2 && (!hwc || h>0);
}
struct Backend {
    bool mock=false,one=false,running=true,prop=false,touched=false;
    bool continuous=false,coordinated=false;int watchdogFd=-1;
    int bits=1,starts=0,externalStarts=0;uint64_t time=100;std::string scenario;
    uint64_t now(){return mock?time:real_ms();}
    void pause(unsigned ms){if(mock)time+=ms;else usleep(ms*1000);}
    void report(const std::string& phase,const std::string& why){
        std::string s=phase+" - "+why+"\n";printf("%llu %s",(unsigned long long)now(),s.c_str());fflush(stdout);
        if(!mock)try{save("status",s);}catch(...){fprintf(stderr,"Status write failed; recovery must continue.\n");}
    }
    std::string get(const char* k){
        if(mock) {if(!strcmp(k,KEY))return prop?"true":"false";if(!strcmp(k,"init.svc.surfaceflinger"))return running?"running":"stopped";return "";}
        char v[PROP_VALUE_MAX]{};__system_property_get(k,v);return v;
    }
    bool set(const char* k,const char* v){
        printf("COMMAND %s %s\n",k,v);fflush(stdout);
        if(mock){
            if(scenario=="oneshot-denied"&&!strcmp(k,"ctl.oneshot_on"))return false;
            if(scenario=="clear-denied"&&!strcmp(k,KEY)&&!strcmp(v,"false")&&touched)return false;
            if(!strcmp(k,"ctl.oneshot_on"))one=true;
            else if(!strcmp(k,"ctl.oneshot_off")){if(prop)throw std::runtime_error("UNSAFE unguard with true property");one=false;}
            else if(!strcmp(k,KEY)){prop=!strcmp(v,"true");if(prop){if(!one||running)throw std::runtime_error("UNSAFE activation");touched=true;}}
            else if(!strcmp(k,"ctl.stop"))running=false;
            else if(!strcmp(k,"ctl.start")){++starts;if(prop){if(starts>1)throw std::runtime_error("UNSAFE duplicate trial");bits=43;running=scenario!="startup-crash";}else{bits=1;running=true;}}
            return true;
        }
        return command({"/system/bin/setprop",k,v}).code==0;
    }
    std::string dump(){
        if(mock){
            if(!running||scenario=="dump-timeout")return "";
            int f=scenario=="output-stays8"?1:bits;
            return "+ name:Display framebuffer, req fmt:"+std::to_string(f)+",\n+ name:Display framebuffer, req fmt:"+std::to_string(f)+",\nlayer: format: "+(f==43?"RGBA_1010102_UBWC":"RGBA_8888_UBWC")+"\n";
        }
        auto r=command({"/system/bin/dumpsys","-t","3","SurfaceFlinger"},4000);return r.code==0?r.text:"";
    }
    bool stopped(){return get("init.svc.surfaceflinger")=="stopped";}
    bool waitStopped(){uint64_t end=now()+5000;while(now()<end){if(stopped())return true;pause(100);}return false;}
    bool waitRunning(){uint64_t end=now()+5000;while(now()<end){if(get("init.svc.surfaceflinger")=="running")return true;pause(100);}return false;}
    bool off(){return get(KEY)=="false" || get(KEY)=="0" || get(KEY).empty();}
    bool softRestartRequested(){return mock?(scenario=="soft-restart"||scenario=="soft-restart-failed"||scenario=="coordinated-handoff")&&time>=20000:
        access((std::string(DIR)+"/soft-restart").c_str(),F_OK)==0;}
    // Only an explicit stop (action.sh off, uninstall) or a signal ends a running
    // session at once. The KernelSU checkbox applies at the next soft reboot.
    bool requestStop(){return interrupted || (mock&&continuous&&time>=300000) || (!mock &&
        access((std::string(DIR)+"/stop").c_str(),F_OK)==0);}
    bool preferenceOff(){return mock?scenario=="planned-disable"&&time>=20000:
        access((std::string(MODULE)+"/disable").c_str(),F_OK)==0||access((std::string(MODULE)+"/remove").c_str(),F_OK)==0;}
    bool pulse(char c){return watchdogFd<0||write(watchdogFd,&c,1)==1;}
    bool disableModule(){
        if(mock)return true;
        int f=open((std::string(MODULE)+"/disable").c_str(),O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
        if(f<0)return false;
        bool ok=fsync(f)==0;close(f);
        int d=open(MODULE,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(d<0)return false;
        ok=fsync(d)==0&&ok;close(d);return ok;
    }
    bool bindLibrary(){if(mock)return scenario!="bind-denied";return command({"/proc/self/exe","--bind-library"}).code==0&&mounted()&&sameFile(LIB,PAYLOAD);}
    bool restoreLibrary(){if(mock)return scenario!="unmount-denied";return command({"/proc/self/exe","--unmount-library"}).code==0&&!mounted()&&digest(LIB,STOCK);}
    std::string renderer(){if(mock)return scenario=="bad-shader"?"unsupported":"Shaders: passthrough_uber_uber_constants_frag(SRGB_INVERSE_EOTF)";auto r=command({"/system/bin/dumpsys","-t","3","cpm","--renderer"},4000);return r.code==0?r.text:"";}
    bool restartDependents(){
        // Exact onrestart actions from the hash-pinned cpm.rc. ctl.stop/start
        // deliberately skips those actions, so execute them once explicitly.
        return set("ctl.restart","zygote")&&set("ctl.restart","vendor.perceptionhost");
    }
    bool frameworkReady(){
        if(mock)return scenario!="framework-stuck";
        auto r=command({"/system/bin/pidof","system_server"});
        return get("init.svc.bootanim")=="stopped"&&r.code==0&&atoi(r.text.c_str())>1;
    }
};
static bool recover(Backend& b){
    b.pulse('R'); // Independent guard allows the bounded stock/UI recovery.
    if(!b.disableModule())b.report("DISABLED","ERROR: cannot persist disable preference; remove module before any startup");
    b.report("DISABLED","rolling back; buffer restoration not yet verified");
    bool cleared=b.set(KEY,"false")&&b.off();
    bool library=b.restoreLibrary();
    if(!cleared||!library){b.report("DISABLED","ERROR: selector/library restoration failed; oneshot remains; power-cycle required");return false;}
    if(b.coordinated&&b.stopped()){
        if(!b.set("ctl.oneshot_off","surfaceflinger"))return false;
        b.report("DISABLED","startup substitutions restored while stopped; KernelSU owns baseline start; output not yet verified");
        return true;
    }
    // No future trial write can occur: this supervisor is the only writer.
    bool stopped=b.set("ctl.stop","surfaceflinger")&&b.waitStopped();
    bool restored=stopped&&b.set("ctl.oneshot_off","surfaceflinger");
    if(!stopped||!restored){b.report("DISABLED","ERROR: service flags/stop unconfirmed; power-cycle required");return false;}
    if(!b.set("ctl.start","surfaceflinger")){b.report("DISABLED","ERROR: baseline start failed; power-cycle required");return false;}
    if(!b.restartDependents()){b.report("DISABLED","ERROR: baseline dependency restart failed; power-cycle required");return false;}
    uint64_t end=b.now()+60000;
    do {auto d=b.dump();if(formats(d,1,true)&& (b.mock||!patchedMapping(sfpid()))&&b.frameworkReady()){
        if(!b.mock){save("baseline-after.txt",d);save("renderer-after.txt",b.renderer());}
        b.report("DISABLED","original library, 8-bit framebuffer/HWC and framework verified; boot animation stopped; normal service restart restored");return true;
    }b.pause(500);}while(b.now()<end);
    b.report("DISABLED","ERROR: output/framework restoration not fully verified; power-cycle required");return false;
}
// Checkbox OFF took effect at a KernelSU soft reboot: KernelSU itself stops and
// restarts Android, and the selector and library bind are already gone after
// startup. Clear the remaining oneshot guard; never restart anything here.
static bool plannedStop(Backend& b){
    b.pulse('R');
    bool ok=b.set(KEY,"false")&&b.off()&&b.restoreLibrary()&&b.set("ctl.oneshot_off","surfaceflinger");
    b.report("DISABLED",ok?"checkbox OFF applied at KernelSU soft reboot; original library and service flags restored; Android restarts with stock 8-bit output"
                          :"ERROR: soft-reboot restoration incomplete; power-cycle required");
    return ok;
}
static bool handoffRestart(Backend& b,const std::string& previous){
    // KernelSU owns stop/start for an intentional soft reboot. Do not race it
    // with another service restart, and preserve the checked preference.
    if(!b.pulse('R'))return false;
    b.report("DISABLED","Waiting: KernelSU soft restart; preserving enabled preference");
    if(!b.set(KEY,"false")||!b.off()||!b.restoreLibrary()||!b.set("ctl.oneshot_off","surfaceflinger"))return false;
    bool coordinated=b.mock?b.scenario=="coordinated-handoff":access((std::string(DIR)+"/coordinated-restart").c_str(),F_OK)==0;
    uint64_t end=b.now()+70000;
    if(b.mock){b.bits=1;b.running=!coordinated;}
    while(b.now()<end&&!b.requestStop()){
        if(coordinated&&b.stopped()){
            b.report("DISABLED","KernelSU stopped compositor; original startup state restored; ready for coordinated replacement");
            return true;
        }
        int pid=b.mock?124:sfpid();
        bool changed=b.mock?b.scenario!="soft-restart-failed":pid>1&&identity(pid)!=previous;
        auto d=b.dump();
        if(changed&&formats(d,1,true)&&(b.mock||!patchedMapping(pid))&&b.frameworkReady()){
            if(!b.mock)save("baseline-after.txt",d);
            b.report("DISABLED","KernelSU soft restart restored original 8-bit output; ready for next service stage");return true;
        }
        b.pause(250);
    }
    return false;
}
static int trial(Backend& b,unsigned seconds){
    bool modified=false,ready=false,completed=false,planned=false;uint64_t hard=b.now()+90000+seconds*1000;std::string activeIdentity;int activePid=0;
    try {
        b.report("DISABLED","arming temporary oneshot service guard");
        // Set modified before call: a timeout may occur after a command applies.
        modified=true;
        if(!b.set("ctl.oneshot_on","surfaceflinger"))throw std::runtime_error("oneshot guard command failed");
        if(b.requestStop())throw std::runtime_error("cancelled before stop");
        if(b.coordinated){if(!b.stopped())throw std::runtime_error("KernelSU has not stopped compositor");}
        else if(!b.set("ctl.stop","surfaceflinger")||!b.waitStopped())throw std::runtime_error("compositor stop failed");
        if(!b.bindLibrary())throw std::runtime_error("temporary library bind failed");
        if(!b.mock)save("mountinfo-bound.txt",readfile("/proc/self/mountinfo"));
        if(b.requestStop())throw std::runtime_error("cancelled before property");
        if(!b.set(KEY,"true")||b.get(KEY)!="true")throw std::runtime_error("10-bit property set/readback failed");
        if(b.coordinated){
            if(!b.mock)save("start-ready",readfile("/dev/ksu-restart-stage/generation"));
            uint64_t startDeadline=b.now()+60000;
            while(b.stopped()&&b.now()<startDeadline&&!b.requestStop()){
                if(!b.pulse('H'))throw std::runtime_error("guardian lost while waiting for KernelSU start");
                b.pause(100);
                if(b.mock&&b.scenario!="coordinated-start-timeout"&&b.now()>=1100){
                    b.running=true;b.bits=b.scenario=="output-stays8"?1:43;++b.externalStarts;
                    puts("EXTERNAL KernelSU start (simulation only)");
                }
            }
            if(b.requestStop()||b.stopped())throw std::runtime_error("KernelSU start cancelled or timed out");
        }else if(!b.set("ctl.start","surfaceflinger"))throw std::runtime_error("single startup failed");
        if(!b.waitRunning())throw std::runtime_error("single startup did not reach running state");
        uint64_t startup=b.now()+60000;bool dependentsRestarted=false;
        while(b.now()<startup&&!b.requestStop()){
            if(b.stopped())throw std::runtime_error("compositor exited; no repeat trial");
            auto d=b.dump();
            if(formats(d,43,true)){
                if(!b.mock)save("active10.txt",d);
                int pid=b.mock?123:sfpid();
                if(!b.mock){save("mapping-initial.txt",mappingDetails(pid));save("maps-initial.txt",readfile("/proc/"+std::to_string(pid)+"/maps"));}
                if(!b.mock&&!patchedMapping(pid))throw std::runtime_error("patched library mapping not found");
                if(!b.set(KEY,"false")||!b.off())throw std::runtime_error("post-start property clear failed");
                if(!b.restoreLibrary())throw std::runtime_error("could not remove startup-only library bind");
                if(!dependentsRestarted){if(!b.coordinated&&!b.restartDependents())throw std::runtime_error("XR dependency restart failed");dependentsRestarted=true;}
                auto r=b.renderer();
                if(!cameraShaders(r,!b.continuous)||!b.frameworkReady()){if(!b.mock)save("renderer-waiting.txt",r);b.pause(500);continue;}
                if(!b.mock){save("renderer-active.txt",r);save("maps-active.txt",readfile("/proc/"+std::to_string(pid)+"/maps"));save("active-pid",std::to_string(pid));activePid=pid;activeIdentity=identity(pid);save("active-identity",activeIdentity);}
                ready=true;b.report("ENABLED",b.continuous?"color patch mapping, 10-bit framebuffer/HWC and framework verified; active until disabled; SDR output":"patched camera shaders, 10-bit framebuffer AND HWC verified; visual window 120 seconds");break;
            }
            b.pause(250);
        }
        if(!ready)throw std::runtime_error("10-bit output not verified before startup deadline");
        uint64_t end=b.continuous?UINT64_MAX:std::min(hard,b.now()+seconds*1000);
        if(!b.mock)save("deadline-boottime-ms",b.continuous?"none; disable to restore\n":std::to_string(end)+"\n");
        unsigned checks=0;
        while(b.now()<end&&!b.requestStop()){
            if(!b.pulse('H'))throw std::runtime_error("independent guardian channel lost");
            if(b.softRestartRequested()){
                if(handoffRestart(b,activeIdentity))return 4;
                throw std::runtime_error("KernelSU restart handoff failed; no automatic retry");
            }
            if(b.mock&&b.scenario=="planned-disable"&&b.time>=20000)b.running=false; // KernelSU stop (simulation)
            if(b.get("init.svc.surfaceflinger")!="running"&&b.preferenceOff()&&!b.softRestartRequested()){planned=true;break;}
            if((b.get("init.svc.surfaceflinger")!="running"||!b.off())&&!b.softRestartRequested())throw std::runtime_error("service exit or external property change");
            // Lightweight steady state: no fork and no dumpsys while running. Full
            // dumpsys holds compositor locks and can cause periodic hitches. Buffers,
            // format and shaders cannot change without a new compositor process.
            if(!b.mock&&++checks%30==0&&identity(activePid)!=activeIdentity&&!b.softRestartRequested())throw std::runtime_error("compositor identity changed");
            if(!b.mock&&checks%100==0&&!patchedMapping(activePid)&&!b.softRestartRequested())throw std::runtime_error("compositor library mapping changed");
            if(b.mock&&b.scenario=="runtime-crash")b.running=false;
            b.pause(100);
        }
        if(planned)return plannedStop(b)?0:3;
        completed=b.continuous||!b.requestStop();
    } catch(const std::exception& e){b.report("DISABLED",std::string("test ended: ")+e.what());}
    bool ok=!modified||recover(b);
    return ok?(ready&&completed?0:2):3;
}
static bool preflight(Backend& b,bool stopped=false){
    if(geteuid()!=0){puts("DISABLED - root required");return false;}
    struct statfs fs{};if(statfs("/dev",&fs)||fs.f_type!=0x01021994){puts("DISABLED - /dev is not tmpfs");return false;}
    if(linkname("/proc/self/ns/mnt").empty()||linkname("/proc/self/ns/mnt")!=linkname("/proc/1/ns/mnt")){puts("DISABLED - global mount namespace required");return false;}
    if(mounted()||!digest(LIB,STOCK)||!digest(PAYLOAD,PATCH)){puts("DISABLED - library source/baseline mismatch or pre-existing mount");return false;}
    struct stat st{};if(lstat(PAYLOAD,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0222)){puts("DISABLED - payload must be root-owned read-only regular file");return false;}
    char label[256]{};ssize_t labelSize=getxattr(PAYLOAD,"security.selinux",label,sizeof(label)-1);
    if(labelSize<0||strcmp(label,"u:object_r:system_lib_file:s0")){puts("DISABLED - replacement library SELinux label mismatch");return false;}
    const char* files[]={"/system/bin/init","/system/bin/surfaceflinger","/system/etc/init/cpm.rc"};
    const char* hashes[]={"8981344592efc845e2f68d43fcbbd8083f5b888d4937924bb45b1ce38ccb7cac","21c9cfe2da316536439871a3e95ca68a74ba5dcab45abc92a29821f62275158d","56539f0c2ef236476fbf461597503da433bdf00ab2e9c3c31a2a5c5d6fe045fe"};
    for(int i=0;i<3;i++){auto r=command({"/system/bin/sha256sum",files[i]});if(r.code||r.text.rfind(hashes[i],0)!=0){puts("DISABLED - pinned file mismatch");return false;}}
    if(stopped){
        if(!b.off()||!b.stopped()||sfpid()!=0){puts("DISABLED - expected stopped compositor and cleared selector");return false;}
        puts("PREFLIGHT OK - pinned originals, global namespace, stopped compositor; KernelSU owns next start");return true;
    }
    if(!b.off()||b.get("init.svc.surfaceflinger")!="running"){puts("DISABLED - nonbaseline selector/service");return false;}
    int pid=sfpid();if(pid<=1||linkname("/proc/"+std::to_string(pid)+"/ns/mnt")!=linkname("/proc/self/ns/mnt")){puts("DISABLED - compositor mount namespace mismatch");return false;}
    if(!formats(b.dump(),1,true)){puts("DISABLED - 8-bit framebuffer/HWC baseline not confirmed");return false;}
    puts("PREFLIGHT OK - pinned original/patched library, global namespace, volatile storage, 8-bit framebuffer/HWC; no mutation");return true;
}
// A different session/process group survives the test supervisor's death.
// No activation code runs here: it can only remove the temporary change.
static int backupWatchdog(int input,pid_t owner,const std::string& expected,uint64_t deadline,bool mock){
    bool timeout=false;
    bool recovery=false;
    for(;;){
        uint64_t now=real_ms();if(now>=deadline){timeout=true;break;}
        pollfd p{input,POLLIN|POLLHUP,0};int r=poll(&p,1,int(std::min<uint64_t>(deadline-now,1000)));
        if(r<0&&errno==EINTR)continue;
        if(r<0)break;
        if(r>0){char c=0;ssize_t n=read(input,&c,1);if(n==1&&c=='D'){close(input);puts("BACKUP: verified normal recovery acknowledged");return 0;}
            if(n==1&&c=='H'&&!recovery){deadline=real_ms()+(mock?100:30000);continue;}
            if(n==1&&c=='R'&&!recovery){recovery=true;deadline=real_ms()+(mock?500:90000);continue;}
            if(n<=0)break;}
    }
    close(input);
    printf("BACKUP: %s; stopping sole activation writer before recovery\n",timeout?"hard deadline expired":"supervisor exited without recovery acknowledgment");
    auto current=identity(owner);
    if(!current.empty()&&current!=expected){puts("BACKUP ERROR: owner PID reused; refusing unrelated process group");return 3;}
    // Group was made with setsid before this backup fork. Our own group differs.
    if(owner<=1||owner==getpgrp()){puts("BACKUP ERROR: invalid isolated process group");return 3;}
    kill(-owner,SIGKILL);
    Backend b;b.mock=mock;b.scenario="normal";
    return recover(b)?0:3;
}
static int watchdogFixture(const std::string& mode){
    if(mode!="ack"&&mode!="death"&&mode!="timeout"&&mode!="heartbeat"&&mode!="recovery")return 64;
    int control[2],ready[2];if(pipe2(control,O_CLOEXEC)||pipe2(ready,O_CLOEXEC))return 1;
    pid_t owner=fork();if(owner<0)return 1;
    if(!owner){
        close(control[0]);close(ready[0]);if(setsid()<0)_exit(1);
        char c='A';if(write(ready[1],&c,1)!=1)_exit(1);close(ready[1]);
        if(mode=="ack"){c='D';_exit(write(control[1],&c,1)==1?0:1);}
        if(mode=="heartbeat"){for(int i=0;i<6;i++){c='H';if(write(control[1],&c,1)!=1)_exit(1);usleep(60000);}c='D';_exit(write(control[1],&c,1)==1?0:1);}
        if(mode=="recovery"){c='R';if(write(control[1],&c,1)!=1)_exit(1);usleep(300000);c='D';_exit(write(control[1],&c,1)==1?0:1);}
        if(mode=="death")_exit(7);
        for(;;)pause();
    }
    close(control[1]);close(ready[1]);char c=0;
    if(read(ready[0],&c,1)!=1)return 1;close(ready[0]);
    auto expected=identity(owner);
    int rc=backupWatchdog(control[0],owner,expected,real_ms()+200,true);
    int status=0;while(waitpid(owner,&status,0)<0&&errno==EINTR){}
    printf("WATCHDOG FIXTURE ONLY mode=%s rc=%d child_status=%d\n",mode.c_str(),rc,status);return rc;
}
int main(int argc,char**argv){
    setvbuf(stdout,nullptr,_IONBF,0);signal(SIGHUP,SIG_IGN);signal(SIGPIPE,SIG_IGN);
    signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);
    Backend b;
#ifndef __ANDROID__
    if(argc<2||(strcmp(argv[1],"--simulate")&&strcmp(argv[1],"--simulate-session")&&strcmp(argv[1],"--simulate-coordinated")&&strcmp(argv[1],"--watchdog-fixture")&&strcmp(argv[1],"--apply-patch"))){puts("HOST TEST BUILD - mock modes only");return 64;}
#endif
    if(argc==4&&!strcmp(argv[1],"--apply-patch"))return applyFile(argv[2],argv[3]);
    if(argc==2&&!strcmp(argv[1],"--build-payload"))return buildPayload();
    if(argc==2&&!strcmp(argv[1],"--mapping-fixture"))return mappingFixture();
    if(argc==2&&!strcmp(argv[1],"--inspect-mapping")){printf("%s",mappingDetails(sfpid()).c_str());return 0;}
    if(argc==3&&!strcmp(argv[1],"--watchdog-fixture"))return watchdogFixture(argv[2]);
    if(argc==2&&!strcmp(argv[1],"--bind-library"))return libraryHelper(true);
    if(argc==2&&!strcmp(argv[1],"--unmount-library"))return libraryHelper(false);
    if(argc==3&&(!strcmp(argv[1],"--simulate")||!strcmp(argv[1],"--simulate-session")||!strcmp(argv[1],"--simulate-coordinated"))){
        b.mock=true;b.continuous=!strcmp(argv[1],"--simulate-session");b.scenario=argv[2];
        b.coordinated=!strcmp(argv[1],"--simulate-coordinated");
        if(b.coordinated){b.continuous=true;b.running=false;}
        const std::vector<std::string> allowed={"normal","oneshot-denied","startup-crash","clear-denied","output-stays8","dump-timeout","runtime-crash","bind-denied","unmount-denied","bad-shader","framework-stuck","soft-restart","soft-restart-failed","coordinated-start-timeout","coordinated-handoff","planned-disable"};
        if(std::find(allowed.begin(),allowed.end(),b.scenario)==allowed.end())return 64;
        int rc=trial(b,120);printf("SIMULATION ONLY rc=%d starts=%d selector=%d oneshot=%d external_starts=%d\n",rc,b.starts,b.prop,b.one,b.externalStarts);return rc;
    }
    if(argc==2&&!strcmp(argv[1],"--preflight"))return preflight(b)?0:1;
    if(argc==2&&!strcmp(argv[1],"--preflight-stopped"))return preflight(b,true)?0:1;
    if(argc==2&&(!strcmp(argv[1],"--prepare-restart")||!strcmp(argv[1],"--prepare-coordinated-restart"))){
        if(geteuid()!=0)return 1;
        auto s=readfile(std::string(DIR)+"/status");
        if(s.rfind("ENABLED",0)!=0){puts("DISABLED - no active session to hand off");return 0;}
        int pid=atoi(readfile(std::string(DIR)+"/supervisor-pid").c_str());
        if(pid<=1||identity(pid)!=readfile(std::string(DIR)+"/supervisor-identity"))return 1;
        try{
            if(!strcmp(argv[1],"--prepare-coordinated-restart"))save("coordinated-restart",readfile("/dev/ksu-restart-stage/generation"));
            save("soft-restart","KernelSU emulated-soft-reboot stage\n");
        }catch(...){return 1;}
        puts("DISABLED - Waiting: soft restart requested; guardian will hand off to KernelSU");return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--status")){
        auto s=readfile(std::string(DIR)+"/status");
        if(b.softRestartRequested()&&access((std::string(DIR)+"/clean-completion").c_str(),F_OK)!=0){puts("DISABLED - Waiting: KernelSU soft restart handoff; startup will wait for verified completion");return 0;}
        if(s.rfind("ENABLED",0)==0){
            int pid=atoi(readfile(std::string(DIR)+"/supervisor-pid").c_str());
            auto expected=readfile(std::string(DIR)+"/supervisor-identity");
            if(pid<=1||expected.empty()||identity(pid)!=expected){puts("DISABLED - ERROR: supervisor identity lost; last output may remain 10-bit; verify recovery or power-cycle");return 3;}
            int active=atoi(readfile(std::string(DIR)+"/active-pid").c_str());
            if(identity(active)!=readfile(std::string(DIR)+"/active-identity")||!b.off()||!formats(b.dump(),43,true)||!patchedMapping(active)||!digest(LIB,STOCK)||mounted()){puts("DISABLED - ERROR: live 10-bit/library state no longer verified; inspect recovery");return 3;}
        }
        printf("%s",s.empty()?"DISABLED - no current-boot session\n":s.c_str());return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--stop")){if(geteuid()!=0)return 1;try{save("stop","requested\n");}catch(...){return 1;}puts("Stop requested; verify status for actual restoration.");return 0;}
    b.coordinated=argc==2&&!strcmp(argv[1],"--run-coordinated-session");
    b.continuous=b.coordinated||(argc==2&&!strcmp(argv[1],"--run-session"));
    if(!b.continuous&&(argc!=3||strcmp(argv[1],"--run")||strcmp(argv[2],"120"))){puts("Usage: --preflight | --status | --stop | --run-session | --run 120 | --simulate SCENARIO");return 64;}
    if(b.coordinated&&access("/dev/ksu-restart-stage/busy",F_OK)!=0){puts("DISABLED - KernelSU restart transaction required");return 1;}
    if(!preflight(b,b.coordinated))return 1;
    // Serialize lifetimes, not boots. Only verified clean sessions are reusable.
    int sessionLock=open("/dev/gxr-framebuffer10.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(sessionLock<0||flock(sessionLock,LOCK_EX|LOCK_NB)){puts("DISABLED - another session owns the compositor transaction");return 1;}
    struct stat prior{};
    if(!lstat(DIR,&prior)){
        if(!S_ISDIR(prior.st_mode)||readfile(std::string(DIR)+"/clean-completion")!="verified\n"){
            puts("DISABLED - previous session incomplete or failed; retry blocked");return 1;
        }
        auto archive=std::string(DIR)+".completed-"+std::to_string(real_ms());
        if(rename(DIR,archive.c_str())){puts("DISABLED - cannot archive completed session");return 1;}
    }
    if(b.requestStop()){puts("DISABLED - module preference is off or stop requested");return 1;}
    if(mkdir(DIR,0700)){puts("DISABLED - cannot create session state");return 1;}
    try{save("status","DISABLED - initializing independent supervisor\n");save("boot-id",readfile("/proc/sys/kernel/random/boot_id"));}catch(...){return 1;}
    pid_t guardian=fork();if(guardian<0)return 1;
    if(guardian){printf("SUPERVISOR pid=%d; actual status is in %s/status\n",guardian,DIR);int st;while(waitpid(guardian,&st,0)<0){if(errno!=EINTR)return 1;}return WIFEXITED(st)?WEXITSTATUS(st):3;}
    if(setsid()<0)_exit(1);
    int log=open((std::string(DIR)+"/events.log").c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(log<0)_exit(1);
    dup2(log,1);dup2(log,2);close(log);
    try{
        pid_t owner=getpid();auto expected=identity(owner);
        save("supervisor-pid",std::to_string(owner)+"\n");save("supervisor-identity",expected);
        int control[2],armed[2];if(pipe2(control,O_CLOEXEC)||pipe2(armed,O_CLOEXEC))_exit(1);
        uint64_t backupDeadline=real_ms()+90000;
        pid_t backup=fork();if(backup<0)_exit(1);
        if(!backup){
            close(control[1]);close(armed[0]);if(setsid()<0)_exit(1);
            int f=open((std::string(DIR)+"/backup.log").c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(f<0)_exit(1);
            dup2(f,1);dup2(f,2);close(f);
            save("backup-pid",std::to_string(getpid()));save("backup-deadline-boottime-ms",std::to_string(backupDeadline));
            char c='A';if(write(armed[1],&c,1)!=1)_exit(1);close(armed[1]);
            _exit(backupWatchdog(control[0],owner,expected,backupDeadline,false));
        }
        close(control[0]);close(armed[1]);pollfd p{armed[0],POLLIN,0};char c=0;
        if(poll(&p,1,4000)<=0||read(armed[0],&c,1)!=1||c!='A')_exit(1);
        close(armed[0]);
        fcntl(control[1],F_SETFL,O_NONBLOCK);b.watchdogFd=control[1];
        int rc=trial(b,120);if(rc!=3&&!b.pulse('D'))rc=3;close(control[1]);
        if(rc==3)_exit(3); // Backup gets EOF and performs one recovery attempt.
        int status=0;while(waitpid(backup,&status,0)<0&&errno==EINTR){}
        if((rc==0||rc==4)&&WIFEXITED(status)&&WEXITSTATUS(status)==0)save("clean-completion","verified\n");
        else if(rc==0||rc==4)rc=3;
        _exit(rc);
    }
    catch(...){fprintf(stderr,"ERROR: supervisor exception; power-cycle fallback required\n");_exit(3);}
}
