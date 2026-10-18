#include "../native/priority.hpp"
#include <cassert>
#include <iostream>
using namespace xrpriority;
struct Fake {
    std::vector<Thread>& owned;
    int value = 0, writes = 0, records = 0;
    bool living = true, other = true, fail = false, persisted = false;
    bool alive(const Thread&) { return living; }
    bool normal(const Thread&) { return other; }
    int priority(const Thread&) { return value; }
    void record() { ++records; persisted = !owned.empty(); }
    void set(const Thread&, int next) {
        assert(persisted); ++writes; value = next;
        if (fail) throw std::runtime_error("injected post-write failure");
    }
};
int main() {
    assert(selected("com.valvesoftware.steamlinkvr", "SVLRXThreadUDP"));
    assert(selected("com.valvesoftware.steamlinkvr", "CodecLooper"));
    assert(!selected("com.valvesoftware.steamlinkvr", "Thread-9"));
    assert(!selected("com.unknown.game", "CodecLooper"));
    assert(!selected("com.valvesoftware.steamlinkvr.clone", "SVLMediaThread"));
    assert(runtime_loaded("1000-2000 r-xp 0 00:00 1 /system/lib64/libopenxr_android.so\n"));
    assert(!runtime_loaded("1000-2000 rw-p 0 00:00 1 /system/lib64/libopenxr_android.so\n"));
    assert(!runtime_loaded("1000-2000 r-xp 0 00:00 1 /tmp/system/lib64/libopenxr_android.so\n"));
    assert(!runtime_loaded("1000-2000 r-xp 0 00:00 1 /system/lib64/libopenxr_android.so (deleted)\n"));
    auto apps = packages("package:com.good.app uid:10123\npackage:com.shared.a uid:10124\npackage:com.shared.b uid:10124\npackage:com.system.app uid:1000\n");
    assert(apps.size() == 1 && apps.at("com.good.app") == 10123);
    bool refused = false; try { packages("garbage"); } catch (...) { refused = true; } assert(refused);
    auto visible = quality::visible_clients("XR Clients state:\ncom.good.app: id=7, state=VISIBLE\nStats since reset:\nCurrent Visible XR ClientId=7\nCurrent Visible Managed FSM XR ClientId=-1\n");
    assert(visible.size() == 1 && visible[0] == "com.good.app");
    refused = false; try { quality::visible_clients("XR Clients state:\n"); } catch (...) { refused = true; } assert(refused);
    assert(!quality::awake("mWakefulness=Asleep\n"));

    Thread t{{10, 100}, {11, 101}, 0}; std::vector<Thread> owned; Fake ops{owned};
    adopt(t, owned, ops); assert(owned.size() == 1 && ops.value == -20 && ops.writes == 1 && ops.records == 1);
    adopt(t, owned, ops); assert(ops.writes == 1); // No duplicate ownership.
    release_inactive(owned, {t.app}, ops); assert(owned.size() == 1 && ops.writes == 1);
    release_inactive(owned, {}, ops); assert(owned.empty() && ops.value == 0 && ops.writes == 2); // Background/sleep.
    ops.value = -20; adopt(t, owned, ops); assert(owned.empty()); // External priority is untouched.
    ops.value = 0; ops.other = false; adopt(t, owned, ops); assert(owned.empty()); // RT excluded.
    ops.other = true; ops.fail = true;
    refused = false; try { adopt(t, owned, ops); } catch (...) { refused = true; }
    assert(refused && owned.size() == 1 && ops.value == -20); // Failed readback remains recoverable.
    ops.fail = false; restore_one(owned[0], ops); assert(ops.value == 0); owned.clear();
    adopt(t, owned, ops); ops.value = -10;
    refused = false; try { release_inactive(owned, {}, ops); } catch (...) { refused = true; }
    assert(refused && owned.size() == 1 && ops.value == -10); // External conflict retains evidence.
    ops.living = false; int before = ops.writes; release_inactive(owned, {}, ops);
    assert(owned.empty() && ops.writes == before); // Exit/PID reuse never writes to replacement.
    std::cout << "PASS: profile/UID/runtime/session guards, write-ahead ownership, background restore, external conflict, RT, exit/reuse and post-write failure recovery\n";
}
