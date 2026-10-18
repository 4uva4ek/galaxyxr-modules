#pragma once
// Exact allowlist; never modifies target memory, only the caller's RX snapshot.
extern "C" bool normalize_known_words(unsigned char* live,const unsigned char* disk,unsigned long n,unsigned expectedTail){
 if(n<0x72c60)return false;
 auto word=[](const unsigned char* p)->unsigned {return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);};
 if(word(disk+0x723cc)!=0x5400050a||word(disk+0x72c5c)!=0x1f1f5d29)return false;
 const unsigned cache=word(live+0x723cc),tail=word(live+0x72c5c);
 if(cache!=0x5400050a&&cache!=0xd503201f)return false;
 if(tail!=0x1f1f5d29&&tail!=expectedTail)return false;
 for(unsigned i=0;i<4;++i){live[0x723cc+i]=disk[0x723cc+i];live[0x72c5c+i]=disk[0x72c5c+i];}
 return true;
}
