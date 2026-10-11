#include "patch.h"
static constexpr uintptr_t SITE=GXR_WHITENING_SITE;
#define ORIGINAL GXR_WHITENING_ORIGINAL
#define PATCHED GXR_WHITENING_PATCHED
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <stddef.h>
#include <sys/system_properties.h>
#include "supervision.h"
static volatile sig_atomic_t stopRequested=0;
static void requestStop(int){stopRequested=1;}
static constexpr uintptr_t TEXT_BEGIN=0x2c4630, TEXT_END=0x73c360;
static constexpr const char* SERVICE="/vendor/bin/hw/vendor.samsung.hardware.secxrcontroller@1.0-service";
static constexpr const char* LIB_HASH="4dfb10d768c5cdf14b50873057c59eeacc83a3532c78d10b21652bb9727fe01f";
static auto activationDeadline=std::chrono::steady_clock::time_point::min();
static pid_t guardianPid=-1;
static void activationAllowed() {
 require(!stopRequested,"Supervisor requested stop before activation");
 require(std::chrono::steady_clock::now()<activationDeadline,"Activation deadline expired");
 require(guardianPid>1&&kill(guardianPid,0)==0,"Recovery guardian is unavailable");
 int status=0;require(waitpid(guardianPid,&status,WNOHANG)==0,"Recovery guardian exited");
}
static void calibrationOff(Memory& mem,const Map& m) {
 char property[PROP_VALUE_MAX]{};__system_property_get("persist.debug_sctrlcalib",property);
 require(!property[0]||!strcmp(property,"0"),"Persistent calibration property is not off");
 unsigned char initialized=0,enabled=1;
 mem.get(m.base+0x7719c8,&initialized,1);require(initialized&1,"Context is not initialized");
 mem.get(m.base+0x771af0+0x378,&enabled,1);require(!enabled,"Calibration mode is enabled; refusing activation");
 auto pointer=[&](uintptr_t address){uint64_t value=0;mem.get(address,&value,8);value&=0x00ffffffffffffffULL;require(value>4096,"Invalid System pointer");return value;};
 uint64_t system=pointer(pointer(m.base+0x755dd0)),range[2]{};
 mem.get(system+0xe0,range,sizeof(range));require(range[0]==range[1],"Calibration worker vector is not empty");
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
// 0=stock, 1=complete patch, 2=known interrupted two-word transaction.
static int validate(Memory& mem,const Map& m,const std::string& disk) {
 require(m.base+TEXT_END<=m.end&&disk.size()>=TEXT_END,"RX extent mismatch");
 auto live=mem.bytes(m.base+TEXT_BEGIN,TEXT_END-TEXT_BEGIN);
 auto slot=[&](uintptr_t a){return live.data()+a-TEXT_BEGIN;};
 int state=!memcmp(slot(SITE),PATCHED,sizeof(PATCHED))?1:0;
 if(!state&&memcmp(slot(SITE),ORIGINAL,sizeof(ORIGINAL))) {
  for(size_t i=0;i<sizeof(ORIGINAL);i+=4)
   require(!memcmp(slot(SITE)+i,ORIGINAL+i,4)||!memcmp(slot(SITE)+i,PATCHED+i,4),"Unknown whitening instructions");
  state=2;
 }
 memcpy(slot(SITE),ORIGINAL,sizeof(ORIGINAL));
 // Prior min2 experiment must be OFF. Only its known unreachable padding is allowed.
 const unsigned char oldPayload[]={95,11,0,113,227,1,0,84,122,5,251,23};
 const uint32_t oldBridge=402326809u,oldEntry=0x34fff5da;
 require(!memcmp(slot(0x5e8540),&oldEntry,4),"Previous min2 experiment is not stock");
 for(auto old:std::vector<std::pair<uintptr_t,std::vector<unsigned char>>>{
     {0x726f54,{oldPayload,oldPayload+sizeof(oldPayload)}},
     {0x726f94,{reinterpret_cast<const unsigned char*>(&oldBridge),reinterpret_cast<const unsigned char*>(&oldBridge)+4}}}) {
  bool empty=std::all_of(slot(old.first),slot(old.first)+old.second.size(),[](unsigned char b){return b==0;});
  require(empty||!memcmp(slot(old.first),old.second.data(),old.second.size()),"Unknown previous payload");
  memset(slot(old.first),0,old.second.size());
 }
 require(!memcmp(live.data(),disk.data()+TEXT_BEGIN,live.size()),"Code differs outside known experiment");
 return state;
}

static bool change(pid_t pid,const std::string& mode,const std::string& expectedStamp) {
 Lock transaction("gxr-whitening-transaction-v1",1500);
 auto identity=read(proc(pid,"cmdline"));require(std::string(identity.c_str())==SERVICE,"Wrong process");
 auto stamp=starttime(pid);require(stamp==expectedStamp,"Process restarted: refusing reused PID");
 auto m=mapping(pid);require(hash(m.file)==LIB_HASH,"Wrong firmware");
 auto disk=read(m.file);Memory mem(pid);int before=validate(mem,m,disk);
 if(mode=="status"||mode=="preflight") {
  if(mode=="preflight")calibrationOff(mem,m);
  printf("%s pid=%d start=%s%s\n",before==2?"PARTIAL_PATCH_REQUIRES_OFF":before?"WHITENING_ACTIVE":"STOCK",pid,stamp.c_str(),mode=="preflight"?" calibration=OFF workers=EMPTY":"");fflush(stdout);return before!=0;
 }
 bool on=mode=="on";
 if(on)activationAllowed();
 require(!on||!before,"Already active; use off first");
 if(on==before) {puts("STOCK verified; no code write needed");fflush(stdout);return false;}
 for(int attempt=0;attempt<8;++attempt) {
  auto freezeStart=std::chrono::steady_clock::now();
  Frozen frozen;frozen.freeze(pid);
  require(starttime(pid)==stamp&&read(proc(pid,"cmdline"))==identity,"Identity changed");
  auto again=mapping(pid);require(again.base==m.base&&again.end==m.end,"Mapping changed");
  // Argument preparation and BL form one semantic transaction, even though two words change.
  if(frozen.pcInRange(m.base+SITE,m.base+SITE+sizeof(ORIGINAL))) {
   require(frozen.release(),"Thread release failed; exit required");usleep(5000);continue;
  }
  before=validate(mem,m,disk);require(before!=int(on),"Concurrent state change");
  require(!on||before==0,"Only stock state can be activated");
  if(on){calibrationOff(mem,m);activationAllowed();}
  try {
   mem.put(m.base+SITE,on?PATCHED:ORIGINAL,sizeof(ORIGINAL));
   require(validate(mem,m,disk)==on,"Verification failed");
  } catch(...) {
   try {mem.put(m.base+SITE,ORIGINAL,sizeof(ORIGINAL));require(!validate(mem,m,disk),"Rollback verification failed");}
   catch(...) {fprintf(stderr,"RESTORE ERROR: disable module, then fully reboot headset.\n");}
   throw;
  }
  size_t threads=frozen.threads.size();
  require(frozen.release(),"Thread release failed; exit required");
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-freezeStart).count();
  printf("%s pid=%d start=%s threads=%zu verified; freeze-through-release %.3f ms; private RAM only\n",on?"WHITENING_ACTIVE":"STOCK",pid,stamp.c_str(),threads,ms);fflush(stdout);return on;
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
  require(argc==3||argc==4,"Usage: gxr-whitening PID status|preflight|off|hold OR gxr-whitening PID run SECONDS(1..60)");
  char* end=nullptr;long id=strtol(argv[1],&end,10);require(end&&!*end&&id>1&&id<INT32_MAX,"Invalid PID");pid=id;
  std::string mode=argv[2];require(mode=="status"||mode=="preflight"||mode=="off"||mode=="run"||mode=="hold","Invalid mode");
  require((mode=="run")==(argc==4),"SECONDS required only for run");
  stamp=starttime(pid);
  if(mode!="run"&&mode!="hold"){change(pid,mode,stamp);return 0;}
  bool hold=mode=="hold";long seconds=0;
  if(hold)armSupervisorDeath(requestStop);
  if(!hold){seconds=strtol(argv[3],&end,10);require(end&&!*end&&seconds>=1&&seconds<=60,"Invalid duration");}
  Lock runLock("gxr-whitening-run-v1");
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
   stockReads=!memcmp(words.data(),ORIGINAL,sizeof(ORIGINAL))?stockReads+1:0;
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
