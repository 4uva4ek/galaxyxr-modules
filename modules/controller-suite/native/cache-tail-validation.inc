#include "payload.h"
#include "rx_policy.h"
static uint32_t tailBranch(uintptr_t from,uintptr_t to){
 int64_t delta=int64_t(to)-int64_t(from);require(!(delta&3)&&delta>=-(1LL<<27)&&delta<(1LL<<27),"Tail branch range");
 return 0x14000000u|(uint32_t(delta/4)&0x03ffffffu);
}
static int validate(Memory& mem,const Map& m,const std::string& disk){
 require(m.base+RX_END<=m.end&&disk.size()>=RX_END,"RX extent mismatch");
 auto live=mem.bytes(m.base,RX_END);uint32_t cache=0,tail=0;
 memcpy(&cache,live.data()+SITE,4);memcpy(&tail,live.data()+0x72c5c,4);
 if(tail!=0x1f1f5d29){
  require((tail&0xfc000000u)==0x14000000u,"Unknown tail instruction");
  int64_t imm=tail&0x03ffffffu;if(imm&(1<<25))imm-=1LL<<26;
  uintptr_t cave=m.base+0x72c5c+imm*4;require(cave>=0x10000&&!(cave&0x3fff),"Tail alignment");
  std::vector<unsigned char> expected(PAYLOAD,PAYLOAD+0x4000);
  uint32_t back=tailBranch(cave+RETURN_OFFSET,m.base+0x72c60);memcpy(expected.data()+RETURN_OFFSET,&back,4);
  require(mem.bytes(cave,0x4000)==expected,"Tail payload differs from pinned build");
  uint64_t magic=0;mem.get(cave+0x4000,&magic,8);require(magic==0x31564c4941545847ULL,"Tail control identity");
 }
 require(normalize_known_words(live.data(),reinterpret_cast<const unsigned char*>(disk.data()),live.size(),tail),"Unknown cache/tail instruction");
 require(!memcmp(live.data(),disk.data(),live.size()),"Client RX differs outside verified cache/tail words");
 return cache==PATCHED;
}
static int compatSelftest(){
 const size_t size=0x120000;auto* raw=static_cast<unsigned char*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));require(raw!=MAP_FAILED,"Test allocation");
 uintptr_t base=(uintptr_t(raw)+0x3fff)&~uintptr_t(0x3fff),cave=base+0x100000;
 std::string disk(RX_END,'\0');uint32_t tail=0x1f1f5d29;memcpy(disk.data()+SITE,&STOCK,4);memcpy(disk.data()+0x72c5c,&tail,4);memcpy(reinterpret_cast<void*>(base),disk.data(),disk.size());
 Map m{base,base+RX_END,""};Memory mem(getpid());int count=0;
 auto check=[&](bool valid){bool ok=true;try{validate(mem,m,disk);}catch(...){ok=false;}require(ok==valid,"Compatibility selftest mismatch");++count;};
 check(true);memcpy(reinterpret_cast<void*>(base+SITE),&PATCHED,4);check(true);
 memcpy(reinterpret_cast<void*>(cave),PAYLOAD,sizeof(PAYLOAD));uint32_t back=tailBranch(cave+RETURN_OFFSET,base+0x72c60);memcpy(reinterpret_cast<void*>(cave+RETURN_OFFSET),&back,4);
 tail=tailBranch(base+0x72c5c,cave);memcpy(reinterpret_cast<void*>(base+0x72c5c),&tail,4);check(true);
 auto* bytes=reinterpret_cast<unsigned char*>(cave);bytes[80]^=1;check(false);bytes[80]^=1;
 bytes[RETURN_OFFSET]^=1;check(false);bytes[RETURN_OFFSET]^=1;
 bytes[0x4000]^=1;check(false);bytes[0x4000]^=1;
 auto* lib=reinterpret_cast<unsigned char*>(base);lib[100]^=1;check(false);lib[100]^=1;
 lib[SITE]^=1;check(false);lib[SITE]^=1;
 memcpy(reinterpret_cast<void*>(base+SITE),&STOCK,4);check(true);
 munmap(raw,size);printf("PASS compatibility validator %d owned-memory cases; no service writes\n",count);return 0;
}
