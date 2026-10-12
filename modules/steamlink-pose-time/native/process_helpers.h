static T monotonic(){timespec t{};require(clock_gettime(CLOCK_MONOTONIC,&t)==0,"clock_gettime failed");return t.tv_sec*1000000000LL+t.tv_nsec;}
struct OperationLock {
 bool owned=false;
 OperationLock(){const T deadline=monotonic()+4000000000LL;for(;;){if(mkdir("/dev/gxr_openxr_transaction",0700)==0){owned=true;return;}require(errno==EEXIST,"OpenXR operation lock failed");require(monotonic()<deadline,"Another OpenXR module operation remains active");usleep(20000);}}
 ~OperationLock(){if(owned)rmdir("/dev/gxr_openxr_transaction");}
};
static void putFile(const std::string& path,const std::string& value){
 int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);require(fd>=0,"Cannot write state");
 const bool ok=write(fd,value.data(),value.size())==static_cast<ssize_t>(value.size());close(fd);require(ok,"State write failed");
}
static void pwriteAll(int fd,uintptr_t addr,const void* data,size_t n){require(pwrite(fd,data,n,addr)==static_cast<ssize_t>(n),"Private sample write failed");}
static user_pt_regs regs(pid_t tid){user_pt_regs r{};iovec io{&r,sizeof(r)};require(ptrace(PTRACE_GETREGSET,tid,reinterpret_cast<void*>(NT_PRSTATUS),&io)==0&&io.iov_len==sizeof(r),"Cannot read registers");return r;}
static void regs(pid_t tid,user_pt_regs& r){iovec io{&r,sizeof(r)};require(ptrace(PTRACE_SETREGSET,tid,reinterpret_cast<void*>(NT_PRSTATUS),&io)==0,"Cannot restore registers");}
static uintptr_t syscallSite(pid_t pid,Memory& mem){
 std::istringstream lines(read(proc(pid,"maps")));std::string line;
 while(std::getline(lines,line)){unsigned long lo,hi,off;char perm[5]{};int used=0;
  if(sscanf(line.c_str(),"%lx-%lx %4s %lx %*s %*s %n",&lo,&hi,perm,&off,&used)!=4||strcmp(perm,"r-xp"))continue;
  auto file=line.substr(used);if(file!="/apex/com.android.runtime/lib64/bionic/libc.so")continue;
  require(hi-lo<4*1024*1024,"Unexpected libc mapping");auto bytes=mem.bytes(lo,hi-lo);
  for(size_t i=0;i+4<=bytes.size();i+=4){uint32_t w;memcpy(&w,bytes.data()+i,4);if(w==0xd4000001)return lo+i;}
 }
 throw std::runtime_error("No verified libc svc instruction");
}
// Execute exactly one kernel syscall on a stopped app thread, then restore all
// general registers. The svc instruction is borrowed read-only from mapped libc.
static uint64_t remoteSyscall(pid_t tid,uintptr_t svc,uint64_t number,std::array<uint64_t,6> args){
 auto saved=regs(tid),r=saved;r.pc=svc;r.regs[8]=number;for(size_t i=0;i<6;++i)r.regs[i]=args[i];
 regs(tid,r);bool running=false;
 try{
  require(ptrace(PTRACE_SINGLESTEP,tid,nullptr,nullptr)==0,"Syscall single step failed");running=true;
  const T deadline=monotonic()+2000000000LL;int status=0;
  for(;;){pid_t got=waitpid(tid,&status,__WALL|WNOHANG);require(got>=0,"Syscall wait failed");if(got==tid)break;
   require(monotonic()<deadline,"Syscall step timeout");usleep(1000);}
  running=false;require(WIFSTOPPED(status)&&WSTOPSIG(status)==SIGTRAP,"Unexpected syscall signal");
  r=regs(tid);require(r.pc==svc+4,"Unexpected syscall return PC");const uint64_t result=r.regs[0];regs(tid,saved);return result;
 }catch(...){
  if(running){ptrace(PTRACE_INTERRUPT,tid,nullptr,nullptr);int s=0;waitpid(tid,&s,__WALL);}
  try{regs(tid,saved);}catch(...){fprintf(stderr,"Register restoration failed; terminate this app\n");}
  throw;
 }
}
