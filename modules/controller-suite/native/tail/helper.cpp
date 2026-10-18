// Author: 4uva4ek. Standalone manually launched RAM experiment, no module/startup.
// Local build only: new injector lifecycle still requires owned-child/device validation.
#include "process.h"
#include "freeze.h"
#include "supervision.h"
#include "payload.h"
#include "rx_policy.h"
#include <asm/ptrace.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/system_properties.h>
#include <poll.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
static constexpr char CLIENT[]="/vendor/lib64/libsctrlclient.so";
static constexpr char SERVICE[]="/vendor/bin/hw/vendor.samsung.hardware.secxrcontroller@1.0-service";
static constexpr char HASH[]="36120385f25c61f5c2d832d2ac2ebf2179dea1ba8142beeb1001994b7f0d22c0";
static constexpr uintptr_t SITE=0x72c5c, RX_END=0xea890, DATA=0x4000, SIZE=0x8000;
static constexpr uint32_t ORIGINAL=0x1f1f5d29;
static volatile sig_atomic_t stopping=0;
static void stop(int){stopping=1;}
using Clock=std::chrono::steady_clock;
struct Lock{
 int fd=-1;
 explicit Lock(const char* name){
  fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);require(fd>=0,"Lock socket failed");
  sockaddr_un a{};a.sun_family=AF_UNIX;size_t n=strlen(name);require(n+1<sizeof(a.sun_path),"Lock too long");memcpy(a.sun_path+1,name,n);
  const auto deadline=Clock::now()+std::chrono::milliseconds(1500);
  while(bind(fd,reinterpret_cast<sockaddr*>(&a),offsetof(sockaddr_un,sun_path)+1+n)<0){
   if(errno!=EADDRINUSE||Clock::now()>=deadline){close(fd);fd=-1;throw std::runtime_error("Another service transaction is active");}usleep(10000);
  }
 }
 ~Lock(){if(fd>=0)close(fd);}
};
struct Target{pid_t pid;std::string stamp,boot;Map map;uintptr_t cave=0;};
static uint32_t branch(uintptr_t from,uintptr_t to){
 int64_t d=int64_t(to)-int64_t(from);require((d&3)==0&&d>=-(1LL<<27)&&d<(1LL<<27),"Branch outside signed 128 MiB reach");
 return 0x14000000u|(uint32_t(d/4)&0x03ffffffu);
}
static std::vector<unsigned char> code(const Target& t){
 std::vector<unsigned char>b(PAYLOAD,PAYLOAD+sizeof(PAYLOAD));uint32_t back=branch(t.cave+RETURN_OFFSET,t.map.base+SITE+4);memcpy(b.data()+RETURN_OFFSET,&back,4);return b;
}
static void identity(const Target& t){
 require(read("/proc/sys/kernel/random/boot_id")==t.boot&&starttime(t.pid)==t.stamp,"Boot/process identity changed");
 require(std::string(read(proc(t.pid,"cmdline")).c_str())==SERVICE,"Wrong HAL process");
 auto m=mapping(t.pid,CLIENT);require(m.base==t.map.base&&m.end==t.map.end,"Library mapping changed");
}
static uint32_t validate(Target& t,Memory& mem,bool allowHook){
 identity(t);require(t.map.base+RX_END<=t.map.end,"RX extent changed");auto disk=read(CLIENT);require(disk.size()>=RX_END,"Short library");
 auto live=mem.bytes(t.map.base,RX_END);uint32_t w;memcpy(&w,live.data()+SITE,4);
 if(w!=ORIGINAL){
  require(allowHook&&(w&0xfc000000u)==0x14000000u,"Unknown tail instruction");
  int64_t imm=w&0x03ffffffu;if(imm&(1<<25))imm-=1LL<<26;
  uintptr_t dest=t.map.base+SITE+imm*4;
  require(dest>=0x10000&&!(dest&0x3fff),"Unaligned payload address");
  if(t.cave)require(dest==t.cave,"Different payload instance");else t.cave=dest;
  auto b=code(t);require(mem.bytes(t.cave,DATA)==std::vector<unsigned char>(b.begin(),b.begin()+DATA),"Payload code does not match this build");
  uint64_t magic;mem.get(t.cave+DATA,&magic,8);require(magic==0x31564c4941545847ULL,"Control identity changed");
 }
 require(normalize_known_words(live.data(),reinterpret_cast<const unsigned char*>(disk.data()),live.size(),w),"Unknown cache/tail instruction");
 require(!memcmp(live.data(),disk.data(),RX_END),"RX differs outside known tail and Cache Bypass words");return w;
}
// Activation requires completed boot. Read-only preflight and restoration also run while
// a KernelSU restart has reset sys.boot_completed; the HAL process survives that restart.
static Target inspect(pid_t pid,bool requireBoot){
 char model[PROP_VALUE_MAX]{},boot[PROP_VALUE_MAX]{};__system_property_get("ro.product.model",model);__system_property_get("sys.boot_completed",boot);
 require(!strcmp(model,"SM-I610"),"Wrong model");require(!requireBoot||!strcmp(boot,"1"),"Android boot not complete");require(hash(CLIENT)==HASH,"Pinned library SHA256 mismatch");
 Target t{pid,starttime(pid),read("/proc/sys/kernel/random/boot_id"),mapping(pid,CLIENT),0};identity(t);return t;
}
static user_pt_regs getRegs(pid_t tid){user_pt_regs r{};iovec io{&r,sizeof(r)};require(ptrace(PTRACE_GETREGSET,tid,reinterpret_cast<void*>(NT_PRSTATUS),&io)==0&&io.iov_len==sizeof(r),"GETREGSET failed");return r;}
static void setRegs(pid_t tid,user_pt_regs& r){iovec io{&r,sizeof(r)};require(ptrace(PTRACE_SETREGSET,tid,reinterpret_cast<void*>(NT_PRSTATUS),&io)==0,"SETREGSET failed");}
static int getSyscall(pid_t tid){int n=0;iovec io{&n,sizeof(n)};require(ptrace(PTRACE_GETREGSET,tid,reinterpret_cast<void*>(NT_ARM_SYSTEM_CALL),&io)==0&&io.iov_len==sizeof(n),"Cannot snapshot syscall number");return n;}
static void setSyscall(pid_t tid,int n){iovec io{&n,sizeof(n)};require(ptrace(PTRACE_SETREGSET,tid,reinterpret_cast<void*>(NT_ARM_SYSTEM_CALL),&io)==0,"Cannot restore syscall number");}
static uintptr_t svcSite(pid_t pid,Memory& mem){
 std::istringstream lines(read(proc(pid,"maps")));std::string line;
 while(std::getline(lines,line)){unsigned long lo,hi,off;char perm[5]{};int used=0;
  if(sscanf(line.c_str(),"%lx-%lx %4s %lx %*s %*s %n",&lo,&hi,perm,&off,&used)!=4||strcmp(perm,"r-xp")||line.substr(used)!="/apex/com.android.runtime/lib64/bionic/libc.so")continue;
  require(hi-lo<4*1024*1024,"Unexpected libc extent");auto b=mem.bytes(lo,hi-lo);
  for(size_t i=0;i+4<=b.size();i+=4){uint32_t w;memcpy(&w,b.data()+i,4);if(w==0xd4000001)return lo+i;}
 }
 throw std::runtime_error("No libc svc instruction");
}
static uint64_t syscallOnce(pid_t tid,uintptr_t svc,uint64_t nr,std::array<uint64_t,6> args){
 auto saved=getRegs(tid),r=saved;int savedSyscall=getSyscall(tid);bool running=false;
 r.pc=svc;r.regs[8]=nr;for(size_t i=0;i<6;++i)r.regs[i]=args[i];
 try{
  setRegs(tid,r);require(ptrace(PTRACE_SINGLESTEP,tid,nullptr,nullptr)==0,"Single step failed");running=true;
  const auto end=Clock::now()+std::chrono::milliseconds(750);int s=0;
  for(;;){pid_t p=waitpid(tid,&s,__WALL|WNOHANG);if(p<0&&errno==EINTR)continue;require(p>=0,"Syscall wait failed");if(p==tid)break;require(Clock::now()<end,"Syscall timeout");usleep(1000);}
  running=false;require(WIFSTOPPED(s)&&WSTOPSIG(s)==SIGTRAP,"Unexpected syscall stop");r=getRegs(tid);require(r.pc==svc+4,"Unexpected svc return");
  uint64_t result=r.regs[0];setSyscall(tid,savedSyscall);setRegs(tid,saved);return result;
 }catch(...){
  if(running){(void)ptrace(PTRACE_INTERRUPT,tid,nullptr,nullptr);int s=0;auto end=Clock::now()+std::chrono::milliseconds(250);while(Clock::now()<end){if(waitpid(tid,&s,__WALL|WNOHANG)==tid){running=false;break;}usleep(1000);}}
  if(running){fprintf(stderr,"Unresolved syscall stop; exit to release tracing. Full power cycle may be needed.\n");_exit(71);}
  try{setSyscall(tid,savedSyscall);setRegs(tid,saved);}catch(...){fprintf(stderr,"Register restore failed; full power cycle required.\n");_exit(72);}throw;
 }
}
static uintptr_t hole(pid_t pid,uintptr_t base){
 const uintptr_t low=base>0x07000000?base-0x07000000:0x10000,high=base+0x07000000;
 uintptr_t best=0,distance=std::numeric_limits<uintptr_t>::max(),previous=0;
 auto consider=[&](uintptr_t a,uintptr_t b){
  a=(std::max(a,low)+0x3fff)&~uintptr_t(0x3fff);b=std::min(b,high);
  if(a>=b||b-a<SIZE)return;
  uintptr_t candidate=a;
  if(b<=base)candidate=(b-SIZE)&~uintptr_t(0x3fff);
  if(candidate<a)return;
  uintptr_t d=candidate>base?candidate-base:base-candidate;
  if(d<distance){best=candidate;distance=d;}
 };
 std::istringstream lines(read(proc(pid,"maps")));std::string line;
 while(std::getline(lines,line)){unsigned long lo,hi;if(sscanf(line.c_str(),"%lx-%lx",&lo,&hi)!=2)continue;
  consider(previous,lo);previous=std::max(previous,uintptr_t(hi));
 }
 consider(previous,high);require(best,"No verified nearby gap for code/control");return best;
}
// File-backed private RX clone: HAL permits executable vendor-library mappings,
// while anonymous RW->RX is denied. The fd is O_RDONLY; POKETEXT creates private COW.
// File offset 0x4000 keeps this clone distinct from the real library load at offset 0.
// Only our just-allocated first page is unmapped/remapped. No existing VMA is replaced.
static void executableClone(Target& t,Memory& mem,uintptr_t svc){
 uintptr_t path=t.cave+DATA+0x1000;mem.put(path,CLIENT,sizeof(CLIENT));
 int64_t fd=syscallOnce(t.pid,svc,56,{uint64_t(-100),path,O_RDONLY|O_CLOEXEC,0,0,0});require(fd>=0&&fd<1000000,"Read-only library open refused");
 bool closed=false;
 try{
  require(syscallOnce(t.pid,svc,215,{t.cave,DATA,0,0,0,0})==0,"Own RW code-page unmap refused");
  auto at=syscallOnce(t.pid,svc,222,{t.cave,DATA,PROT_READ|PROT_EXEC,MAP_PRIVATE|0x100000,uint64_t(fd),0x4000});
  require(at==t.cave,"Private read-only file RX mapping refused");
  require(syscallOnce(t.pid,svc,57,{uint64_t(fd),0,0,0,0,0})==0,"Remote read-only fd close failed");closed=true;
 }catch(...){if(!closed)(void)syscallOnce(t.pid,svc,57,{uint64_t(fd),0,0,0,0,0});throw;}
}
static void prepare(Target& t){
 Lock transaction("gxr-whitening-transaction-v1");Memory mem(t.pid);require(validate(t,mem,false)==ORIGINAL,"Already hooked");
 Frozen frozen;frozen.freeze(t.pid);identity(t);require(validate(t,mem,false)==ORIGINAL,"Code changed");frozen.requireNoPC(t.map.base+0x7209c,t.map.base+0x739d8);
 uintptr_t want=hole(t.pid,t.map.base),svc=svcSite(t.pid,mem);
 // MAP_FIXED_NOREPLACE never overwrites an existing mapping. Unsupported kernels fail closed.
 uintptr_t actual=syscallOnce(t.pid,svc,222,{want,SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|0x100000,uint64_t(-1),0});
 require(actual==want,"Nearby mmap refused or kernel did not honor NOREPLACE");t.cave=actual;
 auto b=code(t);mem.put(t.cave,b.data(),b.size());require(mem.bytes(t.cave,b.size())==b,"Payload writeback mismatch");
 executableClone(t,mem,svc);
 // POKETEXT on the now-executable page uses the same kernel code-write/cache path as the entry word.
 mem.put(t.cave,b.data(),b.size()-2048);require(mem.bytes(t.cave,DATA)==std::vector<unsigned char>(b.begin(),b.begin()+DATA),"RX readback failed");
 require(frozen.release(),"Prepare detach failed");
}
static void restore(Target t){
 // Deliberately restore only our exact entry word even if an unrelated RX word changed.
 Lock transaction("gxr-whitening-transaction-v1");identity(t);Memory mem(t.pid);
 Frozen frozen;frozen.freeze(t.pid);identity(t);uint32_t w;mem.get(t.map.base+SITE,&w,4);
 if(w!=ORIGINAL){
  require(w==branch(t.map.base+SITE,t.cave),"Foreign entry; refuse overwrite");
  // Existing callbacks may finish in the resident cave; never free or rewrite it.
  mem.put(t.map.base+SITE,&ORIGINAL,4);mem.get(t.map.base+SITE,&w,4);require(w==ORIGINAL,"Entry restoration failed");
 }
 require(frozen.release(),"Restore detach failed");puts("RESTORED original entry; resident private allocation retained until process exit");
}
static bool recovery(Target t){
 try{if(read("/proc/sys/kernel/random/boot_id")!=t.boot||starttime(t.pid)!=t.stamp){puts("Original target ended; no replacement process touched");return true;}restore(t);return true;}
 catch(const std::exception& e){if(Frozen::gone(t.pid))return true;fprintf(stderr,"RESTORATION UNVERIFIED: %s; fully remove headset power and reboot.\n",e.what());return false;}
}
static void publish(Target& t){
 Lock transaction("gxr-whitening-transaction-v1");Memory mem(t.pid);Frozen frozen;frozen.freeze(t.pid);identity(t);
 require(validate(t,mem,false)==ORIGINAL,"Entry changed before activation");frozen.requireNoPC(t.map.base+0x7209c,t.map.base+0x739d8);
 require(!stopping,"Stop requested");uint32_t w=branch(t.map.base+SITE,t.cave);mem.put(t.map.base+SITE,&w,4);require(validate(t,mem,true)==w,"Published entry mismatch");require(frozen.release(),"Publish detach failed");
}
static void mode(Target& t,uint32_t enabled,float gain){
 require(enabled<=1&&std::isfinite(gain)&&gain>=0&&gain<=1,"Invalid mode/gain");Lock transaction("gxr-whitening-transaction-v1");Memory mem(t.pid);
 Frozen frozen;frozen.freeze(t.pid);require(validate(t,mem,true)!=ORIGINAL,"Entry is no longer hooked");
 frozen.requireNoPC(t.cave,t.cave+DATA);uint64_t config=enabled;uint32_t f;memcpy(&f,&gain,4);config|=uint64_t(f)<<32;
 mem.put(t.cave+DATA+8,&config,8);require(frozen.release(),"Mode detach failed");printf("MODE=%u gain=%.3f\n",enabled,gain);
}
static void stats(Target& t){
 identity(t);Memory mem(t.pid);auto b=mem.bytes(t.cave+DATA,2048);uint64_t n[4];memcpy(n,b.data()+32,sizeof(n));
 printf("BEST_EFFORT calls=%llu observe=%llu applied=%llu rejected=%llu\n",(unsigned long long)n[0],(unsigned long long)n[1],(unsigned long long)n[2],(unsigned long long)n[3]);
 for(auto spec:std::vector<std::pair<unsigned,unsigned>>{{128,37},{448,102},{1280,42}}){printf("HIST offset=%u",spec.first);for(unsigned i=0;i<spec.second;i++){uint64_t v;memcpy(&v,b.data()+spec.first+i*8,8);printf(" %llu",(unsigned long long)v);}puts("");}
}
template<class Restore> static int guardianWait(int fd,pid_t owner,const std::string& ownerStamp,Restore restoreTarget,int timeout=190000){
 pollfd p{fd,POLLIN|POLLHUP,0};int r;do{r=poll(&p,1,timeout);}while(r<0&&errno==EINTR);char done=0;ssize_t n=r>0?::read(fd,&done,1):0;close(fd);
 if(n==1&&done=='S')return 0;
 if(r<=0){try{if(starttime(owner)==ownerStamp)kill(owner,SIGKILL);}catch(...){}usleep(100000);}
 return restoreTarget()?0:2;
}
#include "late_load.h"
#include "selftest.h"
static float suiteGain(){
 std::istringstream input(read("/data/adb/modules/gxr_controller_suite/settings.conf"));std::string line;
 while(std::getline(input,line)){if(line.rfind("TAIL_GAIN_MILLI=",0)==0){auto text=line.substr(16);require(!text.empty()&&text.size()<=4&&text.find_first_not_of("0123456789")==std::string::npos,"Invalid suite gain");int n=atoi(text.c_str());require(n<=1000,"Gain outside range");return float(n)/1000;}}
 return 1;
}

int main(int argc,char** argv){
 signal(SIGPIPE,SIG_IGN);setvbuf(stdout,nullptr,_IONBF,0);
 for(int fd:{STDOUT_FILENO,STDERR_FILENO}){int f=fcntl(fd,F_GETFL);if(f>=0)(void)fcntl(fd,F_SETFL,f|O_NONBLOCK);}
 try{
  if(argc==2&&!strcmp(argv[1],"--selftest")){selftest();return 0;}
  require(argc>=3,"Usage: gxr-tail-velocity PID preflight|off|hold|module-hold [BOOT_ID STARTTIME]; hold reads observe/apply GAIN/stats/off on stdin, max 180s");
  char* end=nullptr;long id=strtol(argv[1],&end,10);require(end&&!*end&&id>1&&id<INT32_MAX,"Invalid PID");std::string op=argv[2];require(op=="preflight"||op=="off"||op=="hold"||op=="module-hold","Invalid operation");
  const bool moduleMode=op=="module-hold";
  // Exit 3: a KernelSU restart is in progress and nothing is left applied (planned handoff).
  if(moduleMode&&cachegate::restartStarted()){puts("WAITING: KernelSU restart in progress before activation; nothing applied");return 3;}
  Target t=inspect(pid_t(id),op=="hold"||moduleMode);Memory mem(t.pid);uint32_t w=validate(t,mem,true);
  if(op=="preflight"){printf("READ_ONLY pid=%d start=%s boot=%sbase=%llx hooked=%d\n",t.pid,t.stamp.c_str(),t.boot.c_str(),(unsigned long long)t.map.base,w!=ORIGINAL);puts("PREFLIGHT PASS (no ptrace/no writes)");return 0;}
  if(op=="off"){if(w!=ORIGINAL)restore(t);else puts("Already stock");return 0;}
  require(argc==5,"hold requires the freshly inspected BOOT_ID and STARTTIME");require(t.boot==std::string(argv[3])+"\n"&&t.stamp==argv[4],"Explicit expected identity mismatch");require(w==ORIGINAL,"Already active");
  std::string generation;if(moduleMode)generation=cachegate::readyGeneration(true);bool handoff=false;
  Lock run("gxr-tail-velocity-owner-v1");armSupervisorDeath(stop);prepare(t);
  int pipefd[2];require(pipe2(pipefd,O_CLOEXEC)==0,"Guardian pipe failed");pid_t owner=getpid();std::string ownerStamp=starttime(owner);
  pid_t guardian=fork();require(guardian>=0,"Guardian fork failed");
  if(!guardian){close(pipefd[1]);signal(SIGTERM,SIG_IGN);signal(SIGHUP,SIG_IGN);signal(SIGINT,SIG_IGN);
   _exit(guardianWait(pipefd[0],owner,ownerStamp,[&](){return recovery(t);},moduleMode?-1:190000));
  }
  close(pipefd[0]);bool ok=false,trialOk=true;
  try{
   require(kill(guardian,0)==0,"Guardian missing");publish(t);puts(moduleMode ? "SUITE READY: settings control gain; suite disable, gate change or owner exit restores." : "OBSERVE READY: commands observe | apply 0..1 | stats | off; EOF/180s/owner exit restores. Requires matching cache compatibility validator.");
   auto deadline=moduleMode?Clock::time_point::max():Clock::now()+std::chrono::seconds(180);std::string pending;float lastGain=-1;auto nextHealth=Clock::now();
   while(!stopping&&Clock::now()<deadline){
    int st=0;require(waitpid(guardian,&st,WNOHANG)==0,"Guardian exited");identity(t);
    if(moduleMode){
     if(cachegate::restartPending(generation)){handoff=true;puts("PLANNED RESTART: KernelSU restart gate changed; restoring");break;}
     if(access("/data/adb/modules/gxr_controller_suite/disable",F_OK)==0||access("/data/adb/modules/gxr_controller_suite/remove",F_OK)==0)break;
     float gain=suiteGain();if(gain!=lastGain){mode(t,gain>0?1:0,gain);lastGain=gain;}
     if(Clock::now()>=nextHealth){require(validate(t,mem,true)!=ORIGINAL,"Tail health failed");nextHealth=Clock::now()+std::chrono::seconds(2);}
     usleep(200000);continue;
    }
    pollfd input{STDIN_FILENO,POLLIN|POLLHUP,0};int r=poll(&input,1,200);if(r<0&&errno==EINTR)continue;require(r>=0,"Input poll failed");if(!r)continue;
    char b[128];ssize_t n=::read(STDIN_FILENO,b,sizeof(b));if(n<=0)break;pending.append(b,n);require(pending.size()<1024,"Input too long");
    for(size_t pos;(pos=pending.find('\n'))!=std::string::npos;){auto cmd=pending.substr(0,pos);pending.erase(0,pos+1);if(cmd=="off"){stopping=1;break;}if(cmd=="stats")stats(t);else if(cmd=="observe")mode(t,0,1);else if(cmd.rfind("apply ",0)==0){char* e;float g=strtof(cmd.c_str()+6,&e);require(e!=cmd.c_str()+6&&!*e,"Bad gain");mode(t,1,g);}else throw std::runtime_error("Unknown command");}
   }
   stats(t);ok=recovery(t);
  }catch(const std::exception& e){trialOk=false;fprintf(stderr,"TRIAL ERROR: %s\n",e.what());ok=recovery(t);}
  if(ok){char done='S';(void)!::write(pipefd[1],&done,1);}close(pipefd[1]);int st=0;pid_t got;do{got=waitpid(guardian,&st,0);}while(got<0&&errno==EINTR);return trialOk&&ok&&got==guardian&&WIFEXITED(st)&&WEXITSTATUS(st)==0?(handoff?3:0):2;
 }catch(const std::exception& e){fprintf(stderr,"ERROR: %s\n",e.what());return 1;}
}
