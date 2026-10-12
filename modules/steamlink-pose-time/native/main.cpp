// Identity-bound app-only RAM module; KernelSU preference owns lifecycle.
#include "app_process.h"
#include "../../openxr-velocity-frame/native/compatibility.h"
#include "contract.h"
#include "generated.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <array>
#include <sys/file.h>
static constexpr char STATE[]="/dev/gxr_steamlink_pose_time/target";
static constexpr char ROOTSTATE[]="/dev/gxr_steamlink_pose_time";
static constexpr char MODULE[]="/data/adb/modules/gxr_steamlink_pose_time";
static constexpr char LEADFILE[]="/data/adb/modules/gxr_steamlink_pose_time/compensation.conf";
struct Waiting:std::runtime_error{using std::runtime_error::runtime_error;};
static constexpr char UNKNOWN_CODE[]="Velocity Frame consumer code unknown or changing; waiting";
static constexpr T UNKNOWN_LIMIT=30000000000LL; // same app process; longer means a foreign hook or incompatible Velocity Frame
#include "process_helpers.h"
static constexpr char PACKAGE[]="com.valvesoftware.steamlinkvr";
struct Target{pid_t pid;std::string start;uintptr_t runtime,scene,address;};
static bool equal(Memory& mem,uintptr_t a,const unsigned char* b,size_t n){return mem.bytes(a,n)==std::vector<unsigned char>(b,b+n);}
static uintptr_t scene(pid_t pid,Memory& mem){
 uintptr_t found=0;std::istringstream lines(read(proc(pid,"maps")));std::string line;
 while(std::getline(lines,line)){
  unsigned long lo,hi,off;char perm[5]{};int used=0;
  if(sscanf(line.c_str(),"%lx-%lx %4s %lx %*s %*s %n",&lo,&hi,perm,&off,&used)!=4||strcmp(perm,"r-xp"))continue;
  auto path=line.substr(used);if(path.find(PACKAGE)==std::string::npos&&path.find("libvrlink_scene.so")==std::string::npos)continue;
  if(hi-lo>32*1024*1024)continue;auto b=mem.bytes(lo,hi-lo);
  for(size_t i=0;i+32<b.size();i+=4)if(!memcmp(b.data()+i,STATE_CODE,32)){
   uintptr_t base=lo+i-0x1070cc;
   if(equal(mem,base+0xf4e74,SEND_CODE,sizeof(SEND_CODE))){require(!found,"Ambiguous scene");found=base;}
  }
 }
 if(!found)throw Waiting("Steam Link VR library not loaded; connect to SteamVR");return found;
}
static std::array<uintptr_t,3> sites(const Target& t){return {t.runtime+0x28b520,t.scene+0x21ba70,t.scene+0x21b130};}
static std::array<uintptr_t,3> originals(const Target& t){return {t.runtime+0x1b359c,t.scene+0x1070cc,t.scene+0x15a43c};}
static std::array<uintptr_t,3> replacements(const Target& t){return {t.address+CONSUME_ENTRY,t.address+STATE_ENTRY,t.address+SUBMIT_ENTRY};}
static Target current(){
 DIR* d=opendir("/proc");require(d,"proc unavailable");pid_t found=0;
 while(auto* e=readdir(d)){pid_t p=atoi(e->d_name);if(p<=1)continue;try{if(std::string(read(proc(p,"cmdline")).c_str())==PACKAGE){require(!found,"Multiple app processes");found=p;}}catch(const ProcessAccessError&){}catch(const ProcessGone&){} }
 closedir(d);if(!found)throw Waiting("Steam Link is not running");require(thirdParty(found,PACKAGE,thirdPartyApps()),"Not third-party app UID");
 return {found,starttime(found),0,0,0};
}
static void verify(Target& t,Memory& mem){
 verifyIdentity(t.pid,t.start);require(std::string(read(proc(t.pid,"cmdline")).c_str())==PACKAGE,"Package changed");
 if(read(proc(t.pid,"maps")).find("/system/lib64/libopenxr_android.so")==std::string::npos)throw Waiting("Steam Link OpenXR runtime not loaded");
 auto m=mapping(t.pid);require(hash(m.file)=="a7dabaa1030cc7979efe987f212488c61b61e8afb9aa3add59f7ae4fbeff4c92","Runtime hash changed");
 t.runtime=m.base;t.scene=scene(t.pid,mem);
 require(equal(mem,t.scene+0x1070cc,STATE_CODE,sizeof(STATE_CODE)),"Controller writer changed");
 require(equal(mem,t.scene+0x155bf4,CLOCK_CODE,sizeof(CLOCK_CODE)),"Vendor clock changed");
 // Velocity Frame writes its correction without this module's lock, so a partial write
 // can be observed here. Unknown code is a transient wait (bounded by the worker), and a
 // known state must hold across a second read before it counts.
 int mode=gxr_consumer_state(mem.bytes(t.runtime+0x1b359c,sizeof(GXR_CONSUMER)).data());
 if(mode==0)throw Waiting("Waiting for Velocity Frame activation");
 if(mode<0)throw Waiting(UNKNOWN_CODE);
 require(mode==1,"Need stock extrapolation gate; disable Controller Extrapolation");
 usleep(20000);
 if(gxr_consumer_state(mem.bytes(t.runtime+0x1b359c,sizeof(GXR_CONSUMER)).data())!=1)throw Waiting(UNKNOWN_CODE);
 require(gxr_payload_valid(mem.bytes(t.runtime+0x272ae0,0x520).data(),true),"Velocity Frame payload changed");
}
// Compensation preference: "LEAD_US=<0..30000>" in the module directory. Missing or
// invalid content means the default; the file is only ever written through "lead set".
static T leadSetting(){
 std::string text;try{text=read(LEADFILE);}catch(...){return DEFAULT_LEAD_NS;}
 const std::string key="LEAD_US=";auto pos=text.find(key);if(pos==std::string::npos)return DEFAULT_LEAD_NS;
 std::string digits;for(size_t i=pos+key.size();i<text.size()&&isdigit(static_cast<unsigned char>(text[i]));++i)digits+=text[i];
 if(digits.empty()||digits.size()>6)return DEFAULT_LEAD_NS;T us=atol(digits.c_str());
 return us*1000>MAX_LEAD_NS?DEFAULT_LEAD_NS:us*1000;
}
static void leadCommand(int argc,char** argv){
 const std::string op=argc>=3?argv[2]:"get";
 if(op=="set"){
  require(argc==4,"lead set <microseconds 0..30000>");std::string v=argv[3];
  require(!v.empty()&&v.size()<=5&&v.find_first_not_of("0123456789")==std::string::npos,"Compensation must be an integer 0..30000 microseconds");
  const long us=atol(v.c_str());require(us>=0&&us<=MAX_LEAD_NS/1000,"Compensation out of range 0..30000 microseconds");
  const std::string tmp=std::string(LEADFILE)+".tmp";
  putFile(tmp,"# Steam Link Pose Time compensation, microseconds subtracted from the slot label.\n# 0 = raw slot label; 28820 = measured original label lead (content epoch); limit 0..30000.\nLEAD_US="+std::to_string(us)+"\n");
  require(rename(tmp.c_str(),LEADFILE)==0,"Cannot store compensation");
 }else require(op=="get","lead get | lead set <microseconds>");
 const T lead=leadSetting();
 printf("LEAD_US=%lld\nLEAD_MS=%.2f\nDEFAULT_US=%lld\nORIGINAL_US=%lld\nMIN_US=0\nMAX_US=%lld\n",(long long)(lead/1000),lead/1e6,(long long)(DEFAULT_LEAD_NS/1000),(long long)(ORIGINAL_LEAD_NS/1000),(long long)(MAX_LEAD_NS/1000));
}
static Target saved(){Target t{};std::istringstream s(read(STATE));s>>t.pid>>t.start>>t.runtime>>t.scene>>t.address;require(t.pid>1&&decimalIdentity(t.start)&&t.address>=0x10000,"Bad recovery identity");return t;}
// Live compensation update. Writing the control block uses PTRACE_POKETEXT, which
// requires this worker to be the tracer, so the target is frozen first exactly as in
// apply(). Never fatal: a failed update leaves the current value live, is retried next
// cycle and is applied at the next activation regardless. Returns true only when the
// value is confirmed written into our own, still-enabled control block.
static bool writeLead(const Target& t,T want){
 if(want<0||want>MAX_LEAD_NS)return false;
 try{
  Frozen frozen;frozen.freeze(t.pid);verifyIdentity(t.pid,t.start);
  Memory mem(t.pid);Control c{};mem.get(t.address+DATA,&c,sizeof(c));
  if(c.magic!=MAGIC||c.enabled!=1)return false;
  mem.put(t.address+DATA+offsetof(Control,lead),&want,8);
  return true;
 }catch(const std::exception&){return false;}
}
static void status(Target t){
 verifyIdentity(t.pid,t.start);Memory mem(t.pid);Control c{};mem.get(t.address+DATA,&c,sizeof(c));require(c.magic==MAGIC,"Payload identity mismatch");
 bool hooked=true;auto a=sites(t),v=replacements(t);for(unsigned i=0;i<3;++i){uintptr_t x;mem.get(a[i],&x,8);hooked&=x==v[i];}
 printf("PID=%d start=%s hooks=%d enabled=%u lead_us=%lld packets=%llu scopes=%llu consume=%llu invalid=%llu conflicts=%llu faults=%llu\n",t.pid,t.start.c_str(),hooked,c.enabled,(long long)(c.lead/1000),(unsigned long long)c.packets,(unsigned long long)c.scopes,(unsigned long long)c.calls,(unsigned long long)c.invalid,(unsigned long long)c.conflicts,(unsigned long long)c.faults);
 for(unsigned i=0;i<2;++i){auto& h=c.hand[i];printf("hand=%u epoch=content samples=%u ready=%u anchor_ns=%lld matched=%llu unmatched=%llu relabelled=%llu applied=%llu changed=%u last_offset_us=%d source_ns=%lld\n",i,h.count,h.calibrated,(long long)h.anchor,(unsigned long long)h.matched,(unsigned long long)h.unmatched,(unsigned long long)h.ambiguous,(unsigned long long)h.applied,h.changed,h.lastOffset,(long long)h.lastStamp);}
}
static void restore(){
 OperationLock operation;Target t=saved();verifyIdentity(t.pid,t.start);Memory mem(t.pid);Frozen frozen;frozen.freeze(t.pid);verifyIdentity(t.pid,t.start);
 require(equal(mem,t.address,PAYLOAD,sizeof(PAYLOAD)),"Restore payload changed");uint64_t magic;mem.get(t.address+DATA,&magic,8);require(magic==MAGIC,"Restore control identity mismatch");
 U off=0;mem.put(t.address+DATA+offsetof(Control,enabled),&off,4);
 auto a=sites(t),o=originals(t),v=replacements(t);
 for(U i=0;i<3;++i){uintptr_t x;mem.get(a[i],&x,8);require(x==o[i]||x==v[i],"Foreign GOT during restore");}
 for(U i=0;i<3;++i)mem.put(a[i],&o[i],8);
 // Do not unmap: in-flight callbacks can return through the old allocation.
 puts("RESTORED all3 original GOT pointers; resident callbacks disabled. No reapplication.");
}
static Target apply(){
 OperationLock operation;Target t=current();Memory mem(t.pid);verify(t,mem);
 Frozen frozen;frozen.freeze(t.pid);verifyIdentity(t.pid,t.start);
 require(access((std::string(MODULE)+"/disable").c_str(),F_OK)!=0&&access((std::string(MODULE)+"/remove").c_str(),F_OK)!=0,"Module disabled during activation");
 auto a=sites(t),o=originals(t);for(U i=0;i<3;++i){uintptr_t x;mem.get(a[i],&x,8);require(x==o[i],"Already hooked or foreign GOT");}
 uintptr_t svc=syscallSite(t.pid,mem);t.address=remoteSyscall(t.pid,svc,222,{0,ALLOCATION,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,uint64_t(-1),0});
 require(t.address>=0x10000&&t.address<0x1000000000000ULL&&(t.address&0xfff)==0,"mmap refused");
 mem.put(t.address,PAYLOAD,sizeof(PAYLOAD));Control c{};c.magic=MAGIC;c.consume=o[0];c.state=o[1];c.submit=o[2];c.clock=t.scene+0x155bf4;c.lead=leadSetting();
 mem.put(t.address+DATA,&c,sizeof(c));require(remoteSyscall(t.pid,svc,226,{t.address,DATA,PROT_READ|PROT_EXEC,0,0,0})==0,"RX refused");
 putFile(STATE,std::to_string(t.pid)+" "+t.start+" "+std::to_string(t.runtime)+" "+std::to_string(t.scene)+" "+std::to_string(t.address)+"\n");
 auto v=replacements(t);
 try{for(U i=0;i<3;++i)mem.put(a[i],&v[i],8);U on=1;mem.put(t.address+DATA+offsetof(Control,enabled),&on,4);}
 catch(...){for(U i=0;i<3;++i)try{mem.put(a[i],&o[i],8);}catch(...){fprintf(stderr,"Restore failed; close Steam Link\n");}throw;}
 require(equal(mem,t.address,PAYLOAD,sizeof(PAYLOAD)),"Payload readback failed");
 puts("ENABLED - App-private hooks applied; wire epoch = slot label - compensation (default 15 ms; 28.82 ms = original label lead). Automatic reapplication follows KernelSU preference; no duration limit.");status(t);return t;
}
// Narrow native ABI/field-boundary check in this helper's own address space.
static Control* testControl;static uintptr_t testBase;static T testStamp;static unsigned char testPose[84];
static bool fakeConsume(void*,U,U,uint64_t* t,void* r){*t=testStamp;*static_cast<U*>(r)=0x77;return true;}
static bool fakeState(void*,void* p,int){uint64_t t=0;U flags=0;auto fn=reinterpret_cast<bool(*)(void*,U,U,uint64_t*,void*)>(testBase+CONSUME_ENTRY);require(fn(nullptr,3,0x130005,&t,&flags),"consume ABI");memcpy(static_cast<char*>(p)+0x20,testPose,84);return true;}
static U fakeClock(){return U(monotonic()/1000);}
static uintptr_t fakeSubmit(void*,void*,bool a,bool b){return a&&!b?0x12345678:0;}
static void selftest(){
 void* p=mmap(nullptr,ALLOCATION,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);require(p!=MAP_FAILED,"fixture mmap");testBase=reinterpret_cast<uintptr_t>(p);memcpy(p,PAYLOAD,sizeof(PAYLOAD));require(mprotect(p,DATA,PROT_READ|PROT_EXEC)==0,"fixture RX");
 testControl=reinterpret_cast<Control*>(testBase+DATA);auto& c=*testControl;c={};c.magic=MAGIC;c.enabled=1;c.consume=reinterpret_cast<uintptr_t>(&fakeConsume);c.state=reinterpret_cast<uintptr_t>(&fakeState);c.submit=reinterpret_cast<uintptr_t>(&fakeSubmit);c.clock=reinterpret_cast<uintptr_t>(&fakeClock);c.lead=DEFAULT_LEAD_NS;
 testPose[0x36]=2;unsigned char output[0x80]{},packet[0x170]{};testStamp=monotonic()+35000000;
 require(reinterpret_cast<bool(*)(void*,void*,int)>(testBase+STATE_ENTRY)(nullptr,output,1),"state ABI");require(c.hand[0].next==1&&c.hand[0].samples[0].stamp==testStamp,"source captured");
 *reinterpret_cast<U*>(packet)=0x103;*reinterpret_cast<uint16_t*>(packet+4)=0x168;packet[8]=10;*reinterpret_cast<U*>(packet+0x14)=fakeClock();memcpy(packet+0xc0,testPose,84);
 auto& h=c.hand[0];h.calibrated=1;h.anchor=5000000;c.hand[1].calibrated=1;
 unsigned char before[sizeof(packet)];memcpy(before,packet,sizeof(packet));
 require(reinterpret_cast<uintptr_t(*)(void*,void*,bool,bool)>(testBase+SUBMIT_ENTRY)(nullptr,packet,true,false)==0x12345678,"submit ABI");
 require(h.applied==1&&h.lastOffset>19000&&h.lastOffset<=20000,"default compensation: 35 ms label minus 15 ms");
 for(size_t i=0;i<sizeof(packet);++i)if(i<0x114||i>=0x118)require(packet[i]==before[i],"Only left time bytes may change");
 c.lead=ORIGINAL_LEAD_NS;memcpy(packet,before,sizeof(packet));reinterpret_cast<uintptr_t(*)(void*,void*,bool,bool)>(testBase+SUBMIT_ENTRY)(nullptr,packet,true,false);
 require(h.applied==2&&h.lastOffset>5000&&h.lastOffset<=6180,"original lead: 35 ms minus 28.82 ms");
 c.lead=0;memcpy(packet,before,sizeof(packet));reinterpret_cast<uintptr_t(*)(void*,void*,bool,bool)>(testBase+SUBMIT_ENTRY)(nullptr,packet,true,false);
 require(h.applied==3&&h.lastOffset>34000&&h.lastOffset<=35000,"raw label: 35 ms");
 c.lead=MAX_LEAD_NS+1;memcpy(packet,before,sizeof(packet));reinterpret_cast<uintptr_t(*)(void*,void*,bool,bool)>(testBase+SUBMIT_ENTRY)(nullptr,packet,true,false);
 require(h.applied==4&&h.lastOffset>19000&&h.lastOffset<=20000,"out-of-range compensation falls back to the default");
 c.enabled=0;memcpy(packet,before,sizeof(packet));reinterpret_cast<uintptr_t(*)(void*,void*,bool,bool)>(testBase+SUBMIT_ENTRY)(nullptr,packet,true,false);require(!memcmp(packet,before,sizeof(packet)),"disabled passthrough");
 munmap(p,ALLOCATION);puts("PASS native callback ABI, same-call source binding, signed-us field-only edit at default/original/raw/out-of-range compensation, return preservation and disabled passthrough. No app touched.");
}
static volatile sig_atomic_t stopping=0;
static void stop(int){stopping=1;}
static bool requested(){return access((std::string(MODULE)+"/disable").c_str(),F_OK)!=0&&access((std::string(MODULE)+"/remove").c_str(),F_OK)!=0;}
static void checkState(){
 struct stat s{};require(lstat(ROOTSTATE,&s)==0&&S_ISDIR(s.st_mode)&&s.st_uid==0&&(s.st_mode&077)==0,"Unsafe volatile state directory");
}
static bool targetAlive(const Target& t){return readProcessIdentity(proc(t.pid,"stat"),t.start).state!=ProcessState::Gone;}
static void health(const Target& t){
 verifyIdentity(t.pid,t.start);Memory mem(t.pid);Control c{};mem.get(t.address+DATA,&c,sizeof(c));auto o=originals(t),a=sites(t),v=replacements(t);
 require(c.magic==MAGIC&&c.consume==o[0]&&c.state==o[1]&&c.submit==o[2]&&c.clock==t.scene+0x155bf4&&c.enabled==1&&c.lead>=0&&c.lead<=MAX_LEAD_NS,"Control health check failed");
 require(equal(mem,t.address,PAYLOAD,sizeof(PAYLOAD)),"Payload health check failed");
 for(U i=0;i<3;++i){uintptr_t value;mem.get(a[i],&value,8);require(value==v[i],"Live hook health check failed");}
 require(gxr_consumer_state(mem.bytes(t.runtime+0x1b359c,sizeof(GXR_CONSUMER)).data())==1&&gxr_payload_valid(mem.bytes(t.runtime+0x272ae0,0x520).data(),true),"Velocity Frame dependency changed; restoring pose-time hooks");
}
static void restoreIfPresent(){
 if(access(STATE,F_OK)!=0){require(errno==ENOENT,"Cannot inspect recovery record");return;}
 Target t=saved();if(targetAlive(t)){
  try{restore();}catch(const ProcessGone&){}
  catch(const ProcessAccessError&){if(targetAlive(t))throw;}
 }
 require(unlink(STATE)==0||errno==ENOENT,"Cannot clear recovery record");
}
static void nativeStatus(){
 checkState();if(access(STATE,F_OK)!=0){puts("DISABLED - Waiting: no app patch is resident; inspect startup preference and worker.");return;}
 Target t=saved();if(!targetAlive(t)){puts("DISABLED - Waiting: recorded Steam Link process exited.");return;}
 health(t);puts("ENABLED - Live app hooks verified. Wire epoch = slot label - compensation (see lead_us); no mean subtraction or duration limit.");status(t);
}
static int worker(){
 checkState();int lease=open((std::string(ROOTSTATE)+"/lease").c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
 require(lease>=0&&flock(lease,LOCK_EX|LOCK_NB)==0,"Worker already running");
 signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGHUP,stop);
 const std::string pending=std::string(MODULE)+"/auto.pending";
 Target active{};T checked=0,lead=-1;std::string previous,unknownKey;T unknownSince=0;
 putFile(std::string(ROOTSTATE)+"/pid",std::to_string(getpid())+" "+starttime(getpid())+"\n");
 try{
  // A previous worker's app hook must be recovered before beginning a new session.
  restoreIfPresent();
  while(!stopping&&requested()){
   if(active.pid){
    if(!targetAlive(active)){unlink(STATE);active={};previous.clear();}
    else if(monotonic()>=checked){
     try{health(active);const T want=leadSetting();if(want!=lead&&writeLead(active,want))lead=want;checked=monotonic()+2000000000LL;}
     catch(const ProcessGone&){unlink(STATE);active={};}
     catch(const ProcessAccessError&){if(targetAlive(active))throw;unlink(STATE);active={};}
    }
   }
   if(!active.pid){
    Target candidate{};
    try{
     candidate=current();Memory mem(candidate.pid);verify(candidate,mem);
     require(access(pending.c_str(),F_OK)!=0,"Previous activation interrupted; no same-boot retry");
     if(!requested()||stopping)break;
     putFile(pending,"App-private activation pending\n");
     active=apply();health(active);require(unlink(pending.c_str())==0,"Cannot confirm activation");lead=leadSetting();
     checked=monotonic()+2000000000LL;previous.clear();
    }catch(const Waiting& e){
     // Waiting is only possible before the first write in apply().
     if(access(STATE,F_OK)!=0)unlink(pending.c_str());
     if(previous!=e.what()){printf("DISABLED - Waiting: %s\n",e.what());fflush(stdout);previous=e.what();}
     // Unknown consumer code is transient during Velocity Frame activation; a persistent
     // unknown state in the same app process is a real conflict and still fails closed.
     if(e.what()==std::string(UNKNOWN_CODE)&&candidate.pid){
      const std::string key=std::to_string(candidate.pid)+" "+candidate.start;
      if(key!=unknownKey){unknownKey=key;unknownSince=monotonic();}
      else require(monotonic()-unknownSince<UNKNOWN_LIMIT,"Velocity Frame consumer code stayed unknown for 30 s; foreign hook or incompatible Velocity Frame");
     }else unknownKey.clear();
    }catch(const ProcessGone&){
     // Confirm old process death before clearing any incomplete-apply evidence.
     if(access(STATE,F_OK)==0){Target old=saved();require(!targetAlive(old),"Target identity changed during activation");unlink(STATE);}
     unlink(pending.c_str());active={};
    }catch(const ProcessAccessError&){
     if(!candidate.pid||targetAlive(candidate))throw;
     if(access(STATE,F_OK)==0){Target old=saved();require(!targetAlive(old),"Recovery record belongs to live process");unlink(STATE);}
     unlink(pending.c_str());active={};
    }
   }
   usleep(2000000);
  }
  restoreIfPresent();unlink((std::string(ROOTSTATE)+"/pid").c_str());close(lease);puts("DISABLED - Worker stopped; owned app hooks restored.");return 0;
 }catch(const std::exception& e){
  putFile(std::string(MODULE)+"/disable","");putFile(std::string(ROOTSTATE)+"/failed",e.what());
  try{restoreIfPresent();}catch(const std::exception& restoreError){fprintf(stderr,"RESTORE FAILED: %s; leave disabled and restart Steam Link.\n",restoreError.what());}
  unlink((std::string(ROOTSTATE)+"/pid").c_str());close(lease);throw;
 }
}
int main(int argc,char** argv){try{
 require(argc>=2&&argc<=4,"worker/status/restore/selftest/lead");std::string mode=argv[1];require(mode=="lead"||argc==2,"Only lead takes arguments");
 if(mode=="lead")leadCommand(argc,argv);else if(mode=="selftest")selftest();else if(mode=="worker")return worker();else if(mode=="status")nativeStatus();else if(mode=="restore"){checkState();restoreIfPresent();}else throw std::runtime_error("Unknown command");return 0;
 }catch(const Waiting& e){printf("DISABLED - Waiting: %s\n",e.what());return 2;}
 catch(const std::exception& e){fprintf(stderr,"DISABLED - Error: %s\n",e.what());return 1;}}
