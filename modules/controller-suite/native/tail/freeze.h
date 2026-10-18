// All-thread ptrace guard for a bounded, manually launched private-memory test.
// No EXITKILL. Every successfully seized TID is tracked, even before its stop.
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <elf.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <algorithm>
#include <string>
#include <vector>
#include <stdexcept>
#include <memory>

struct Frozen {
    using Clock = std::chrono::steady_clock;
    struct Thread {
        pid_t id = 0;
        bool seized = false;
        bool stopped = false;
        int signal = 0;
        uintptr_t pc = 0;
        uintptr_t lr = 0;
        bool pcKnown = false;
        int lastError = 0;
    };
    struct Arm64Registers { uint64_t x[31], sp, pc, pstate; };
    std::vector<Thread> threads;
    bool cleanupFailed = false;
    Frozen() = default;
    Frozen(const Frozen&) = delete;
    Frozen& operator=(const Frozen&) = delete;

    static bool gone(pid_t tid) {
        errno = 0;
        return kill(tid, 0) == -1 && errno == ESRCH;
    }
    static void recordStop(Thread& item, int status) {
        item.stopped = true;
        // Preserve real signal-delivery stops. PTRACE_INTERRUPT's synthetic
        // event and pre-existing group-stop events must not inject SIGTRAP.
        item.signal = (status >> 16) == 0 ? WSTOPSIG(status) : 0;
    }
    static bool waitStop(Thread& item, Clock::time_point deadline, bool strict) {
        for (;;) {
            int status = 0;
            pid_t got = waitpid(item.id, &status, __WALL | WNOHANG);
            if (got == item.id) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    item.seized = false;
                    item.stopped = false;
                    return false;
                }
                if (WIFSTOPPED(status)) {
                    recordStop(item, status);
                    return !strict || ((status >> 16) == PTRACE_EVENT_STOP &&
                                       WSTOPSIG(status) == SIGTRAP);
                }
            } else if (got < 0 && errno != EINTR) {
                item.lastError = errno;
                if (gone(item.id)) item.seized = false;
                return false;
            }
            if (Clock::now() >= deadline) { item.lastError = ETIMEDOUT; return false; }
            usleep(1000);
        }
    }
    static void snapshotPC(Thread& item) {
        Arm64Registers regs{};
        iovec io{&regs, sizeof(regs)};
        if (ptrace(PTRACE_GETREGSET, item.id, reinterpret_cast<void*>(NT_PRSTATUS), &io) == -1 ||
            io.iov_len != sizeof(regs)) {
            item.lastError = errno;
            throw std::runtime_error("Cannot verify stopped thread PC");
        }
        item.pc = regs.pc;
        item.lr = regs.x[30];
        item.pcKnown = true;
    }
    void freeze(pid_t pid, unsigned timeoutMs = 750) {
        if (!threads.empty()) throw std::runtime_error("Frozen guard cannot be reused");
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        for (int round = 0; round < 8; ++round) {
            if (Clock::now() >= deadline) throw std::runtime_error("Thread stop timeout");
            const auto taskPath = "/proc/" + std::to_string(pid) + "/task";
            std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(taskPath.c_str()), &closedir);
            if (!dir) throw std::runtime_error("Cannot list target threads");
            std::vector<pid_t> fresh;
            for (;;) {
                errno = 0;
                auto* entry = readdir(dir.get());
                if (!entry) {
                    if (errno) throw std::runtime_error("Incomplete target thread enumeration");
                    break;
                }
                char* end = nullptr;
                long value = strtol(entry->d_name, &end, 10);
                if (!end || *end || value <= 0 || value > INT32_MAX) continue;
                pid_t tid = static_cast<pid_t>(value);
                if (std::none_of(threads.begin(), threads.end(), [tid](const Thread& t) { return t.id == tid && t.seized; }))
                    fresh.push_back(tid);
            }
            dir.reset();
            if (fresh.empty()) return;
            // Allocate before SEIZE: an allocation failure must not lose a
            // newly attached thread from the cleanup inventory.
            threads.reserve(threads.size() + fresh.size());
            for (pid_t tid : fresh) {
                if (Clock::now() >= deadline) throw std::runtime_error("Thread stop timeout");
                if (ptrace(PTRACE_SEIZE, tid, nullptr, nullptr) == -1) {
                    if (errno == ESRCH) continue;
                    throw std::runtime_error("PTRACE_SEIZE denied");
                }
                threads.push_back(Thread{});
                Thread& item = threads.back();
                item.id = tid;
                item.seized = true;
                if (ptrace(PTRACE_INTERRUPT, tid, nullptr, nullptr) == -1) {
                    item.lastError = errno;
                    if (gone(tid)) { item.seized = false; continue; }
                    throw std::runtime_error("Thread interrupt failed");
                }
                if (!waitStop(item, deadline, true)) {
                    if (!item.seized) continue;
                    throw std::runtime_error(item.stopped ? "Unexpected target thread stop" : "Thread stop timeout or wait failure");
                }
                snapshotPC(item);
            }
        }
        throw std::runtime_error("Target thread list did not stabilize");
    }
    bool pcInRange(uintptr_t begin, uintptr_t end) const {
        if (begin >= end) throw std::runtime_error("Invalid patch PC interval");
        for (const auto& item : threads) {
            if (!item.seized) continue;
            if (!item.stopped || !item.pcKnown) throw std::runtime_error("Thread PC is unverified");
            if ((item.pc >= begin && item.pc < end) ||
                (item.lr >= begin && item.lr < end)) return true;
        }
        return false;
    }
    void requireNoPC(uintptr_t begin, uintptr_t end) const {
        if (pcInRange(begin, end)) throw std::runtime_error("PC in patch window");
    }
    // Call explicitly before printing success or starting the experiment timer.
    // A false result is fatal containment trouble: report it and exit promptly
    // so the kernel releases any remaining tracees. Never claim resumed/stable.
    bool release(unsigned timeoutMs = 250) noexcept {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        bool ok = true;
        for (auto pos = threads.rbegin(); pos != threads.rend(); ++pos) {
            Thread& item = *pos;
            if (!item.seized) continue;
            if (!item.stopped) {
                if (ptrace(PTRACE_INTERRUPT, item.id, nullptr, nullptr) == -1) {
                    item.lastError = errno;
                    if (gone(item.id)) { item.seized = false; continue; }
                }
                // Also drain an already pending interrupt/event after a failed
                // second INTERRUPT. Non-stopped attachments cannot be ignored.
                if (!waitStop(item, deadline, false) && item.seized) { ok = false; continue; }
                if (!item.seized) continue;
            }
            if (ptrace(PTRACE_DETACH, item.id, nullptr,
                       reinterpret_cast<void*>(static_cast<intptr_t>(item.signal))) == -1) {
                item.lastError = errno;
                if (gone(item.id)) { item.seized = false; item.stopped = false; continue; }
                ok = false;
            } else {
                item.seized = false;
                item.stopped = false;
            }
        }
        cleanupFailed = !ok;
        return ok;
    }
    ~Frozen() {
        if (!release()) {
            // Do not enter ordinary catch/retry logic with tracees still held.
            // Exiting drops all ptrace attachments; the independent guardian
            // observes pipe EOF and attempts stock restoration. No EXITKILL.
            int flags = fcntl(STDERR_FILENO, F_GETFL);
            if (flags >= 0) (void)fcntl(STDERR_FILENO, F_SETFL, flags | O_NONBLOCK);
            static constexpr char error[] = "ERROR: ptrace detach incomplete; exiting 70 to release tracees. Guardian must verify stock; reboot if recovery is uncertain.\n";
            (void)!::write(STDERR_FILENO, error, sizeof(error) - 1);
            _exit(70);
        }
    }
};
