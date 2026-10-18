#pragma once
#include <sys/stat.h>
#include <sys/vfs.h>
namespace cachegate {
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
// A KernelSU soft restart writes a new generation, creates busy, removes ready and
// resets sys.boot_completed. These read-only checks never call ksud.
static bool restartStarted(){
 char boot[PROP_VALUE_MAX]{};__system_property_get("sys.boot_completed",boot);if(strcmp(boot,"1"))return true;
 struct stat st{};errno=0;return lstat("/dev/ksu-restart-stage/busy",&st)==0||errno!=ENOENT;
}
static bool restartPending(const std::string& generation){
 if(restartStarted())return true;
 struct stat st{};errno=0;if(lstat("/dev/ksu-restart-stage/error",&st)==0||errno!=ENOENT)return true;
 try{return gateFile("/dev/ksu-restart-stage/ready")!="ready"||gateFile("/dev/ksu-restart-stage/generation")!=generation;}
 catch(const std::exception&){return true;}
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
}
