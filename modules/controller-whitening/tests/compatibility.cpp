#define main module_entry
#include "../native/main.cpp"
#undef main
#include <sys/mman.h>
static Map mapped(const std::string& disk) {
 void* p=mmap(nullptr,disk.size(),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 require(p!=MAP_FAILED,"fixture allocation");memcpy(p,disk.data(),disk.size());
 return {(uintptr_t)p,(uintptr_t)p+disk.size(),"fixture"};
}
int main(){try{
 auto disk=read("/vendor/lib64/libsctrl.so");auto m=mapped(disk);Memory mem(getpid());
 auto check=[&](){return validate(mem,m,disk);};
 require(check()==0,"stock");memcpy((void*)(m.base+SITE),PATCHED,12);require(check()==1,"whitening");
 uint32_t nop=0xd503201f;memcpy((void*)(m.base+0x5cd750),&nop,4);
 bool rejected=false;try{check();}catch(...){rejected=true;}require(rejected,"partial wait accepted");
 memcpy((void*)(m.base+0x5cd79c),&nop,4);require(check()==1,"both patches");
 memcpy((void*)(m.base+SITE),ORIGINAL,12);require(check()==0,"whitening restore while wait active");
 *(unsigned char*)(m.base+0x5cd7a0)^=1;rejected=false;try{check();}catch(...){rejected=true;}require(rejected,"foreign change accepted");
 puts("PASS whitening: stock, own, peer, both, restore with peer, partial and foreign rejection");
return 0;}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
