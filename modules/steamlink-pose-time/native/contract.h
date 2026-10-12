#pragma once
#include <stdint.h>
#include <stddef.h>
using U=uint32_t;using T=int64_t;
constexpr uint64_t MAGIC=0x31544d4954585247ULL;
constexpr size_t DATA=0x8000, ALLOCATION=0x18000;
struct Scope {U tid,active;T stamp[2];U seen[2],bad;};
struct Sample {T stamp,receipt;unsigned char pose[84];};
struct Hand {Sample samples[16];U next,count,calibrated,pad;T lastCalibration,sum,anchor,lastStamp;int32_t lastOffset;U changed;uint64_t matched,unmatched,ambiguous,applied;};
struct Control {
 uint64_t magic,consume,state,submit,clock;
 U enabled,lock;uint64_t calls,scopes,conflicts,invalid,packets,faults;
 Scope scope[16];Hand hand[2];
};
static_assert(sizeof(Control)<ALLOCATION-DATA,"allocation");
