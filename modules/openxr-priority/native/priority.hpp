#pragma once
#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "session_policy.hpp"
#include "../../openxr-velocity-frame/native/steamlink_package.h"

namespace xrpriority {
struct Identity {
    int pid = 0;
    unsigned long long start = 0;
    bool operator==(const Identity& other) const { return pid == other.pid && start == other.start; }
};
struct Thread { Identity app, tid; int original = 0; };
constexpr int target = -20;

// One thread profile covers Steam Link and its renamed copies.
inline bool profiled(const std::string& package) { return steamLinkPackage(package); }
inline bool selected(const std::string& package, const std::string& name) {
    // Generic Thread-N names do not reliably identify a pipeline role.
    return profiled(package) && (name == "SVLRXThreadUDP" || name == "SVLMediaThread" ||
                                name == "NDK MediaCodec_" || name == "CodecLooper");
}
inline bool runtime_loaded(const std::string& maps) {
    std::istringstream lines(maps); std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line); std::string range, mode, offset, dev, inode, path, extra;
        if (!(fields >> range >> mode >> offset >> dev >> inode >> path) || fields >> extra) continue;
        if (mode == "r-xp" && (path == "/system/lib64/libopenxr_android.so" ||
                               path == "/system/lib/libopenxr_android.so")) return true;
    }
    return false;
}
inline std::map<std::string, unsigned long> packages(const std::string& text) {
    std::map<std::string, unsigned long> result; std::map<unsigned long, int> counts;
    std::istringstream lines(text); std::string line;
    while (std::getline(lines, line)) {
        line = quality::trim(line); if (line.empty()) continue;
        auto split = line.find(" uid:");
        if (line.rfind("package:", 0) || split == std::string::npos)
            throw std::runtime_error("Unrecognized package response");
        auto package = line.substr(8, split - 8); auto number = line.substr(split + 5);
        if (!quality::package_name(package)) continue;
        if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Invalid package UID");
        auto uid = std::stoul(number);
        if (uid % 100000 < 10000 || uid % 100000 >= 20000) continue;
        if (!result.emplace(package, uid).second) throw std::runtime_error("Duplicate package");
        ++counts[uid];
    }
    for (auto it = result.begin(); it != result.end();) {
        if (counts[it->second] != 1) it = result.erase(it); else ++it;
    }
    return result;
}

// Ops owns identity checks, priority syscalls and durable journal writes.
// Write-ahead ownership also covers failures after setpriority but before readback.
template<class Ops> void restore_one(const Thread& t, Ops& ops) {
    if (!ops.alive(t)) return;
    int current = ops.priority(t);
    if (current == t.original) return;
    if (current != target || !ops.normal(t)) throw std::runtime_error("Priority restore conflict");
    ops.set(t, t.original);
    if (ops.alive(t) && ops.priority(t) != t.original) throw std::runtime_error("Priority restore readback failed");
}
template<class Ops> void release_inactive(std::vector<Thread>& owned,
                                        const std::vector<Identity>& active, Ops& ops) {
    for (size_t i = 0; i < owned.size();) {
        const auto t = owned[i];
        if (!ops.alive(t) || std::find(active.begin(), active.end(), t.app) == active.end()) {
            restore_one(t, ops);
            owned.erase(owned.begin() + i); ops.record();
        } else ++i;
    }
}
template<class Ops> void adopt(Thread t, std::vector<Thread>& owned, Ops& ops) {
    if (!ops.alive(t) || !ops.normal(t)) return;
    if (std::any_of(owned.begin(), owned.end(), [&](const Thread& old) {
        return old.app == t.app && old.tid == t.tid;
    })) return;
    t.original = ops.priority(t);
    if (t.original == target) return; // External -20 is never claimed or restored.
    if (owned.size() >= 4096) throw std::runtime_error("Too many owned threads");
    owned.push_back(t); ops.record();
    if (!ops.alive(t)) return;
    if (!ops.normal(t) || ops.priority(t) != t.original)
        throw std::runtime_error("Priority changed before apply");
    ops.set(t, target);
    if (ops.alive(t) && ops.priority(t) != target) throw std::runtime_error("Priority readback failed");
}
}
