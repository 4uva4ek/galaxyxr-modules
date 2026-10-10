#pragma once
#include "compatibility.generated.h"
// Pure validator. Only two complete states of the correction and the exact
// single-instruction extrapolation change are allowed; never skip a code range.
// Returns correction bit 1 / extrapolation bit 2, or -1 for unknown/partial code.
static inline int gxr_consumer_state(const unsigned char* bytes) {
    unsigned matched=0;
    for(unsigned i=0;i<sizeof(GXR_SITES)/sizeof(*GXR_SITES);++i) {
        unsigned word=0,stock=0;
        __builtin_memcpy(&word,bytes+GXR_SITES[i]-0x1b359c,4);
        __builtin_memcpy(&stock,GXR_CONSUMER+GXR_SITES[i]-0x1b359c,4);
        if(word==GXR_PATCHED[i]) ++matched;
        else if(word!=stock) return -1;
    }
    if(matched && matched!=sizeof(GXR_SITES)/sizeof(*GXR_SITES)) return -1;
    unsigned gate=0;__builtin_memcpy(&gate,bytes+0x1b3860-0x1b359c,4);
    if(gate!=0x3400038a && gate!=0xd503201f) return -1;
    for(unsigned i=0;i<sizeof(GXR_CONSUMER);++i) {
        bool owned=i>=0x1b3860-0x1b359c && i<0x1b3864-0x1b359c;
        for(unsigned site:GXR_SITES) owned|=i>=site-0x1b359c && i<site-0x1b359c+4;
        if(!owned && bytes[i]!=GXR_CONSUMER[i]) return -1;
    }
    return (matched?1:0)|(gate==0xd503201f?2:0);
}
static inline bool gxr_payload_valid(const unsigned char* bytes, bool active) {
    bool zero=true;
    for(unsigned i=0;i<0x520;++i) zero&=bytes[i]==0;
    if(zero) return !active;
    for(unsigned i=0;i<0x520;++i) {
        unsigned char expected=i<sizeof(GXR_PAYLOAD)?GXR_PAYLOAD[i]:0;
        if(i==GXR_ENABLED) expected=active?1:0;
        if(bytes[i]!=expected) return false;
    }
    return true;
}
