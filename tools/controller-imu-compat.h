#pragma once
// Firmware-specific compatibility. Call only after the libsctrl SHA check.
// Accept both wait gates together; never mask an unknown or partial peer patch.
static void normalizeImuWait(std::vector<unsigned char>& live, uintptr_t begin) {
 const uintptr_t a=0x5cd750,b=0x5cd79c;
 const uint32_t oa=0x34000d74,ob=0x35000b08,nop=0xd503201f;
 auto at=[&](uintptr_t p){return live.data()+p-begin;};
 const bool stock=!memcmp(at(a),&oa,4)&&!memcmp(at(b),&ob,4);
 const bool active=!memcmp(at(a),&nop,4)&&!memcmp(at(b),&nop,4);
 require(stock||active,"Unknown or partial IMU wait patch");
 memcpy(at(a),&oa,4);memcpy(at(b),&ob,4);
}
