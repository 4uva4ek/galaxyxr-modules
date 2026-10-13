#include "../native/policy.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
static std::string file(const char* path) {
    std::ifstream f(path); assert(f); return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
    assert(argc == 4);
    auto idle = quality::visible_clients(file(argv[1])); assert(idle.empty());
    auto vr = quality::visible_clients(file(argv[2]));
    assert(vr.size() == 1 && vr[0] == "com.valvesoftware.steamlinkvr");
    auto gallery = quality::visible_clients(file(argv[3])); assert(gallery.empty());
    auto check_bad = [](const std::string& data) {
        bool bad = false;
        try { quality::visible_clients(data); } catch (...) { bad = true; }
        assert(bad);
    };
    check_bad("");
    check_bad("XR Clients state:\nCurrent Visible XR ClientId=5\nCurrent Visible Managed FSM XR ClientId=-1\n");
    check_bad("XR Clients state:\n com.foo.app: id=5\nCurrent Visible XR ClientId=5\n");
    assert(quality::thermal("IsStatusOverride: false\nThermal Status: 3\n") == 3);
    bool bad = false;
    try { quality::thermal("IsStatusOverride: true\nThermal Status: 0\n"); } catch (...) { bad = true; }
    assert(bad);
    assert(!quality::awake("mWakefulness=Asleep\n"));
    assert(quality::awake("mWakefulness=Awake\n"));
    bool critical = false;
    assert(quality::hold(true, true, 3, critical));
    assert(!quality::hold(true, true, 4, critical));
    assert(!quality::hold(true, true, 3, critical));
    assert(quality::hold(true, true, 2, critical));
    assert(!quality::hold(false, true, 2, critical));
    assert(!quality::hold(true, false, 2, critical));
    for (int s = 0; s <= 6; ++s) {
        assert(quality::normal_mode(s, false) == (s >= 3 ? 2 : 1));
        assert(quality::normal_mode(s, true) == 2);
    }
    std::cout << "PASS: retained Steam Link/idle/gallery snapshots; malformed-state rejection; sleep; critical guard hysteresis; all restore thermal/power modes\n";
}
