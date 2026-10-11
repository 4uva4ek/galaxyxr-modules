#include "patch.h"
static constexpr uintptr_t SITE=GXR_TIME_ENTRY_VA;
static constexpr uintptr_t CAVE=GXR_TIME_ADAPTER_VA;
#define ORIGINAL GXR_TIME_ORIGINAL
#define PATCHED GXR_TIME_ENTRY
#define ADAPTER GXR_TIME_ADAPTER
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <stddef.h>
#include "supervision.h"
static volatile sig_atomic_t stopRequested=0;
static void requestStop(int){stopRequested=1;}
static constexpr uintptr_t TEXT_BEGIN=0x16000, TEXT_END=0x2f000;
static constexpr const char* SERVICE="/vendor/bin/hw/vendor.samsung.hardware.secxrcontroller@1.0-service";
static constexpr const char* LIB_HASH=GXR_TIME_SHA256;
static auto activationDeadline=std::chrono::steady_clock::time_point::min();
static pid_t guardianPid=-1;
static void activationAllowed() {
 require(!stopRequested,"Supervisor requested stop before activation");
 require(std::chrono::steady_clock::now()<activationDeadline,"Activation deadline expired");
 require(guardianPid>1&&kill(guardianPid,0)==0,"Recovery guardian is unavailable");
 int status=0;require(waitpid(guardianPid,&status,WNOHANG)==0,"Recovery guardian exited");
}
// Abstract sockets provide process-lifetime locks without filesystem state.
struct Lock {
 int fd=-1;
 explicit Lock(const char* name,int waitms=0) {
  fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);require(fd>=0,"Lock socket failed");
  sockaddr_un a{};a.sun_family=AF_UNIX;size_t n=strlen(name);require(n+1<sizeof(a.sun_path),"Lock name too long");
  memcpy(a.sun_path+1,name,n);
  auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(waitms);
  while(bind(fd,reinterpret_cast<sockaddr*>(&a),offsetof(sockaddr_un,sun_path)+1+n)<0) {
   if(errno!=EADDRINUSE||std::chrono::steady_clock::now()>=until) {close(fd);fd=-1;throw std::runtime_error("Another experiment transaction is running");}
   usleep(10000);
  }
 }
 ~Lock(){if(fd>=0)close(fd);}
};
// A killed staging writer can leave known 8-byte payload words among zero words.
// With STOCK entry these are inert and safely recoverable; active entry always
// requires the complete adapter. Never erase a retained or partial adapter.
static_assert(CAVE%8==0&&sizeof(ADAPTER)%8==0,"Adapter staging must use aligned 8-byte words");
static int validate(Memory& mem,const Map& m,const std::string& disk,int* adapterState=nullptr) {
 require(m.base+TEXT_END<=m.end&&disk.size()>=TEXT_END,"RX extent mismatch");
 auto live=mem.bytes(m.base+TEXT_BEGIN,TEXT_END-TEXT_BEGIN);
 auto slot=[&](uintptr_t a){return live.data()+a-TEXT_BEGIN;};
 bool active=!memcmp(slot(SITE),PATCHED,sizeof(PATCHED));
 require(active||!memcmp(slot(SITE),ORIGINAL,sizeof(ORIGINAL)),"Unknown timestamp entry instructions");
 bool complete=!memcmp(slot(CAVE),ADAPTER,sizeof(ADAPTER));
 bool empty=std::all_of(slot(CAVE),slot(CAVE)+sizeof(ADAPTER),[](unsigned char b){return b==0;});
 for(size_t offset=0;offset<sizeof(ADAPTER);offset+=8) {
  uint64_t word=0;memcpy(&word,slot(CAVE)+offset,8);
  require(word==0||!memcmp(slot(CAVE)+offset,ADAPTER+offset,8),"Unknown timestamp adapter bytes; reboot required");
 }
 require(!active||complete,"Active entry has no complete timestamp adapter");
 if(adapterState)*adapterState=complete?1:empty?0:2;
 require(std::all_of(disk.begin()+CAVE,disk.begin()+CAVE+sizeof(ADAPTER),[](char b){return b==0;}),"Original adapter padding is not zero");
 memcpy(slot(SITE),ORIGINAL,sizeof(ORIGINAL));
 memset(slot(CAVE),0,sizeof(ADAPTER));
 require(!memcmp(live.data(),disk.data()+TEXT_BEGIN,live.size()),"HAL RX differs outside known timestamp patch");
 uint64_t binding=0;mem.get(m.base+0x2fdb8,&binding,sizeof(binding));
 require(binding==m.base+0x225dc,"getPredictPose binding differs from expected native callee");
 return active?1:0;
}

static bool change(pid_t pid,const std::string& mode,const std::string& expectedStamp) {
 // Existing whitening owners/guardians already use this abstract lock. Its
 // legacy name now serializes both helpers' transactions on the same service.
 Lock transaction("gxr-whitening-transaction-v1",1500);
 auto identity=read(proc(pid,"cmdline"));require(std::string(identity.c_str())==SERVICE,"Wrong process");
 auto stamp=starttime(pid);require(stamp==expectedStamp,"Process restarted: refusing reused PID");
 auto m=mapping(pid);require(hash(m.file)==LIB_HASH,"Wrong firmware");
 auto disk=read(m.file);Memory mem(pid);int adapterState=0;int before=validate(mem,m,disk,&adapterState);
 if(mode=="status"||mode=="preflight") {
  printf("%s pid=%d start=%s adapter=%s%s\n",before?"TIMESTAMP_PRECISE_ACTIVE":"STOCK",pid,stamp.c_str(),adapterState==1?"COMPLETE":adapterState==2?"PARTIAL_INERT":"EMPTY",mode=="preflight"?" RX=VERIFIED binding=NATIVE":"");fflush(stdout);return before!=0;
 }
 bool on=mode=="on";
 if(on)activationAllowed();
 require(!on||!before,"Already active; use off first");
 if(on==before) {puts("STOCK verified; no code write needed; any adapter remains inert");fflush(stdout);return false;}
 for(int attempt=0;attempt<8;++attempt) {
  auto freezeStart=std::chrono::steady_clock::now();
  Frozen frozen;frozen.freeze(pid);
  require(starttime(pid)==stamp&&read(proc(pid,"cmdline"))==identity,"Identity changed");
  auto again=mapping(pid);require(again.base==m.base&&again.end==m.end,"Mapping changed");
  // The entry is one instruction. Stage the whole adapter before exposing it.
  // Off only restores the entry; in-flight adapter users must be allowed to finish.
  if(frozen.pcInRange(m.base+SITE,m.base+SITE+sizeof(ORIGINAL))||
     (on&&frozen.pcInRange(m.base+CAVE,m.base+CAVE+sizeof(ADAPTER)))) {
   require(frozen.release(),"Thread release failed; exit required");usleep(5000);continue;
  }
  before=validate(mem,m,disk);require(before!=int(on),"Concurrent state change");
  require(!on||before==0,"Only stock state can be activated");
  if(on)activationAllowed();
  try {
   if(on) {
    auto adapter=mem.bytes(m.base+CAVE,sizeof(ADAPTER));
    if(memcmp(adapter.data(),ADAPTER,sizeof(ADAPTER)))
     mem.put(m.base+CAVE,ADAPTER,sizeof(ADAPTER));
    require(!validate(mem,m,disk),"Adapter staging altered stock entry");
    adapter=mem.bytes(m.base+CAVE,sizeof(ADAPTER));
    require(!memcmp(adapter.data(),ADAPTER,sizeof(ADAPTER)),"Adapter staging is incomplete; entry remains stock");
    activationAllowed();
   }
   mem.put(m.base+SITE,on?PATCHED:ORIGINAL,sizeof(ORIGINAL));
   require(validate(mem,m,disk)==int(on),"Verification failed");
  } catch(...) {
   try {
    mem.put(m.base+SITE,ORIGINAL,sizeof(ORIGINAL));
    auto restored=mem.bytes(m.base+SITE,sizeof(ORIGINAL));
    require(!memcmp(restored.data(),ORIGINAL,sizeof(ORIGINAL)),"Rollback entry verification failed");
    // Recognized partial staging remains inert and is intentionally retained.
    require(!validate(mem,m,disk),"Rollback verification failed");
   } catch(...) {fprintf(stderr,"RESTORE ERROR: disable module, then fully reboot headset. Adapter was not erased.\n");}
   throw;
  }
  size_t threads=frozen.threads.size();
  require(frozen.release(),"Thread release failed; exit required");
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-freezeStart).count();
  printf("%s pid=%d start=%s threads=%zu verified; freeze-through-release %.3f ms; private RAM only; adapter retained\n",on?"TIMESTAMP_PRECISE_ACTIVE":"STOCK",pid,stamp.c_str(),threads,ms);fflush(stdout);return on;
 }
 throw std::runtime_error("Thread remained in patch window; no code changed");
}
static bool restore(pid_t pid,const std::string& stamp) {
 for(int retry=0;retry<3;++retry) {
  try {change(pid,"off",stamp);return true;}
  catch(const std::exception& e){fprintf(stderr,"Restore attempt %d: %s\n",retry+1,e.what());usleep(200000);}
 }
 fprintf(stderr,"RESTORE NOT VERIFIED: disable module, then fully reboot the headset.\n");return false;
}
int main(int argc,char** argv) {
 // A disconnected logging pipe must never kill or block recovery.
 signal(SIGPIPE,SIG_IGN);
 for(int output:{STDOUT_FILENO,STDERR_FILENO}){int flags=fcntl(output,F_GETFL);if(flags>=0)(void)fcntl(output,F_SETFL,flags|O_NONBLOCK);}
 pid_t pid=0,guardian=-1;int pipefd[2]{-1,-1};bool attempted=false;std::string stamp;
 try {
  require(argc==3||argc==4,"Usage: gxr-timestamp PID status|preflight|off|hold OR gxr-timestamp PID run SECONDS(1..60)");
  char* end=nullptr;long id=strtol(argv[1],&end,10);require(end&&!*end&&id>1&&id<INT32_MAX,"Invalid PID");pid=id;
  std::string mode=argv[2];require(mode=="status"||mode=="preflight"||mode=="off"||mode=="run"||mode=="hold","Invalid mode");
  require((mode=="run")==(argc==4),"SECONDS required only for run");
  stamp=starttime(pid);
  if(mode!="run"&&mode!="hold"){change(pid,mode,stamp);return 0;}
  bool hold=mode=="hold";long seconds=0;
  if(hold)armSupervisorDeath(requestStop);
  if(!hold){seconds=strtol(argv[3],&end,10);require(end&&!*end&&seconds>=1&&seconds<=60,"Invalid duration");}
  Lock runLock("gxr-timestamp-run-v1");
  require(!change(pid,"status",stamp),"An existing patch must be restored first");
  const pid_t parentPid=getpid();const auto parentStamp=starttime(parentPid);
  activationDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  require(pipe2(pipefd,O_CLOEXEC)==0,"Guardian pipe failed");
  guardian=fork();require(guardian>=0,"Guardian fork failed");
  if(!guardian) {
   close(pipefd[1]);signal(SIGINT,SIG_IGN);signal(SIGTERM,SIG_IGN);signal(SIGHUP,SIG_IGN);
   pollfd p{pipefd[0],POLLIN|POLLHUP,0};int result;
   do {result=poll(&p,1,6000);}while(result<0&&errno==EINTR);
   char b=0;ssize_t n=result>0?::read(pipefd[0],&b,1):0;
   if(n==1&&b=='A') {
    // Activation acknowledged. Hold has no elapsed-time rollback; a broken
    // parent/pipe still invokes recovery. Bounded runs remain for fixture QA.
    do {result=poll(&p,1,hold?-1:int((seconds+3)*1000));}while(result<0&&errno==EINTR);
    b=0;n=result>0?::read(pipefd[0],&b,1):0;
   }
   close(pipefd[0]);
   if(n==1&&b=='S')_exit(0); // Parent has already verified restoration.
   if(result<=0) {
    // Release a hung tracer/transaction owner before attempting recovery.
    // Only our own exact parent instance may be terminated.
    try {if(starttime(parentPid)==parentStamp)(void)kill(parentPid,SIGKILL);}catch(...){}
    usleep(100000);
   }
   fprintf(stderr,"GUARDIAN: parent ended or deadline reached; restoring stock.\n");
   bool ok=restore(pid,stamp);fflush(nullptr);_exit(ok?0:2);
  }
  close(pipefd[0]);pipefd[0]=-1;
  guardianPid=guardian;
  printf("OWNER pid=%d guardian=%d target=%d\n",getpid(),guardian,pid);fflush(stdout);
  signal(SIGINT,requestStop);signal(SIGTERM,requestStop);signal(SIGHUP,requestStop);signal(SIGPIPE,SIG_IGN);
  attempted=true;change(pid,"on",stamp);
  char active='A';require(::write(pipefd[1],&active,1)==1,"Guardian activation acknowledgement failed");
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
  Memory monitoring(pid);auto monitoredMap=mapping(pid);unsigned stockReads=0;
  printf("MODE %s; disable module to stop and restore; reboot after disabling if recovery fails.\n",hold?"HOLD_UNTIL_USER_OFF":"BOUNDED_FIXTURE_RUN");fflush(stdout);
  while(!stopRequested&&(hold||std::chrono::steady_clock::now()<deadline)) {
   usleep(200000);
   // Detect an independently requested off without keeping a hidden active owner.
   auto words=monitoring.bytes(monitoredMap.base+SITE,sizeof(ORIGINAL));
   bool isStock=!memcmp(words.data(),ORIGINAL,sizeof(ORIGINAL));
   require(isStock||!memcmp(words.data(),PATCHED,sizeof(PATCHED)),"Timestamp entry changed unexpectedly");
   stockReads=isStock?stockReads+1:0;
   if(stockReads>=2)break;
  }
  require(restore(pid,stamp),"Timed restoration failed");attempted=false;
  char done='S';(void)!::write(pipefd[1],&done,1);close(pipefd[1]);pipefd[1]=-1;
  int status=0;require(waitpid(guardian,&status,0)==guardian,"Guardian wait failed");guardian=-1;
  require(WIFEXITED(status)&&WEXITSTATUS(status)==0,"Guardian failed");
  puts("DONE: stock instructions restored and read back. No startup or original-file changes.");return 0;
 } catch(const std::exception& e) {
  fprintf(stderr,"ERROR: %s (%s)\n",e.what(),strerror(errno));
  bool restored=!attempted||restore(pid,stamp);
  if(pipefd[1]>=0){if(restored){char done='S';(void)!::write(pipefd[1],&done,1);}close(pipefd[1]);}
  if(pipefd[0]>=0)close(pipefd[0]);
  if(guardian>0){int status=0;while(waitpid(guardian,&status,0)<0&&errno==EINTR){}}
  return 1;
 }
}
