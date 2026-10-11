#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <limits>
extern "C" void run_tuning(float*,float,float,int);
int main(){int count=0;for(int pcm: {0,1,80}) for(int i=-100;i<=1100;i++) {float a=i/1000.f, o[2];run_tuning(o,a,7.f,pcm);float want=a;float f=(pcm||!(a>0))?7.f:2.f;if(o[0]!=want||o[1]!=f)return 1;count++;} for(float a: {std::numeric_limits<float>::quiet_NaN(),-0.f}) {float o[2];run_tuning(o,a,7.f,0);if((std::isnan(a)?!std::isnan(o[0]):o[0]!=a)||o[1]!=7.f)return 2;}printf("PASS %d ARM64 tuning cases plus NaN/zero; PCM unchanged\n",count);}
