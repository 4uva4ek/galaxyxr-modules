using uintptr_t=unsigned long long;
using uint32_t=unsigned;
#include "patch.h"
#include "validation.h"
extern "C" __declspec(dllexport) int check_rx(unsigned char* live,const unsigned char* stock) {
 return validate_rx(live,stock);
}
