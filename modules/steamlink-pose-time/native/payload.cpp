// Observation-only source hooks; only wire per-pose time fields may change.
// 0.1.3: identical pose bytes under two labels resolve to the latest receipt instead of passing through.
#include "contract.h"
extern "C" __attribute__((section(".control"),visibility("hidden"))) Control gxr_control;
Control gxr_control={};
using Consume=bool(*)(void*,U,U,uint64_t*,void*);
using State=bool(*)(void*,void*,int);
using Submit=uintptr_t(*)(void*,void*,bool,bool);
static T now(){struct{long s,n;} ts{};register long x0 asm("x0")=1;register void* x1 asm("x1")=&ts;register long x8 asm("x8")=113;asm volatile("svc #0":"+r"(x0):"r"(x1),"r"(x8):"memory","cc");return x0==0?ts.s*1000000000LL+ts.n:0;}
static U tid(){register long x0 asm("x0");register long x8 asm("x8")=178;asm volatile("svc #0":"=r"(x0):"r"(x8):"memory","cc");return U(x0);}
static bool lock(){return !__atomic_exchange_n(&gxr_control.lock,1,__ATOMIC_ACQUIRE);}
static void unlock(){__atomic_store_n(&gxr_control.lock,0,__ATOMIC_RELEASE);}
static bool same(const unsigned char* a,const unsigned char* b){for(U i=0;i<84;++i)if(a[i]!=b[i])return false;return true;}
extern "C" __attribute__((section(".consume"),visibility("hidden")))
bool hook_consume(void* a,U device,U input,uint64_t* stamp,void* relation){
 auto& c=gxr_control;bool ok=reinterpret_cast<Consume>(c.consume)(a,device,input,stamp,relation);
 if(!c.enabled||!ok||!stamp||!relation||(device!=2&&device!=3)||(input&0xffff)!=5)return ok;
 const T t=*stamp;const U id=tid(),hand=device==3?0:1;
 while(!lock()){}
 {
  ++c.calls;for(auto& s:c.scope)if(s.tid==id&&s.active){
   if(t<=0||(*reinterpret_cast<U*>(relation)&0x33)!=0x33)s.bad=1;
   if(s.seen[hand]&&s.stamp[hand]!=t)s.bad=1;
   s.stamp[hand]=t;++s.seen[hand];break;
  }
  unlock();
 }
 return ok;
}
extern "C" __attribute__((section(".state"),visibility("hidden")))
bool hook_state(void* self,void* output,int handArg){
 auto& c=gxr_control;int slot=-1;U id=tid();
 if(c.enabled&&lock()){
  bool nested=false;for(auto& s:c.scope)if(s.tid==id&&s.active)nested=true;
  if(!nested)for(int i=0;i<16;++i)if(!c.scope[i].active){slot=i;c.scope[i]={};c.scope[i].tid=id;c.scope[i].active=1;++c.scopes;break;}
  unlock();
 }
 bool ok=reinterpret_cast<State>(c.state)(self,output,handArg);
 if(slot>=0){
  // Only the owner writes its scope while original runs; other hooks access under lock.
  while(!lock()){} auto& s=c.scope[slot];const T receipt=now();
  if(ok&&output&&!s.bad&&(!!s.seen[0]+!!s.seen[1])==1){
   U h=s.seen[0]?0:1;T t=s.stamp[h];auto* p=static_cast<unsigned char*>(output)+0x20;
   if(receipt>0&&t-receipt<100000000&&receipt-t<100000000&&p[0x36]==2){
    Hand& dest=c.hand[h];Sample& sample=dest.samples[dest.next++%16];sample.stamp=t;sample.receipt=receipt;
    for(U i=0;i<84;++i)sample.pose[i]=p[i];
   }else ++c.invalid;
  }else ++c.invalid;
  s.active=0;unlock();
 }
 return ok;
}
extern "C" __attribute__((section(".submit"),visibility("hidden")))
uintptr_t hook_submit(void* self,void* packet,bool a,bool b){
 auto& c=gxr_control;auto* bytes=static_cast<unsigned char*>(packet);
 if(c.enabled&&bytes&&*reinterpret_cast<U*>(bytes)==0x103&&*reinterpret_cast<uint16_t*>(bytes+4)>=0x168&&bytes[8]==10&&lock()){
  ++c.packets;
  // Bracket clock read around vendor microsecond clock. Wrap-safe 32-bit difference.
  const T t0=now();const U vendorNow=reinterpret_cast<U(*)()>(c.clock)();const T t1=now();
  const int32_t elapsed=static_cast<int32_t>(vendorNow-*reinterpret_cast<U*>(bytes+0x14));
  const T header=(t0+t1)/2-T(elapsed)*1000;
  if(t0<=0||t1<t0||t1-t0>100000||elapsed<0||elapsed>20000){++c.invalid;unlock();return reinterpret_cast<Submit>(c.submit)(self,packet,a,b);}
  for(U h=0;h<2;++h){
   auto* p=bytes+(h?0x118:0xc0);Hand& dest=c.hand[h];T stamp=0,receipt=-1;bool relabelled=false;
   if(*reinterpret_cast<int32_t*>(p+0x54)!=0||p[0x36]!=2){++dest.unmatched;continue;}
   // The producer can republish identical pose bytes under a newer slot label. The packet
   // is built from the newest GetControllerState for this hand, so the matching sample with
   // the latest receipt is the call that produced it; older identical samples only count.
   for(auto& s:dest.samples)if(s.stamp&&t1-s.receipt>=0&&t1-s.receipt<100000000&&same(s.pose,p)){
    if(stamp&&stamp!=s.stamp)relabelled=true;
    if(s.receipt>receipt){receipt=s.receipt;stamp=s.stamp;}
   }
   if(!stamp){++dest.unmatched;continue;}if(relabelled)++dest.ambiguous;++dest.matched;
   const T age=header-stamp;
   if(age< -100000000||age>100000000){++c.invalid;continue;}
   // Wire epoch = slot label minus the configured compensation (0 = raw label,
   // 28.82 ms = measured original label lead). No learned mean. Header clock converted
   // above; the source-age guard bounds the label to +/-100 ms of the header.
   dest.calibrated=1;
   T lead=c.lead;if(lead<0||lead>MAX_LEAD_NS)lead=DEFAULT_LEAD_NS;
   T offset=-(age+lead)/1000;
   *reinterpret_cast<int32_t*>(p+0x54)=static_cast<int32_t>(offset);
   dest.lastStamp=stamp;dest.lastOffset=static_cast<int32_t>(offset);++dest.applied;
   if(offset)++dest.changed;
  }unlock();
 }
 return reinterpret_cast<Submit>(c.submit)(self,packet,a,b);
}
