// Firmware-profiled CPM relation -> runtime tracking-space velocities.
// Pose and timestamp remain untouched; subsequent runtime composition handles baseSpace.
using U = unsigned int;
struct V { float x,y,z; };
struct Q { float x,y,z,w; };
struct Relation { U flags; Q q; V p,v,w; };
static_assert(sizeof(Relation)==56,"Unexpected relation ABI");
#ifdef GXR_HOST_TEST
#define API extern "C" __declspec(dllexport)
API volatile U gxr_enabled=1;
extern "C" int _fltused=0;
#else
#define API extern "C" __attribute__((visibility("hidden")))
API volatile U gxr_enabled;
#endif
struct Geometry { float s,c; V p; };
// Inverse descriptor quaternion and descriptor translation in source axes.
static constexpr Geometry geometry[6]={
 { .1305261922f,.9914448614f,{0,0,-.02f}},
 {-.3611364f,.932513f,{0,0,0}},
 {0,1,{.02f,-.03695805370807648f,.03367643803358078f}},
 {0,1,{0,.01f,-.04f}},
 {0,1,{0,.01f,-.05f}},
 {-.3611364f,.932513f,{0,-.05854155495762825f,.05334347486495972f}}
};
static V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static V scale(V a,float b){return {a.x*b,a.y*b,a.z*b};}
static bool finite(V a){return __builtin_isfinite(a.x)&&__builtin_isfinite(a.y)&&__builtin_isfinite(a.z);}
__attribute__((noinline)) static V rotate(Q q,V a){V t=scale(cross({q.x,q.y,q.z},a),2);return add(a,add(scale(t,q.w),cross({q.x,q.y,q.z},t)));}
API void gxr_correct(Relation* r,U device,U input){
 if(!gxr_enabled||!r||(device!=2&&device!=3)||(input&0xffff)!=5)return;
 U index=(input>>16)-0x13;if(index>=6||!(r->flags&1))return;
 Q q=r->q;float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
 if(!(norm>.999f&&norm<1.001f))return;
 Geometry g=geometry[index];if(device==3)g.p.x=-g.p.x;
 // q_source = q_input * inverse(q_descriptor).
 q={q.x*g.c+q.w*g.s,q.y*g.c+q.z*g.s,q.z*g.c-q.y*g.s,q.w*g.c-q.x*g.s};
 const V angular=r->w;
 const bool have_w=(r->flags&0x40)&&finite(angular);
 if((r->flags&0x20)&&finite(r->v)){
   if(index==1) { V out=rotate(q,r->v);if(finite(out))r->v=out; }
   else if(have_w){V out=rotate(q,add(r->v,cross(angular,g.p)));if(finite(out))r->v=out;}
   else r->flags&=~0x20u; // Offset-point velocity cannot be established without omega.
 }
 if(have_w){V out=rotate(q,angular);if(finite(out))r->w=out;}
}
