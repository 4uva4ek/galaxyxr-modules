// Own-child fixture only: no service discovery, target originals or module access.
extern "C" void tail_fixture(uintptr_t code,void* predictor,float* result);
asm(".text\n.balign 4\n.global tail_fixture\ntail_fixture:\n"
 "stp x19,x20,[sp,#-96]!\nstr x30,[sp,#16]\nstp d8,d9,[sp,#32]\nstp d10,d11,[sp,#48]\nstp d12,d13,[sp,#64]\nstp d14,d15,[sp,#80]\n"
 "mov x19,x2\nmov x20,x1\nldr d25,[x20,#0x500]\nldr s26,[x20,#0x508]\nadd x8,x20,#0x500\nldur d27,[x8,#12]\nldr s24,[x20,#0x514]\nldr d28,[x20,#0x518]\nldr s29,[x20,#0x520]\n"
 "ldr d22,[x20,#0x530]\nfmov s9,wzr\nldr s23,[x20,#0x538]\nldr s31,[x20,#0x53c]\nblr x0\nstr d22,[x19]\nstr s9,[x19,#8]\n"
 "ldp d8,d9,[sp,#32]\nldp d10,d11,[sp,#48]\nldp d12,d13,[sp,#64]\nldp d14,d15,[sp,#80]\nldr x30,[sp,#16]\nldp x19,x20,[sp],#96\nret\n");
static void selftest(){
 alarm(25);pid_t child=-1,owner=-1;void* area=MAP_FAILED;int cmd[2]{-1,-1},reply[2]{-1,-1},event[2]{-1,-1};
 auto closefd=[](int& f){if(f>=0){close(f);f=-1;}};
 auto cleanup=[&](){for(pid_t p:{owner,child})if(p>0){kill(p,SIGKILL);while(waitpid(p,nullptr,0)<0&&errno==EINTR){}}for(int* p:{cmd,reply,event}){closefd(p[0]);closefd(p[1]);}if(area!=MAP_FAILED)munmap(area,0x100000);alarm(0);};
 try{
  area=mmap(nullptr,0x100000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);require(area!=MAP_FAILED,"Fixture mmap");
  uint32_t instructions[]={ORIGINAL,0xd65f03c0};memcpy(static_cast<char*>(area)+SITE,instructions,sizeof(instructions));
  __builtin___clear_cache(static_cast<char*>(area),static_cast<char*>(area)+0x100000);require(mprotect(area,0x100000,PROT_READ|PROT_EXEC)==0,"Fixture RX");
  alignas(16) unsigned char pred[0x600]{};double dt=.01;float gravity[]={0,0,-9.81f},dv[]={.02f,-.03f,.1381f},R[]={1,0,0,0,1,0,0,0,1},vh[]={1,2,3,.015f};
  memcpy(pred+0x88,&dt,8);memcpy(pred+0x48,gravity,12);memcpy(pred+0xb4,dv,12);memcpy(pred+0x500,R,36);memcpy(pred+0x530,vh,16);
  require(pipe2(cmd,O_CLOEXEC)==0&&pipe2(reply,O_CLOEXEC)==0,"Fixture pipe");pid_t parent=getpid();child=fork();require(child>=0,"Fixture fork");
  if(!child){
   if(prctl(PR_SET_PDEATHSIG,SIGKILL)<0||getppid()!=parent)_exit(80);close(cmd[1]);close(reply[0]);
   for(;;){char c;ssize_t n=::read(cmd[0],&c,1);if(n!=1)_exit(0);float out[3];tail_fixture(reinterpret_cast<uintptr_t>(area)+SITE,pred,out);if(::write(reply[1],out,sizeof(out))!=sizeof(out))_exit(81);}
  }
  closefd(cmd[0]);closefd(reply[1]);Target t{child,starttime(child),"",{reinterpret_cast<uintptr_t>(area),reinterpret_cast<uintptr_t>(area)+0x100000,""},0};Memory mem(child);
  auto query=[&](bool active){
   require(starttime(child)==t.stamp&&read(proc(child,"status")).find("TracerPid:\t0\n")!=std::string::npos,"Fixture trace/identity");
   char c='Q';require(::write(cmd[1],&c,1)==1,"Fixture command");pollfd p{reply[0],POLLIN|POLLHUP,0};require(poll(&p,1,2000)>0,"Fixture response timeout");float got[3];require(::read(reply[0],got,sizeof(got))==sizeof(got),"Fixture response");float expected[]={active?1.03f:1.f,active?1.955f:2.f,active?3.06f:3.f};
   for(unsigned i=0;i<3;++i)require(std::abs(got[i]-expected[i])<2e-6,"Fixture payload arithmetic");
  };
  query(false);
  {
   Frozen f;f.freeze(child);auto saved=getRegs(child);int savedNr=getSyscall(child);uintptr_t svc=svcSite(child,mem),want=hole(child,t.map.base);
   auto occupied=syscallOnce(child,svc,222,{t.map.base,SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|0x100000,uint64_t(-1),0});require(int64_t(occupied)==-EEXIST,"NOREPLACE did not refuse occupied range");
   t.cave=syscallOnce(child,svc,222,{want,SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|0x100000,uint64_t(-1),0});require(t.cave==want,"Fixture nearby allocation");
   auto b=code(t);mem.put(t.cave,b.data(),b.size());executableClone(t,mem,svc);mem.put(t.cave,b.data(),DATA);
   auto after=getRegs(child);require(((saved.pstate^after.pstate)&~(1ULL<<21))==0,"Architectural PSTATE changed");
   // TIF_SINGLESTEP keeps debugger SS set until PTRACE_DETACH; test its removal below.
   after.pstate=saved.pstate;require(!memcmp(&saved,&after,sizeof(saved))&&savedNr==getSyscall(child),"Remote syscall registers not restored");
   uint32_t jump=branch(t.map.base+SITE,t.cave);mem.put(t.map.base+SITE,&jump,4);require(f.release(),"Fixture detach");
  }
  query(false);
  {Frozen f;f.freeze(child);require((getRegs(child).pstate&(1ULL<<21))==0,"Debugger SS survived detach");require(f.release(),"Post-step audit detach");}
  auto config=[&](unsigned enabled){Frozen f;f.freeze(child);uint64_t value=0x3f80000000000000ULL|enabled;mem.put(t.cave+DATA+8,&value,8);require(f.release(),"Fixture config detach");};
  config(1);query(true);config(0);query(false);config(1);query(true);
  require(*reinterpret_cast<uint32_t*>(static_cast<char*>(area)+SITE)==ORIGINAL,"Patch escaped child private mapping");
  auto restoreFixture=[&](){Frozen f;f.freeze(child);mem.put(t.map.base+SITE,&ORIGINAL,4);require(f.release(),"Fixture restore detach");return true;};
  require(pipe2(event,O_CLOEXEC)==0,"Fixture guardian pipe");owner=fork();require(owner>=0,"Fixture owner fork");
  if(!owner){if(prctl(PR_SET_PDEATHSIG,SIGKILL)<0||getppid()!=parent)_exit(82);close(event[0]);usleep(100000);kill(getpid(),SIGKILL);_exit(83);}
  closefd(event[1]);auto ownerStamp=starttime(owner);int fd=event[0];event[0]=-1;require(guardianWait(fd,owner,ownerStamp,restoreFixture,2000)==0,"Fixture guardian failed");
  int st;require(waitpid(owner,&st,0)==owner&&WIFSIGNALED(st)&&WTERMSIG(st)==SIGKILL,"Fixture owner-death path");owner=-1;query(false);
  closefd(cmd[1]);require(waitpid(child,&st,0)==child&&WIFEXITED(st)&&WEXITSTATUS(st)==0,"Fixture child exit");child=-1;
  cleanup();puts("SELFTEST PASS: owned child only; NOREPLACE, remote mmap/read-only file RX clone, full register+syscall restore, RX execution, mode0/M1/mode0, private COW, guardian EOF rollback, detach verified");
 }catch(...){cleanup();throw;}
}
