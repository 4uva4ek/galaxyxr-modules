#pragma once
#include <stdint.h>
#include <stddef.h>
using U=uint32_t;using T=int64_t;
constexpr uint64_t MAGIC=0x31544d4954585247ULL;
constexpr size_t DATA=0x8000, ALLOCATION=0x18000;
// Compensation subtracted from the slot label before it goes on the wire. Measured
// 2026-10-12: the runtime labels a controller slot HAL target + 28.82 ms while its content
// is the pose at the target, so 28.82 ms is the original label lead (content epoch) and
// 0 forwards the raw label. The user-chosen default is 15 ms; the worker writes the
// setting into Control.lead and the payload clamps anything outside 0..MAX to DEFAULT.
constexpr T ORIGINAL_LEAD_NS=28820000, DEFAULT_LEAD_NS=15000000, MAX_LEAD_NS=30000000;
struct Scope {U tid,active;T stamp[2];U seen[2],bad;};
struct Sample {T stamp,receipt;unsigned char pose[84];};
struct Hand {Sample samples[16];U next,count,calibrated,pad;T lastCalibration,sum,anchor,lastStamp;int32_t lastOffset;U changed;uint64_t matched,unmatched,ambiguous,applied;};
struct Control {
 uint64_t magic,consume,state,submit,clock;
 U enabled,lock;uint64_t calls,scopes,conflicts,invalid,packets,faults;
 T lead;
 Scope scope[16];Hand hand[2];
};
static_assert(sizeof(Control)<ALLOCATION-DATA,"allocation");
