// Author: 4uva4ek. App scheduling only; no HAL, memory patches or original-file writes.
#include "priority.hpp"
#include "query.hpp"
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sched.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <fstream>

using xrpriority::Identity;
using xrpriority::Thread;
static const std::string STATE = "/dev/gxr_openxr_priority";
static std::string mod;
static bool safe_mod = false;
static volatile sig_atomic_t stopping = 0;
static void stop(int) { stopping = 1; }
static void need(bool ok, const std::string& reason) { if (!ok) throw std::runtime_error(reason); }
static bool exists(const std::string& path) { return access(path.c_str(), F_OK) == 0; }
static std::string proc(int pid, const std::string& leaf) { return "/proc/" + std::to_string(pid) + "/" + leaf; }
static std::string readfile(const std::string& path) {
    std::ifstream input(path); need(bool(input), "Cannot read " + path);
    std::string result; char buffer[8192];
    while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
        result.append(buffer, static_cast<size_t>(input.gcount()));
        need(result.size() <= 4 * 1024 * 1024, "Input exceeds bound");
    }
    need(!input.bad(), "Read failed"); return result;
}
static void save(const std::string& path, const std::string& data) {
    auto tmp = path + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    need(fd >= 0, "Cannot create state");
    bool ok = write(fd, data.data(), data.size()) == static_cast<ssize_t>(data.size()) && fsync(fd) == 0;
    close(fd); need(ok && rename(tmp.c_str(), path.c_str()) == 0, "State write failed");
}
static unsigned long long starttime(int pid) {
    auto stat = readfile(proc(pid, "stat")); auto end = stat.rfind(')');
    need(end != std::string::npos, "Invalid process identity");
    std::istringstream fields(stat.substr(end + 2)); std::string value, state;
    need(bool(fields >> state), "Missing process state");
    if (state == "Z" || state == "X" || state == "x") return 0;
    for (int i = 0; i < 19; ++i) need(bool(fields >> value), "Short process identity");
    need(!value.empty() && value.find_first_not_of("0123456789") == std::string::npos, "Invalid start time");
    return std::stoull(value);
}
static bool alive(Identity identity) {
    try { return identity.pid > 1 && identity.start && starttime(identity.pid) == identity.start; }
    catch (...) {
        if (access(proc(identity.pid, "stat").c_str(), F_OK) != 0 && (errno == ENOENT || errno == ESRCH)) return false;
        throw; // Unreadable identities must not be mistaken for exited targets during recovery.
    }
}
static std::vector<int> ids(const std::string& path) {
    DIR* directory = opendir(path.c_str()); need(directory, "Cannot enumerate " + path);
    std::vector<int> result;
    while (auto* entry = readdir(directory)) {
        std::string name(entry->d_name);
        if (name.empty() || name.size() > 7 || name.find_first_not_of("0123456789") != std::string::npos) continue;
        int value = std::stoi(name); if (value > 1) result.push_back(value);
    }
    closedir(directory); need(result.size() < 32768, "Process enumeration exceeds bound"); return result;
}
static std::string cmd(int pid) { auto text = readfile(proc(pid, "cmdline")); return text.substr(0, text.find('\0')); }
static bool owned_app(int pid, unsigned long expected) {
    std::istringstream lines(readfile(proc(pid, "status"))); std::string line;
    while (std::getline(lines, line)) if (line.rfind("Uid:", 0) == 0) {
        std::istringstream fields(line.substr(4)); unsigned long real, effective, saved, fs;
        return bool(fields >> real >> effective >> saved >> fs) && real == expected &&
               effective == expected && saved == expected && fs == expected;
    }
    return false;
}
struct App { Identity identity; std::string package; };
static std::vector<App> discover(std::string& report) {
    const auto power = query({"/system/bin/dumpsys", "-t", "2", "power"});
    if (!quality::awake(power)) { report = "Waiting: headset asleep; priorities restored."; return {}; }
    const auto visible = quality::visible_clients(query({"/system/bin/dumpsys", "-t", "2", "cpm"}));
    if (visible.empty()) { report = "Waiting: no visible OpenXR app; priorities restored."; return {}; }
    const auto packages = xrpriority::packages(query({"/system/bin/cmd", "package", "list", "packages", "-3", "-U"}));
    std::vector<App> apps; std::string unknown;
    for (int pid : ids("/proc")) {
        try {
            auto package = cmd(pid);
            if (std::find(visible.begin(), visible.end(), package) == visible.end()) continue;
            auto found = packages.find(package); if (found == packages.end()) continue;
            Identity identity{pid, starttime(pid)};
            if (!identity.start || !owned_app(pid, found->second) ||
                !xrpriority::runtime_loaded(readfile(proc(pid, "maps"))) || !alive(identity)) continue;
            if (xrpriority::profiled(package)) apps.push_back({identity, package});
            else unknown += package + " (profile unavailable; unchanged) ";
        } catch (const std::exception&) { /* Unverifiable or exiting apps are not targets. */ }
    }
    report = apps.empty() ? "Waiting: no supported visible OpenXR app. " : "Steam Link profile active. ";
    report += unknown; return apps;
}
struct Journal { Identity owner; std::vector<Thread> threads; };
static void record(const Journal& journal) {
    std::ostringstream text; text << journal.owner.pid << ' ' << journal.owner.start << '\n';
    for (const auto& t : journal.threads)
        text << t.app.pid << ' ' << t.app.start << ' ' << t.tid.pid << ' ' << t.tid.start << ' ' << t.original << '\n';
    save(STATE + "/journal", text.str());
}
static Journal load() {
    Journal journal; std::istringstream input(readfile(STATE + "/journal"));
    need(bool(input >> journal.owner.pid >> journal.owner.start) && journal.owner.pid > 1 && journal.owner.start, "Invalid owner");
    input >> std::ws;
    while (input.peek() != EOF) {
        Thread t;
        need(bool(input >> t.app.pid >> t.app.start >> t.tid.pid >> t.tid.start >> t.original), "Invalid thread journal");
        need(t.app.pid > 1 && t.tid.pid > 1 && t.app.start && t.tid.start && t.original > -20 && t.original <= 19, "Invalid thread fields");
        journal.threads.push_back(t); need(journal.threads.size() <= 4096, "Oversized journal"); input >> std::ws;
    }
    return journal;
}
struct Ops {
    Journal& journal;
    bool alive(const Thread& t) const {
        return ::alive(t.app) && ::alive(t.tid) && exists(proc(t.app.pid, "task/" + std::to_string(t.tid.pid)));
    }
    bool normal(const Thread& t) const { return sched_getscheduler(t.tid.pid) == SCHED_OTHER; }
    int priority(const Thread& t) const {
        errno = 0; int value = getpriority(PRIO_PROCESS, t.tid.pid); need(errno == 0, "getpriority failed"); return value;
    }
    void set(const Thread& t, int value) const {
        if (!alive(t)) return;
        need(normal(t), "Scheduling policy changed");
        if (setpriority(PRIO_PROCESS, t.tid.pid, value) && alive(t)) throw std::runtime_error("setpriority denied");
    }
    void record() const { ::record(journal); }
};
static void verify(Journal& journal) {
    Ops ops{journal};
    for (const auto& t : journal.threads) if (ops.alive(t))
        need(ops.normal(t) && ops.priority(t) == xrpriority::target, "Owned priority changed externally");
}
static void tune(Journal& journal) {
    std::string report; auto apps = discover(report); std::vector<Identity> active;
    for (const auto& app : apps) active.push_back(app.identity);
    Ops ops{journal}; xrpriority::release_inactive(journal.threads, active, ops);
    for (const auto& app : apps) {
        if (!alive(app.identity)) continue;
        std::vector<int> tids;
        try { tids = ids(proc(app.identity.pid, "task")); }
        catch (...) { if (!alive(app.identity)) continue; throw; }
        for (int tid : tids) {
            Thread t{app.identity, {tid, 0}, 0};
            try {
                t.tid.start = starttime(tid);
                if (!xrpriority::selected(app.package, quality::trim(readfile(proc(tid, "comm"))))) continue;
            } catch (...) { continue; }
            xrpriority::adopt(t, journal.threads, ops);
        }
    }
    report += " Owned threads=" + std::to_string(journal.threads.size()) + "; nice=-20.\n";
    if (!exists(STATE + "/status") || readfile(STATE + "/status") != report) save(STATE + "/status", report);
}
static bool restore(Journal& journal) {
    Ops ops{journal}; bool ok = true;
    for (const auto& t : journal.threads) {
        try { xrpriority::restore_one(t, ops); }
        catch (const std::exception& error) { fprintf(stderr, "DISABLED - Restore error: %s\n", error.what()); ok = false; }
    }
    if (ok) {
        unlink((STATE + "/journal").c_str()); unlink((STATE + "/status").c_str());
        unlink((mod + "/state/active.lease").c_str());
    }
    return ok;
}
static bool allowed() { return !exists(mod + "/disable") && !exists(mod + "/remove"); }
static void failure(const std::string& reason) {
    save(mod + "/disable", ""); save(mod + "/state/failure", reason + "\n");
    fprintf(stderr, "DISABLED - Error: %s\n", reason.c_str());
}
int main(int argc, char** argv) {
    try {
        need(argc == 3, "Usage: gxr-openxr-priority MODULE_DIR run|status|restore");
        mod = argv[1]; std::string mode = argv[2];
        need(getuid() == 0, "Root required"); umask(077);
        need(mod == "/data/adb/modules/gxr_openxr_priority", "Unexpected module path"); safe_mod = true;
        for (const auto& path : {mod, mod + "/state", STATE}) {
            struct stat st{};
            if (lstat(path.c_str(), &st)) need(errno == ENOENT && mkdir(path.c_str(), 0700) == 0, "Cannot create state directory");
            else need(S_ISDIR(st.st_mode) && st.st_uid == 0, "Unsafe state directory");
        }
        if (mode == "status") {
            if (exists(mod + "/state/failure")) printf("Last failure: %s", readfile(mod + "/state/failure").c_str());
            if (!exists(STATE + "/journal")) {
                need(!exists(mod + "/state/active.lease"), "Interrupted lease; restoration unconfirmed");
                puts("DISABLED - No owned priorities. ON requires standard KernelSU startup."); return 0;
            }
            auto journal = load(); verify(journal); need(alive(journal.owner), "Owner exited; run Action off");
            char path[4096]{}; auto n = readlink(proc(journal.owner.pid, "exe").c_str(), path, sizeof(path) - 1);
            need(n > 0 && std::string(path, size_t(n)) == mod + "/bin/gxr-openxr-priority", "Owner executable mismatch");
            printf("%s - %s", allowed() ? "ENABLED" : "DISABLED", exists(STATE + "/status") ?
                   readfile(STATE + "/status").c_str() : "Waiting: discovering visible OpenXR apps.\n"); return 0;
        }
        need(mode == "run" || mode == "restore", "Unknown command");
        int lock = open((STATE + "/lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        need(lock >= 0, "Lock open failed");
        if (flock(lock, LOCK_EX | LOCK_NB)) { puts("DISABLED - Waiting: worker owns the lock"); return 2; }
        if (mode == "restore") {
            if (exists(STATE + "/journal")) { auto journal = load(); need(restore(journal), "Restoration incomplete; leave disabled"); }
            else need(!exists(mod + "/state/active.lease"), "Interrupted lease without journal; fully power-cycle");
            puts("DISABLED - Owned priorities restored or targets exited"); return 0;
        }
        if (!allowed()) return 0;
        need(!exists(mod + "/state/active.lease") && !exists(mod + "/state/failure") && !exists(STATE + "/journal"), "Same-boot retry blocked");
        Journal journal; journal.owner = {getpid(), starttime(getpid())};
        signal(SIGTERM, stop); signal(SIGINT, stop); signal(SIGHUP, stop);
        save(mod + "/state/active.lease", readfile("/proc/sys/kernel/random/boot_id")); record(journal);
        int code = 0;
        try {
            while (allowed() && !stopping) { verify(journal); tune(journal); sleep(2); }
            if (stopping) failure("Worker interrupted; disabled pending review");
        } catch (const std::exception& error) { failure(error.what()); code = 1; }
        if (!restore(journal)) { failure("Restoration incomplete; leave disabled and fully power-cycle"); code = 1; }
        return code;
    } catch (const std::exception& error) {
        fprintf(stderr, "DISABLED - Error: %s\n", error.what());
        if (safe_mod && argc == 3 && std::string(argv[2]) != "status") try { failure(error.what()); } catch (...) {}
        return 1;
    }
}
