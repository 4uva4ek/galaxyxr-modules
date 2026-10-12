// Broker-owned cache bypass. Only private service memory changes; originals remain untouched.
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/system_properties.h>
#include <sys/mman.h>
#include <poll.h>
#include <stddef.h>
#include "supervision.h"
#include <sys/stat.h>
#include <sys/vfs.h>
static constexpr uintptr_t SITE=0x723cc, RX_END=0xea890;
static constexpr uint32_t STOCK=0x5400050a, PATCHED=0xd503201f;
static constexpr const char* SERVICE="/vendor/bin/hw/vendor.samsung.hardware.secxrcontroller@1.0-service";
static constexpr const char* LIB_HASH="36120385f25c61f5c2d832d2ac2ebf2179dea1ba8142beeb1001994b7f0d22c0";
static volatile sig_atomic_t stopRequested=0;
static void requestStop(int){stopRequested=1;}
static pid_t guardianPid=-1;
static auto activationDeadline=std::chrono::steady_clock::time_point::min();
static std::string activationGeneration;
static std::string gateFile(const char* path){
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);require(fd>=0,"Soft-restart gate file absent");
 struct stat st{};bool ok=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_size>0&&st.st_size<128;
 char data[128]{};ssize_t n=ok?::read(fd,data,sizeof(data)-1):-1;close(fd);require(ok&&n>0,"Soft-restart gate file invalid");
 std::string value(data,static_cast<size_t>(n));if(!value.empty()&&value.back()=='\n')value.pop_back();return value;
}
static void lateLoadMode(){
 int p[2];require(pipe2(p,O_CLOEXEC)==0,"KernelSU mode pipe failed");pid_t child=fork();
 if(child<0){close(p[0]);close(p[1]);throw std::runtime_error("KernelSU mode fork failed");}
 if(!child){dup2(p[1],STDOUT_FILENO);int null=open("/dev/null",O_WRONLY|O_CLOEXEC);if(null>=0)dup2(null,STDERR_FILENO);close(p[0]);close(p[1]);execl("/data/adb/ksud","ksud","debug","info",static_cast<char*>(nullptr));_exit(127);}
 close(p[1]);std::string output;bool ended=false;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
 while(std::chrono::steady_clock::now()<deadline&&output.size()<4096){pollfd item{p[0],POLLIN|POLLHUP,0};int ready=poll(&item,1,100);if(ready<0&&errno==EINTR)continue;if(ready<=0)continue;char buffer[512];ssize_t n=::read(p[0],buffer,sizeof(buffer));if(n==0){ended=true;break;}if(n<0)break;output.append(buffer,static_cast<size_t>(n));}
 close(p[0]);if(!ended)kill(child,SIGKILL);int status=0;while(waitpid(child,&status,0)<0&&errno==EINTR){}
 require(ended&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"KernelSU runtime mode cannot be verified");
 std::istringstream lines(output);std::string line;bool late=false,mode=false;while(std::getline(lines,line)){late|=line=="late_load: true";mode|=line=="runtime_mode: late-load";}
 require(late&&mode,"KernelSU is not late-load; cold-boot activation blocked");
}
static std::string readyGeneration(bool verifyMode){
 struct statfs fs{};require(statfs("/dev",&fs)==0&&fs.f_type==0x01021994,"/dev is not tmpfs");
 char boot[PROP_VALUE_MAX]{};__system_property_get("sys.boot_completed",boot);require(!strcmp(boot,"1"),"Android boot not complete");
 const char* stage="/dev/ksu-restart-stage";struct stat st{};require(lstat(stage,&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==0,"Soft-restart stage absent or unsafe");
 for(const char* name:{"busy","error"}){std::string path=std::string(stage)+"/"+name;errno=0;require(lstat(path.c_str(),&st)<0&&errno==ENOENT,"Soft restart busy or failed");}
 require(gateFile("/dev/ksu-restart-stage/ready")=="ready","Soft-restart ready marker invalid");
 auto generation=gateFile("/dev/ksu-restart-stage/generation");require(generation.size()==36,"Invalid restart generation");
 for(size_t i=0;i<generation.size();++i){bool dash=i==8||i==13||i==18||i==23;char c=generation[i];require(dash?c=='-':((c>='0'&&c<='9')||(c>='a'&&c<='f')),"Invalid restart generation");}
 if(verifyMode)lateLoadMode();
 require(gateFile("/dev/ksu-restart-stage/generation")==generation&&gateFile("/dev/ksu-restart-stage/ready")=="ready","Restart generation changed");
 for(const char* name:{"busy","error"}){std::string path=std::string(stage)+"/"+name;errno=0;require(lstat(path.c_str(),&st)<0&&errno==ENOENT,"Soft restart busy or failed");}
 return generation;
}
struct Lock {
 int fd=-1;
 explicit Lock(const char* name,int waitms=0){
  fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);require(fd>=0,"Lock socket failed");
  sockaddr_un a{};a.sun_family=AF_UNIX;size_t n=strlen(name);require(n+1<sizeof(a.sun_path),"Lock name too long");memcpy(a.sun_path+1,name,n);
  auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(waitms);
  while(bind(fd,reinterpret_cast<sockaddr*>(&a),offsetof(sockaddr_un,sun_path)+1+n)<0){
   if(errno!=EADDRINUSE||std::chrono::steady_clock::now()>=until){close(fd);fd=-1;throw std::runtime_error("Another service experiment transaction is running");}usleep(10000);
  }
 }
 ~Lock(){if(fd>=0)close(fd);}
};
static void activationAllowed(){
 require(!stopRequested,"Stop requested before activation");
 require(std::chrono::steady_clock::now()<activationDeadline,"Activation deadline expired");
 require(guardianPid>1&&kill(guardianPid,0)==0,"Recovery guardian unavailable");
 int status=0;require(waitpid(guardianPid,&status,WNOHANG)==0,"Recovery guardian exited");
 require(!activationGeneration.empty()&&readyGeneration(false)==activationGeneration,"Soft-restart gate changed before activation");
}
static int validate(Memory& mem,const Map& m,const std::string& disk){
 require(m.base+RX_END<=m.end&&disk.size()>=RX_END,"RX extent mismatch");
 auto live=mem.bytes(m.base,RX_END);
 uint32_t word=0;memcpy(&word,live.data()+SITE,4);
 require(word==STOCK||word==PATCHED,"Unknown cache branch instruction");
 uint32_t diskWord=0;memcpy(&diskWord,disk.data()+SITE,4);require(diskWord==STOCK,"Original library branch differs");
 memcpy(live.data()+SITE,&STOCK,4);
 require(!memcmp(live.data(),disk.data(),live.size()),"Client RX differs outside cache branch");
 return word==PATCHED;
}
static bool change(pid_t pid,bool on,bool statusOnly,const std::string& expectedStamp){
 Lock transaction("gxr-whitening-transaction-v1",1500);
 char model[PROP_VALUE_MAX]{};__system_property_get("ro.product.model",model);require(!strcmp(model,"SM-I610"),"Wrong headset model");
 auto identity=read(proc(pid,"cmdline"));require(std::string(identity.c_str())==SERVICE,"Wrong service process");
 auto stamp=starttime(pid);require(stamp==expectedStamp,"Service restarted; refusing reused PID");
 auto m=mapping(pid);require(hash(m.file)==LIB_HASH,"Wrong client library SHA256");auto disk=read(m.file);Memory mem(pid);
 int before=validate(mem,m,disk);
 if(statusOnly){printf("%s pid=%d start=%s model=%s RX=VERIFIED branch=0x%08x\n",before?"CACHE_BYPASS_ACTIVE":"STOCK",pid,stamp.c_str(),model,before?PATCHED:STOCK);fflush(stdout);return before;}
 if(on)activationAllowed();require(!on||!before,"Already active; restore first");
 if(on==bool(before)){puts("STOCK verified; no memory write needed");fflush(stdout);return false;}
 for(int attempt=0;attempt<8;++attempt){
  auto started=std::chrono::steady_clock::now();Frozen frozen;frozen.freeze(pid);
  require(starttime(pid)==stamp&&read(proc(pid,"cmdline"))==identity,"Target identity changed");
  auto again=mapping(pid);require(again.base==m.base&&again.end==m.end,"Target mapping changed");
  // POKETEXT preserves the neighboring instruction in the aligned8-byte word.
  if(frozen.pcInRange(m.base+(SITE&~uintptr_t(7)),m.base+(SITE&~uintptr_t(7))+8)){
   require(frozen.release(),"Thread release failed");usleep(5000);continue;
  }
  before=validate(mem,m,disk);require(before!=int(on),"Concurrent state change");if(on)activationAllowed();
  const uint32_t word=on?PATCHED:STOCK;
  try{mem.put(m.base+SITE,&word,4);require(validate(mem,m,disk)==int(on),"Patch verification failed");}
  catch(...){try{mem.put(m.base+SITE,&STOCK,4);require(!validate(mem,m,disk),"Rollback verification failed");}
   catch(...){fprintf(stderr,"RESTORE ERROR: disable HAL Cache Bypass, fully remove headset power and reboot.\n");}throw;}
  size_t count=frozen.threads.size();require(frozen.release(),"Thread release failed");
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
  printf("%s pid=%d start=%s threads=%zu freeze_ms=%.3f verified PRIVATE_RAM_ONLY\n",on?"CACHE_BYPASS_ACTIVE":"STOCK",pid,stamp.c_str(),count,ms);fflush(stdout);return on;
 }
 throw std::runtime_error("Thread remained in patch word; no change applied");
}
static bool restore(pid_t pid,const std::string& stamp){
 // A gone/replaced process has discarded the private patch. Never attach to its replacement.
 try{if(starttime(pid)!=stamp){puts("Original service instance ended; private patch no longer applicable");return true;}}
 catch(...){if(Frozen::gone(pid)){puts("Original service exited; private patch discarded");return true;}}
 for(int i=0;i<3;++i){try{change(pid,false,false,stamp);return true;}
  catch(const std::exception& e){fprintf(stderr,"Restore attempt%d: %s\n",i+1,e.what());usleep(200000);}}
 fprintf(stderr,"RESTORE NOT VERIFIED: disable HAL Cache Bypass, fully remove headset power and reboot. An enabled module may reapply.\n");return false;
}
static int selftest(){
 // Child-owned anonymous private executable page only. No service lookup.
 const size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE));
 void* code=mmap(nullptr,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);require(code!=MAP_FAILED,"Selftest mmap");
 const uint32_t words[]={0x52800020,0x14000002,0x52800040,0xd65f03c0}; //return1; branch skip return2; return
 memcpy(code,words,sizeof(words));__builtin___clear_cache(static_cast<char*>(code),static_cast<char*>(code)+sizeof(words));
 require(mprotect(code,page,PROT_READ|PROT_EXEC)==0,"Selftest RX");int to[2],from[2];require(pipe2(to,O_CLOEXEC)==0&&pipe2(from,O_CLOEXEC)==0,"Selftest pipes");
 pid_t child=fork();require(child>=0,"Selftest fork");
 if(!child){close(to[1]);close(from[0]);char command=0;while(::read(to[0],&command,1)==1){if(command=='Q')break;int value=reinterpret_cast<int(*)()>(code)();if(::write(from[1],&value,sizeof(value))!=sizeof(value))_exit(4);} _exit(0);}
 close(to[0]);close(from[1]);
 try{
  auto call=[&](){char go='G';require(::write(to[1],&go,1)==1,"Selftest request");pollfd p{from[0],POLLIN,0};require(poll(&p,1,1000)==1,"Selftest response timeout");int value=0;require(::read(from[0],&value,4)==4,"Selftest response");return value;};
  require(call()==1,"Selftest baseline return");Memory mem(child);const uintptr_t site=reinterpret_cast<uintptr_t>(code)+4;
  for(uint32_t instruction:{PATCHED,words[1]}){Frozen frozen;frozen.freeze(child);frozen.requireNoPC(site-4,site+4);mem.put(site,&instruction,4);
   uint32_t check[4]{};mem.get(site-4,check,sizeof(check));require(check[0]==words[0]&&check[1]==instruction&&check[2]==words[2]&&check[3]==words[3],"Selftest adjacent instruction corruption");require(frozen.release(),"Selftest detach");require(call()==(instruction==PATCHED?2:1),"Selftest execution/cache result");}
  char quit='Q';require(::write(to[1],&quit,1)==1,"Selftest quit");int status=0;require(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"Selftest child exit");
  close(to[1]);close(from[0]);munmap(code,page);puts("PASS owned-child RX POKETEXT, instruction preservation, execution after patch/restore, all-thread detach; no service operations");return 0;
 }catch(...){kill(child,SIGKILL);waitpid(child,nullptr,0);close(to[1]);close(from[0]);munmap(code,page);throw;}
}
int main(int argc,char** argv){
 signal(SIGPIPE,SIG_IGN);for(int output:{STDOUT_FILENO,STDERR_FILENO}){int flags=fcntl(output,F_GETFL);if(flags>=0)(void)fcntl(output,F_SETFL,flags|O_NONBLOCK);}
 pid_t pid=0,guardian=-1;int pipefd[2]{-1,-1};bool attempted=false;std::string stamp;
 try{
  if(argc==2&&!strcmp(argv[1],"--selftest"))return selftest();
  require(argc==3||argc==4,"Usage: gxr-cache-bypass PID status|preflight|check-active|off|hold OR PID run|on SECONDS(1..180); --selftest");
  if(argc==3&&(!strcmp(argv[1],"--check-active")||!strcmp(argv[1],"--preflight"))){char* command=argv[1]+2;argv[1]=argv[2];argv[2]=command;}
  char* end=nullptr;long id=strtol(argv[1],&end,10);require(end&&!*end&&id>1&&id<INT32_MAX,"Invalid PID");pid=id;
  std::string mode=argv[2];require(mode=="status"||mode=="preflight"||mode=="check-active"||mode=="off"||mode=="hold"||mode=="run"||mode=="on","Invalid command");
  bool run=mode=="run"||mode=="on",hold=mode=="hold";require(run==(argc==4),"Duration required only for run/on");
  if(hold)armSupervisorDeath(requestStop);
  stamp=starttime(pid);
  if(!run&&!hold){bool active=change(pid,false,mode!="off",stamp);if(mode=="check-active")return active?0:3;if(mode=="preflight")return active?3:0;return 0;}
  try{activationGeneration=readyGeneration(true);}catch(const std::exception& e){printf("DISABLED - Waiting: %s; preference unchanged\n",e.what());return 0;}
  long seconds=0;if(run){seconds=strtol(argv[3],&end,10);require(end&&!*end&&seconds>=1&&seconds<=180,"Duration must be1..180seconds");}
  Lock owner("gxr-cache-bypass-run-v1");require(!change(pid,false,true,stamp),"Existing patch must be restored first");
  const pid_t parentPid=getpid();const auto parentStamp=starttime(parentPid);
  activationDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  require(pipe2(pipefd,O_CLOEXEC)==0,"Guardian pipe failed");guardian=fork();require(guardian>=0,"Guardian fork failed");
  if(!guardian){
   close(pipefd[1]);signal(SIGINT,SIG_IGN);signal(SIGTERM,SIG_IGN);signal(SIGHUP,SIG_IGN);
   pollfd p{pipefd[0],POLLIN|POLLHUP,0};int result;
   do{result=poll(&p,1,6000);}while(result<0&&errno==EINTR);char b=0;ssize_t n=result>0?::read(pipefd[0],&b,1):0;
   if(n==1&&b=='A'){do{result=poll(&p,1,hold?-1:int((seconds+3)*1000));}while(result<0&&errno==EINTR);b=0;n=result>0?::read(pipefd[0],&b,1):0;}
   close(pipefd[0]);if(n==1&&b=='S')_exit(0);
   if(result<=0){try{if(starttime(parentPid)==parentStamp)(void)kill(parentPid,SIGKILL);}catch(...){}usleep(100000);}
   fprintf(stderr,"GUARDIAN: parent ended/deadline reached; restoring original cache branch.\n");bool ok=restore(pid,stamp);fflush(nullptr);_exit(ok?0:2);
  }
  close(pipefd[0]);pipefd[0]=-1;guardianPid=guardian;
  printf("OWNER pid=%d guardian=%d target=%d mode=%s duration=%ld\n",getpid(),guardian,pid,hold?"HOLD":"BOUNDED",seconds);fflush(stdout);
  signal(SIGINT,requestStop);signal(SIGTERM,requestStop);signal(SIGHUP,requestStop);
  attempted=true;change(pid,true,false,stamp);char active='A';require(::write(pipefd[1],&active,1)==1,"Guardian acknowledgement failed");
  printf("READY CACHE_BYPASS_ACTIVE pid=%d start=%s\n",pid,stamp.c_str());fflush(stdout);
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);Memory monitoring(pid);auto m=mapping(pid);
  auto nextHealth=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  puts(hold?"HOLD: supervisor death or signal restores stock; disable HAL Cache Bypass for persistent rollback. Full power removal clears RAM but an enabled module may reapply.":"BOUNDED_RUN: signal owner or use PID off to restore; automatic timeout restore; disable HAL Cache Bypass before full reboot for persistent rollback.");fflush(stdout);
  while(!stopRequested&&(hold||std::chrono::steady_clock::now()<deadline)){
   try{if(readyGeneration(false)!=activationGeneration)throw std::runtime_error("Soft-restart generation changed");}
   catch(const std::exception& e){printf("DISABLED - Waiting: %s; restoring live patch; preference unchanged\n",e.what());fflush(stdout);break;}
   usleep(200000);require(starttime(pid)==stamp,"Service restarted");uint32_t word=0;monitoring.get(m.base+SITE,&word,4);require(word==STOCK||word==PATCHED,"Unexpected cache instruction");if(word==STOCK)break;
   if(std::chrono::steady_clock::now()>=nextHealth){
    int guardianStatus=0;require(kill(guardian,0)==0&&waitpid(guardian,&guardianStatus,WNOHANG)==0,"Recovery guardian died");
    require(std::string(read(proc(pid,"cmdline")).c_str())==SERVICE,"Service command changed");
    printf("HEALTH CACHE_BYPASS_ACTIVE pid=%d start=%s\n",pid,stamp.c_str());fflush(stdout);
    nextHealth=std::chrono::steady_clock::now()+std::chrono::seconds(2);
   }
  }
  require(restore(pid,stamp),"Timed restoration failed");attempted=false;char done='S';(void)!::write(pipefd[1],&done,1);close(pipefd[1]);pipefd[1]=-1;
  int status=0;require(waitpid(guardian,&status,0)==guardian,"Guardian wait failed");guardian=-1;require(WIFEXITED(status)&&WEXITSTATUS(status)==0,"Guardian failed");
  puts("DONE stock branch restored; original files unchanged. HAL Cache Bypass activation preference remains module-owned.");return 0;
 }catch(const std::exception& e){fprintf(stderr,"ERROR: %s (%s)\n",e.what(),strerror(errno));bool restored=!attempted||restore(pid,stamp);
  if(pipefd[1]>=0){if(restored){char done='S';(void)!::write(pipefd[1],&done,1);}close(pipefd[1]);}if(pipefd[0]>=0)close(pipefd[0]);
  if(guardian>0){int status=0;while(waitpid(guardian,&status,0)<0&&errno==EINTR){}}return 1;}
}
