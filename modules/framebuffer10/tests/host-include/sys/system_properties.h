#pragma once
#include <cstdlib>
#define PROP_VALUE_MAX 92
// Host builds are hard-gated to mock-only entry points in main().
inline int __system_property_get(const char*, char*) { std::abort(); }
