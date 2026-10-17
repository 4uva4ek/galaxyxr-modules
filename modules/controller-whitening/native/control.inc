// Author: 4uva4ek. Combined private-memory Whitening + Qc correction.
// No file replacement, ELF header edits, service restart or automatic startup.
#include "payload.h"
#include "patch.h"
#include "../../../tools/controller-imu-compat.h"
#include "supervision.h"
#include <array>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/system_properties.h>
#include <poll.h>
#include <stddef.h>
#include "remote_syscall.inc"

using Clock=std::chrono::steady_clock;
static constexpr uintptr_t BEGIN=0x2c4630,END=0x73c360,ALLOCATION=0x790000;
static constexpr const char* SERVICE="/vendor/bin/hw/vendor.samsung.hardware.secxrcontroller@1.0-service";
static constexpr const char* SHA="4dfb10d768c5cdf14b50873057c59eeacc83a3532c78d10b21652bb9727fe01f";
static constexpr const char* STATE_DIR="/dev/gxr_controller_whitening/projection";
static volatile sig_atomic_t stopping=0;
static volatile sig_atomic_t unsafeProcess=0;
static pid_t guardPid=-1;
static auto activationDeadline=Clock::time_point::min();
static void stop(int){stopping=1;}
static void log(const char* s){printf("%s\n",s);fflush(stdout);}
static void guardianAlive(){
 require(!stopping&&Clock::now()<activationDeadline,"Activation cancelled or deadline expired");
 require(guardPid>1&&kill(guardPid,0)==0,"Guardian absent before mutation");
 int status=0;require(waitpid(guardPid,&status,WNOHANG)==0,"Guardian exited before mutation");
}
struct Lock {
 int fd=-1;
 explicit Lock(const char* name,int ms=0){
  fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);require(fd>=0,"Lock socket");
  sockaddr_un a{};a.sun_family=AF_UNIX;size_t n=strlen(name);require(n+1<sizeof(a.sun_path),"Lock name");memcpy(a.sun_path+1,name,n);
  auto end=Clock::now()+std::chrono::milliseconds(ms);
  while(bind(fd,reinterpret_cast<sockaddr*>(&a),offsetof(sockaddr_un,sun_path)+1+n)<0){
   if(errno!=EADDRINUSE||Clock::now()>=end){close(fd);fd=-1;throw std::runtime_error("Transaction/owner lock busy");}usleep(5000);
  }
 }
 ~Lock(){if(fd>=0)close(fd);}
};
struct Context {
 pid_t pid;std::string stamp,identity,boot,disk;Map m;
 Context(pid_t p,const std::string& s,bool requireBootReady):pid(p),stamp(s){
  require(geteuid()==0,"Root required");require(sysconf(_SC_PAGESIZE)==4096,"Only4KiB pages supported");
  struct statfs fs{};require(statfs("/dev",&fs)==0&&fs.f_type==0x01021994,"/dev must be tmpfs");
  char model[PROP_VALUE_MAX]{},complete[PROP_VALUE_MAX]{};
  __system_property_get("ro.product.model",model);__system_property_get("sys.boot_completed",complete);
  require(!strcmp(model,"SM-I610"),"Wrong model");
  // Read-only status and restoration must work during planned Android restart.
  // Activation and calibration preflight still require completed boot.
  if(requireBootReady)require(!strcmp(complete,"1"),"Boot incomplete");
  identity=read(proc(pid,"cmdline"));require(std::string(identity.c_str())==SERVICE,"Wrong target executable");
  require(starttime(pid)==stamp,"Target start identity differs");boot=read("/proc/sys/kernel/random/boot_id");
  m=mapping(pid);require(hash(m.file)==SHA,"Original libsctrl SHA mismatch");disk=read(m.file);
  require(m.base+END<=m.end&&disk.size()>=END,"RX extent mismatch");
 }
 void verify() const{
  require(starttime(pid)==stamp&&read(proc(pid,"cmdline"))==identity,"Target identity changed");
  auto now=mapping(pid);require(now.base==m.base&&now.end==m.end,"Target mapping changed");
  require(read("/proc/sys/kernel/random/boot_id")==boot,"Kernel boot changed");
 }
};
struct State {int qc=0;int whitening=0;}; // qc0 stock,1 complete,2 partial.
static State validate(Memory& mem,const Context& c){
 auto live=mem.bytes(c.m.base+BEGIN,END-BEGIN);
 auto at=[&](uintptr_t va){return live.data()+va-BEGIN;};
 unsigned changed=0;
 for(auto& p:PATCHES){uint32_t word=0;memcpy(&word,at(p.va),4);require(word==p.old_word||word==p.new_word,"Unknown Qc instruction");changed+=word==p.new_word;memcpy(at(p.va),&p.old_word,4);}
 State s;s.qc=changed==0?0:changed==std::size(PATCHES)?1:2;
 unsigned white=0;
 for(size_t off=0;off<GXR_WHITENING_LENGTH;off+=4){
  require(!memcmp(at(GXR_WHITENING_SITE)+off,GXR_WHITENING_ORIGINAL+off,4)||!memcmp(at(GXR_WHITENING_SITE)+off,GXR_WHITENING_PATCHED+off,4),"Unknown Whitening word");
  if(memcmp(GXR_WHITENING_ORIGINAL+off,GXR_WHITENING_PATCHED+off,4)&&!memcmp(at(GXR_WHITENING_SITE)+off,GXR_WHITENING_PATCHED+off,4))++white;
 }
 s.whitening=white==0?0:white==2?1:2;
 memcpy(at(GXR_WHITENING_SITE),GXR_WHITENING_ORIGINAL,GXR_WHITENING_LENGTH);
 normalizeImuWait(live,BEGIN);
 const unsigned char oldPayload[]={95,11,0,113,227,1,0,84,122,5,251,23};
 const uint32_t oldBridge=402326809u,oldEntry=0x34fff5da;
 require(!memcmp(at(0x5e8540),&oldEntry,4),"Old min2 branch active");
 for(auto old:std::vector<std::pair<uintptr_t,std::vector<unsigned char>>>{
  {0x726f54,{oldPayload,oldPayload+sizeof(oldPayload)}},
  {0x726f94,{reinterpret_cast<const unsigned char*>(&oldBridge),reinterpret_cast<const unsigned char*>(&oldBridge)+4}}}){
  bool empty=std::all_of(at(old.first),at(old.first)+old.second.size(),[](unsigned char x){return x==0;});
  require(empty||!memcmp(at(old.first),old.second.data(),old.second.size()),"Unknown old payload");memset(at(old.first),0,old.second.size());
 }
 require(!memcmp(live.data(),c.disk.data()+BEGIN,live.size()),"RX differs outside exact accepted peer patches");return s;
}
static void calibrationOff(Memory& mem,const Context& c){
 char prop[PROP_VALUE_MAX]{};__system_property_get("persist.debug_sctrlcalib",prop);require(!prop[0]||!strcmp(prop,"0"),"Calibration property enabled");
 unsigned char init=0,enabled=1;mem.get(c.m.base+0x7719c8,&init,1);require(init&1,"Context uninitialized");
 mem.get(c.m.base+0x771af0+0x378,&enabled,1);require(!enabled,"Calibration enabled");
 auto ptr=[&](uintptr_t a){uint64_t x=0;mem.get(a,&x,8);x&=0x00ffffffffffffffULL;require(x>4096,"Invalid System pointer");return x;};
 uint64_t system=ptr(ptr(c.m.base+0x755dd0)),v[2]{};mem.get(system+0xe0,v,sizeof(v));require(v[0]==v[1],"Calibration workers present");
}
struct Region {uintptr_t lo,hi;std::string perms,path;unsigned long off;};
static std::vector<Region> regions(pid_t pid){
 std::istringstream in(read(proc(pid,"maps")));std::string l;std::vector<Region> out;
 while(std::getline(in,l)){unsigned long lo,hi,off;char p[5]{};int used=0;
  if(sscanf(l.c_str(),"%lx-%lx %4s %lx %*s %*s %n",&lo,&hi,p,&off,&used)==4)out.push_back({lo,hi,p,l.substr(used),off});
 }return out;
}
static void payloadValid(Memory&,const Context&);
// Gap, exact failed-test RW page, or an exact retained private RX payload.
static int allocationReady(Memory& mem,const Context& c){
 uintptr_t a=c.m.base+ALLOCATION;
 for(auto& r:regions(c.pid))if(r.hi>a&&r.lo<a+4096){
  if(r.perms=="r-xp"){payloadValid(mem,c);return 2;}
  require(r.lo==a&&r.hi==a+4096&&r.perms=="rw-p"&&r.path.empty(),"Allocation occupied by a different mapping");
  std::vector<unsigned char> expected(4096,0);memcpy(expected.data()+0x300,PAYLOAD,sizeof(PAYLOAD));
  require(mem.bytes(a,4096)==expected,"Orphan RW page does not exactly match ORION v3");return true;
 }
 return false;
}
static void payloadValid(Memory& mem,const Context& c){
 bool mapped=false;for(auto& r:regions(c.pid))if(r.lo<=c.m.base+ALLOCATION&&r.hi>=c.m.base+ALLOCATION+4096){
  require(r.lo==c.m.base+ALLOCATION&&r.hi==c.m.base+ALLOCATION+4096&&r.perms=="r-xp"&&r.path==c.m.file&&r.off==0x1000,"Payload mapping must be a private RX copy of original library page");mapped=true;
 }
 require(mapped,"Payload mapping missing");
 std::vector<unsigned char> expected(c.disk.begin()+0x1000,c.disk.begin()+0x2000);memcpy(expected.data()+0x300,PAYLOAD,sizeof(PAYLOAD));
 require(mem.bytes(c.m.base+ALLOCATION,4096)==expected,"Private payload page bytes mismatch");
}
static uintptr_t syscallSite(pid_t pid,Memory& mem){
 for(auto& r:regions(pid))if(r.perms=="r-xp"&&r.path=="/apex/com.android.runtime/lib64/bionic/libc.so"){
  require(r.hi-r.lo<4*1024*1024,"Unexpected libc mapping");auto b=mem.bytes(r.lo,r.hi-r.lo);
  for(size_t i=0;i+4<=b.size();i+=4){uint32_t w=0;memcpy(&w,b.data()+i,4);if(w==0xd4000001)return r.lo+i;}
 }
 throw std::runtime_error("No mapped libc SVC");
}
static void noPC(const Frozen& f,const Context& c){
 f.requireNoPC(c.m.base+0x639d1c,c.m.base+0x63a084);
 f.requireNoPC(c.m.base+ALLOCATION,c.m.base+ALLOCATION+4096);
 f.requireNoPC(c.m.base+(GXR_WHITENING_SITE&~uintptr_t(7)),c.m.base+GXR_WHITENING_END);
 for(auto& p:PATCHES)f.requireNoPC(c.m.base+(p.va&~uintptr_t(7)),c.m.base+(p.va&~uintptr_t(7))+8);
}
static pid_t syscallThread(const Frozen& f){
 for(auto& t:f.threads)if(t.seized&&t.stopped&&remoteSyscallEligible(t.id))return t.id;
 throw std::runtime_error("No stopped thread outside a kernel syscall; retry later");
}
static void stateDir(){
 struct stat s{};if(lstat(STATE_DIR,&s)){require(errno==ENOENT&&mkdir(STATE_DIR,0700)==0,"State directory create");require(lstat(STATE_DIR,&s)==0,"State directory stat");}
 require(S_ISDIR(s.st_mode)&&s.st_uid==0&&(s.st_mode&077)==0,"Unsafe state directory");
}
static std::string leaseText(const Context& c){return c.boot+std::to_string(c.pid)+" "+c.stamp+" "+std::to_string(c.m.base)+"\n";}
static void lease(const Context& c){
 stateDir();std::string path=std::string(STATE_DIR)+"/lease";int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);require(fd>=0,"Lease already exists; restore/review first");
 auto s=leaseText(c);bool ok=write(fd,s.data(),s.size())==static_cast<ssize_t>(s.size());close(fd);require(ok,"Lease write failed");
}
static void requireLease(const Context& c){
 std::string path=std::string(STATE_DIR)+"/lease";struct stat st{};
 require(lstat(path.c_str(),&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==0&&(st.st_mode&077)==0,"Owned lease absent/unsafe");
 require(read(path)==leaseText(c),"Owned lease target identity mismatch");
}
static bool unsafeRegisters(){return unsafeProcess||access((std::string(STATE_DIR)+"/unsafe-registers").c_str(),F_OK)==0;}
static void remoteFailure(Frozen& f,const Context& c,const RemoteSyscallFatal& e){
 for(auto& t:f.threads)if(t.id==e.tid){t.stopped=e.threadStopped;t.signal=e.pendingSignal;if(e.targetGone)t.seized=false;}
 if(e.targetGone||e.registersRestored)return;
 unsafeProcess=1;
 // No code detours have been installed yet. Do not resume an uncertified
 // context: queue a volatile group stop for our exact target before detaching.
 std::string path=std::string(STATE_DIR)+"/unsafe-registers";
 int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd>=0){std::string text=leaseText(c)+e.what()+"\nFULL POWER CYCLE REQUIRED\n";(void)!write(fd,text.data(),text.size());close(fd);}
 bool contained=false;try{if(starttime(c.pid)==c.stamp&&read(proc(c.pid,"cmdline"))==c.identity)contained=kill(c.pid,SIGSTOP)==0;}catch(...){}
 if(contained)for(auto& t:f.threads)if(t.seized&&t.stopped)t.signal=SIGSTOP;
 fprintf(stderr,"UNSAFE_REGISTERS: exact target group-stop requested=%d. Do not resume HAL or Whitening; fully remove power and reboot.\n",contained);
 // Even if ptrace release needs its bounded emergency exit, the independent
 // guardian sees unsafe-registers and must never report ordinary recovery.
 (void)f.release(100);
}
static void removeLease(const Context& c){requireLease(c);require(unlink((std::string(STATE_DIR)+"/lease").c_str())==0,"Lease removal failed");}
static void stockWrites(Memory& mem,const Context& c){
 // Frozen transaction: disconnect all entry branches, restore producer/Whitening.
 for(auto it=std::rbegin(PATCHES);it!=std::rend(PATCHES);++it)mem.put(c.m.base+it->va,&it->old_word,4);
 mem.put(c.m.base+GXR_WHITENING_SITE,GXR_WHITENING_ORIGINAL,GXR_WHITENING_LENGTH);
 auto s=validate(mem,c);require(s.qc==0&&s.whitening==0,"Stock restoration readback failed");
}
static void activate(Context& c){
 Memory mem(c.pid);auto pre=validate(mem,c);require(pre.qc==0&&pre.whitening==0,"Quiesce legacy Whitening to stock before activation");calibrationOff(mem,c);(void)allocationReady(mem,c);
 uintptr_t svc=syscallSite(c.pid,mem);lease(c);
 Lock transaction("gxr-whitening-transaction-v1",500);auto began=Clock::now();Frozen f;f.freeze(c.pid,250);
 c.verify();noPC(f,c);int allocation=allocationReady(mem,c);auto s=validate(mem,c);require(!s.qc&&!s.whitening,"Concurrent code change");calibrationOff(mem,c);require(!stopping,"Stopped before activation");
 pid_t tid=allocation==2?0:syscallThread(f);uintptr_t expected=c.m.base+ALLOCATION;
 bool wrote=false;
 try{
  guardianAlive();
  // Existing vendor library is already executable in this domain. Map a NEW
  // private view of its pinned page, then POKETEXT writes only its COW memory.
  // No original-file writes, policy changes, mprotect, or destructive MAP_FIXED.
  if(allocation!=2){
  if(allocation==0){
   uint64_t scratch=remoteSyscall(tid,svc,222,{expected,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|0x100000,UINT64_MAX,0});
   if(scratch!=expected&&scratch>=0x10000&&scratch<0x0001000000000000ULL)
    require(remoteSyscall(tid,svc,215,{scratch,4096,0,0,0,0})==0,"Wrong scratch cleanup failed");
   require(scratch==expected,"Scratch no-replace mmap refused");
  }
  const char library[]="/vendor/lib64/libsctrl.so";
  mem.put(expected,library,sizeof(library));
  uint64_t fd=remoteSyscall(tid,svc,56,{uint64_t(int64_t(-100)),expected,O_RDONLY|O_CLOEXEC,0,0,0});
  if(fd>=1048576){
   const unsigned char zero[sizeof(library)]{};mem.put(expected,zero,sizeof(zero));
   throw std::runtime_error("Read-only original library openat errno="+std::to_string(-int64_t(fd)));
  }
  uint64_t unmapped=remoteSyscall(tid,svc,215,{expected,4096,0,0,0,0});
  if(unmapped!=0){(void)remoteSyscall(tid,svc,57,{fd,0,0,0,0,0});throw std::runtime_error("Own scratch munmap refused");}
  uint64_t addr=remoteSyscall(tid,svc,222,{expected,4096,PROT_READ|PROT_EXEC,MAP_PRIVATE|0x100000,fd,0x1000});
  require(remoteSyscall(tid,svc,57,{fd,0,0,0,0,0})==0,"Read-only FD close failed");
  if(addr!=expected&&addr>=0x10000&&addr<0x0001000000000000ULL)
   require(remoteSyscall(tid,svc,215,{addr,4096,0,0,0,0})==0,"Wrong private RX map cleanup failed");
  if(addr!=expected)throw std::runtime_error("Private file RX mmap refused errno="+std::to_string(-int64_t(addr)));
  mem.put(expected+0x300,PAYLOAD,sizeof(PAYLOAD));
  } // Existing exact private RX payload is reused without syscalls or unmap.
  payloadValid(mem,c);
  require(Clock::now()-began<std::chrono::milliseconds(700),"Activation budget exhausted before code edits");
  guardianAlive();wrote=true;
  for(auto& p:PATCHES)mem.put(c.m.base+p.va,&p.new_word,4);
  mem.put(c.m.base+GXR_WHITENING_SITE,GXR_WHITENING_PATCHED,GXR_WHITENING_LENGTH);
  s=validate(mem,c);require(s.qc==1&&s.whitening==1,"Combined patch verification failed");
 }catch(const RemoteSyscallFatal& e){remoteFailure(f,c,e);throw;}
 catch(...){if(wrote){try{stockWrites(mem,c);}catch(...){fprintf(stderr,"FATAL: code rollback unverified; power-cycle required\n");}}throw;}
 require(f.release(100),"Detach failed after activation");
 printf("WHITENING_ACTIVE pid=%d start=%s base=%llx allocation=%llx transaction_ms=%.3f\n",c.pid,c.stamp.c_str(),(unsigned long long)c.m.base,(unsigned long long)expected,std::chrono::duration<double,std::milli>(Clock::now()-began).count());fflush(stdout);
}
static bool restore(Context& c){
 if(unsafeRegisters()){log("UNSAFE_REGISTERS: no automatic detach/resume/recovery claim; FULL POWER CYCLE REQUIRED");return false;}
 if(Frozen::gone(c.pid)){try{removeLease(c);}catch(...){}log("Original target exited; private patch discarded");return true;}
 try{if(starttime(c.pid)!=c.stamp){try{removeLease(c);}catch(...){}log("Target replaced; old private patch discarded, replacement untouched");return true;}}catch(...){return false;}
 for(int attempt=0;attempt<3;++attempt){
  try{
   Lock transaction("gxr-whitening-transaction-v1",500);requireLease(c);Memory mem(c.pid);auto state=validate(mem,c);
   if(!state.qc&&!state.whitening){removeLease(c);log("ORION_STOCK_VERIFIED; private allocation may remain unreachable");return true;}
   Frozen f;f.freeze(c.pid,250);c.verify();noPC(f,c);validate(mem,c);stockWrites(mem,c);require(f.release(100),"Restore detach failed");
   removeLease(c);log("ORION_STOCK_VERIFIED; private RX payload retained unreachable until process exit");return true;
  }catch(const std::exception& e){fprintf(stderr,"RESTORE attempt%d: %s\n",attempt,e.what());usleep(50000);}
 }
 log("RESTORE_UNVERIFIED: do not resume other patch owner; fully remove power and reboot");return false;
}
static void status(Context& c,bool preflight){
 Memory mem(c.pid);Lock transaction("gxr-whitening-transaction-v1",500);c.verify();auto s=validate(mem,c);
 if(s.qc)payloadValid(mem,c);
 if(preflight){require((s.qc==0&&s.whitening==0)||(s.qc==1&&s.whitening==1),"Partial or legacy-only correction");calibrationOff(mem,c);if(!s.qc)(void)allocationReady(mem,c);}
 printf("%s pid=%d start=%s QC=%d WHITENING=%d RX_VERIFIED\n",s.qc==1&&s.whitening==1?"WHITENING_ACTIVE":s.qc==0&&s.whitening==0?"STOCK":s.qc==0&&s.whitening==1?"LEGACY_WHITENING_ONLY":"PARTIAL_KNOWN_WORDS",c.pid,c.stamp.c_str(),s.qc,s.whitening);fflush(stdout);
}
static bool waitChild(pid_t child,int& status,int ms){
 auto end=Clock::now()+std::chrono::milliseconds(ms);while(Clock::now()<end){pid_t p=waitpid(child,&status,WNOHANG);if(p==child)return true;if(p<0&&errno!=EINTR)return false;usleep(1000);}return false;
}
static int session(Context& c,int seconds){
 require(!unsafeRegisters(),"Previous unsafe-registers guard: full power cycle required");
 Lock owner("orion-qc-projection-run-v1");Lock whiteOwner("gxr-whitening-run-v1");
 int pipefd[2],ready[2];require(pipe2(pipefd,O_CLOEXEC)==0&&pipe2(ready,O_CLOEXEC)==0,"Guardian pipes");pid_t ownerPid=getpid();std::string ownerStamp=starttime(ownerPid);
 pid_t guardian=fork();require(guardian>=0,"Guardian fork");
 if(!guardian){
  close(pipefd[1]);close(ready[0]);signal(SIGINT,SIG_IGN);signal(SIGTERM,SIG_IGN);signal(SIGHUP,SIG_IGN);bool active=false,done=false;
  char rdy='R';if(write(ready[1],&rdy,1)!=1)_exit(3);close(ready[1]);
  for(;;){pollfd p{pipefd[0],POLLIN|POLLHUP,0};int r=poll(&p,1,active?3000:10000);if(r<0&&errno==EINTR)continue;
   char b=0;ssize_t n=r>0?::read(pipefd[0],&b,1):0;if(n==1&&b=='S'){done=true;break;}if(n==1&&b=='U'){unsafeProcess=1;break;}if(n==1&&(b=='A'||b=='H')){active=true;continue;}break;
  }
  close(pipefd[0]);if(done)_exit(0);
  try{if(starttime(ownerPid)==ownerStamp)kill(ownerPid,SIGKILL);}catch(...){}usleep(50000);
  log("GUARDIAN: restoring combined RAM patch after owner/deadline loss");bool ok=restore(c);fflush(nullptr);_exit(ok?0:2);
 }
 close(pipefd[0]);close(ready[1]);pollfd rdy{ready[0],POLLIN|POLLHUP,0};char ack=0;
 bool readyOk=poll(&rdy,1,1000)>0&&::read(ready[0],&ack,1)==1&&ack=='R';close(ready[0]);
 if(!readyOk){close(pipefd[1]);int s=0;waitChild(guardian,s,4000);throw std::runtime_error("Guardian readiness failed; no activation");}
 guardPid=guardian;activationDeadline=Clock::now()+std::chrono::seconds(8);
 printf("OWNER pid=%d guardian=%d target=%d start=%s\n",getpid(),guardian,c.pid,c.stamp.c_str());fflush(stdout);
 bool ok=false;try{
  activate(c);char a='A';require(write(pipefd[1],&a,1)==1,"Guardian activation acknowledgement");
  auto deadline=seconds?Clock::now()+std::chrono::seconds(seconds):Clock::time_point::max();Memory mem(c.pid);
  while(!stopping&&Clock::now()<deadline){
   usleep(200000);char h='H';require(write(pipefd[1],&h,1)==1,"Guardian unavailable");
   c.verify();for(auto& p:PATCHES){uint32_t w=0;mem.get(c.m.base+p.va,&w,4);require(w==p.new_word,"Live patch changed");}
   require(mem.bytes(c.m.base+GXR_WHITENING_SITE,12)==std::vector<unsigned char>(GXR_WHITENING_PATCHED,GXR_WHITENING_PATCHED+12),"Whitening changed");
  }
  ok=restore(c);
 }catch(const std::exception& e){fprintf(stderr,"SESSION: %s\n",e.what());if(unsafeProcess){char u='U';(void)!write(pipefd[1],&u,1);}ok=restore(c);}
 if(ok){char s='S';(void)!write(pipefd[1],&s,1);}close(pipefd[1]);int childStatus=0;
 if(!waitChild(guardian,childStatus,4000)){fprintf(stderr,"Guardian exit not verified; do not restart tracking until recovery is verified\n");return 2;}
 require(ok&&WIFEXITED(childStatus)&&WEXITSTATUS(childStatus)==0,"Combined restoration/guardian failed");
 log("DONE: owner and guardian complete; combined correction restored");return 0;
}
int main(int argc,char** argv){
 signal(SIGPIPE,SIG_IGN);signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGHUP,stop);
 for(int fd:{STDOUT_FILENO,STDERR_FILENO}){int flags=fcntl(fd,F_GETFL);if(flags>=0)(void)fcntl(fd,F_SETFL,flags|O_NONBLOCK);}
 try{
  require(argc==3||argc==4,"Usage: gxr-whitening PID status|preflight|off|hold OR PID run SECONDS(1..180)");
  char* end=nullptr;long p=strtol(argv[1],&end,10);require(end&&!*end&&p>1&&p<INT32_MAX,"Invalid PID");
  std::string stamp=starttime(static_cast<pid_t>(p));require(!stamp.empty()&&std::all_of(stamp.begin(),stamp.end(),[](char c){return c>='0'&&c<='9';}),"Invalid START");
  std::string mode=argv[2];require(mode=="status"||mode=="preflight"||mode=="off"||mode=="hold"||mode=="run","Unknown mode");
  require((mode=="run")== (argc==4),"Seconds only with run");Context c(static_cast<pid_t>(p),stamp,mode!="status"&&mode!="off");
  if(mode=="status"||mode=="preflight"){status(c,mode=="preflight");return 0;}
  if(mode=="off"){Lock owner("orion-qc-projection-run-v1");Lock whiteOwner("gxr-whitening-run-v1");return restore(c)?0:2;}
  int seconds=0;if(mode=="run"){long n=strtol(argv[3],&end,10);require(end&&!*end&&n>=1&&n<=180,"Duration1..180");seconds=static_cast<int>(n);}
  if(mode=="hold")armSupervisorDeath(stop);
  return session(c,seconds);
 }catch(const std::exception& e){fprintf(stderr,"ERROR: %s\n",e.what());return 1;}
}
