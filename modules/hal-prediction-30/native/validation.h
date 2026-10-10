// Pure validator, tested on Windows with the same code used on Android.
// Inputs are PRIVATE COPIES of the full RX range [0x10000,0x22000).
static unsigned read_word(const unsigned char* p) {
 return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);
}
static void write_word(unsigned char* p,unsigned value) {
 for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(value>>(8*i));
}
static int validate_rx(unsigned char* live,const unsigned char* stock) {
 constexpr unsigned begin=0x10000,size=0x12000;
 unsigned hook=read_word(live+SITE-begin);
 if(hook!=ORIGINAL&&hook!=PATCHED)return -1;
 bool on=hook==PATCHED;
 write_word(live+SITE-begin,ORIGINAL);
 bool zero=true,ours=true;
 for(unsigned i=0;i<sizeof(PAYLOAD);++i){
  zero=zero&&live[CAVE-begin+i]==0;
  ours=ours&&live[CAVE-begin+i]==PAYLOAD[i];
 }
 if((!zero&&!ours)||(on&&!ours))return -2;
 for(unsigned i=0;i<sizeof(PAYLOAD);++i)live[CAVE-begin+i]=0;
 // Ownership contract, independent of the Haptic version or instruction bytes.
 // Haptic owns nativePerformHapticFeedback [0x122ac,0x12914).
 // Its own validator is responsible for that function. This does not authenticate
 // who wrote the bytes: ANY changes confined to this range are tolerated here.
 // Never normalize or restore that range in the target process.
 constexpr unsigned haptic_begin=0x122ac-begin,haptic_end=0x12914-begin;
 static_assert(SITE<0x122ac || SITE>=0x12914,"Prediction hook overlaps Haptic");
 static_assert(CAVE>=0x12914 || CAVE+sizeof(PAYLOAD)<=0x122ac,"Payload overlaps Haptic");
 for(unsigned i=0;i<size;++i) {
  if(i>=haptic_begin&&i<haptic_end)continue;
  if(live[i]!=stock[i])return -4;
 }
 return on?1:0;
}
