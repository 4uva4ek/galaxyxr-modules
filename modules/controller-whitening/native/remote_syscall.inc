// Author: 4uva4ek. ORION bounded ARM64 syscall primitive; no discovery or startup.
// Caller owns an all-thread ptrace stop and must keep every other thread stopped.
// Only mmap/mprotect/munmap are permitted. A borrowed libc SVC is verified again.
// A real signal reported by RemoteSyscallFatal must be forwarded by the owner's
// detach guard (set that TID's saved signal); never silently resume after failure.
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

struct RemoteSyscallFatal : std::runtime_error {
    pid_t tid;
    bool threadStopped;
    bool registersRestored;
    bool targetGone;
    int pendingSignal;
    RemoteSyscallFatal(pid_t t, bool stopped, bool restored, bool gone,
                       int pending, const std::string& reason)
        : std::runtime_error("REMOTE_SYSCALL_FATAL tid=" + std::to_string(t) +
              " stopped=" + std::to_string(stopped) +
              " restored=" + std::to_string(restored) +
              " gone=" + std::to_string(gone) +
              " pending_signal=" + std::to_string(pending) + ": " + reason),
          tid(t), threadStopped(stopped), registersRestored(restored),
          targetGone(gone), pendingSignal(pending) {}
};

namespace orion_remote {
using Clock = std::chrono::steady_clock;
constexpr unsigned long GPR_NOTE = 1;       // NT_PRSTATUS
constexpr unsigned long FPSIMD_NOTE = 2;    // NT_PRFPREG / NT_FPREGSET
constexpr unsigned long SYSCALL_NOTE = 0x404; // NT_ARM_SYSTEM_CALL
constexpr uint64_t SINGLE_STEP = uint64_t(1) << 21;
struct Gpr { uint64_t x[31], sp, pc, pstate; };
struct alignas(16) Fpsimd {
    unsigned char v[32][16];
    uint32_t fpsr, fpcr;
    uint32_t reserved[2];
};
static_assert(sizeof(Gpr) == 272, "ARM64 GPR layout");
static_assert(sizeof(Fpsimd) == 528, "ARM64 FPSIMD layout");

static std::runtime_error error(const char* what) {
    return std::runtime_error(std::string(what) + " errno=" + std::to_string(errno));
}
template<class T> static T get(pid_t tid, unsigned long note) {
    T value{}; iovec io{&value, sizeof(value)};
    if (ptrace(PTRACE_GETREGSET, tid, reinterpret_cast<void*>(note), &io) == -1)
        throw error("GETREGSET");
    if (io.iov_len != sizeof(value)) throw std::runtime_error("Short ARM64 regset");
    return value;
}
template<class T> static void set(pid_t tid, unsigned long note, const T& value) {
    T copy = value; iovec io{&copy, sizeof(copy)};
    if (ptrace(PTRACE_SETREGSET, tid, reinterpret_cast<void*>(note), &io) == -1)
        throw error("SETREGSET");
    if (io.iov_len != sizeof(copy)) throw std::runtime_error("Short ARM64 regset write");
}
static bool sameGpr(Gpr a, Gpr b) {
    // ARM64 keeps PSTATE.SS transiently set while single-stepped under ptrace.
    // Detach must clear this kernel-managed bit; every other bit must match.
    a.pstate &= ~SINGLE_STEP; b.pstate &= ~SINGLE_STEP;
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}
static bool sameFp(const Fpsimd& a, const Fpsimd& b) {
    return std::memcmp(a.v, b.v, sizeof(a.v)) == 0 &&
           a.fpsr == b.fpsr && a.fpcr == b.fpcr;
}
struct Saved {
    Gpr gpr;
    Fpsimd fp;
    int32_t syscall;
};
static Saved save(pid_t tid) {
    Saved s{get<Gpr>(tid, GPR_NOTE), get<Fpsimd>(tid, FPSIMD_NOTE),
            get<int32_t>(tid, SYSCALL_NOTE)};
    // Refuse an interrupted/restartable kernel syscall. GPR restoration alone
    // cannot restore private kernel orig_x0/restart bookkeeping.
    if (s.syscall != -1)
        throw std::runtime_error("Syscall thread ineligible: NT_ARM_SYSTEM_CALL is not -1; no changes");
    if ((s.gpr.pc & 3) || s.gpr.pc < 0x10000)
        throw std::runtime_error("Thread PC ineligible; no changes");
    return s;
}
static void restore(pid_t tid, const Saved& s, bool& fpEverChanged) {
    // A kernel syscall must not touch FP/SIMD. Avoid writing FPSIMD on the
    // normal path: such a write can affect extended SVE state on some kernels.
    auto fp = get<Fpsimd>(tid, FPSIMD_NOTE);
    const bool fpChanged = !sameFp(fp, s.fp);
    fpEverChanged |= fpChanged;
    if (fpChanged) set(tid, FPSIMD_NOTE, s.fp);
    set(tid, SYSCALL_NOTE, s.syscall);
    set(tid, GPR_NOTE, s.gpr);
    if (!sameGpr(get<Gpr>(tid, GPR_NOTE), s.gpr) ||
        get<int32_t>(tid, SYSCALL_NOTE) != s.syscall ||
        !sameFp(get<Fpsimd>(tid, FPSIMD_NOTE), s.fp))
        throw std::runtime_error("ARM64 register restoration readback mismatch");
    if (fpChanged)
        throw std::runtime_error("Unexpected FPSIMD mutation restored; extended-vector state not certified");
}
struct Stop {
    bool stopped = false, gone = false;
    int status = 0, pending = 0;
};
static Stop waitStopped(pid_t tid, Clock::time_point deadline) {
    for (;;) {
        int status = 0;
        pid_t got = waitpid(tid, &status, __WALL | WNOHANG);
        if (got == tid) {
            if (WIFSTOPPED(status)) {
                // Event stops are synthetic. An ordinary signal-delivery stop
                // must be preserved by the owner's detach guard on failure.
                int pending = (status >> 16) == 0 ? WSTOPSIG(status) : 0;
                return {true, false, status, pending};
            }
            if (WIFEXITED(status) || WIFSIGNALED(status)) return {false, true, status, 0};
        } else if (got < 0 && errno != EINTR) {
            const int savedErrno = errno;
            errno = 0;
            if (kill(tid, 0) == -1 && errno == ESRCH) return {false, true, 0, 0};
            errno = savedErrno;
            throw error("Bounded syscall wait");
        }
        if (Clock::now() >= deadline) return {};
        usleep(1000);
    }
}
static bool isStepTrap(pid_t tid, int status) {
    if (!WIFSTOPPED(status) || (status >> 16) != 0 || WSTOPSIG(status) != SIGTRAP)
        return false;
    siginfo_t info{};
    if (ptrace(PTRACE_GETSIGINFO, tid, nullptr, &info) == -1)
        throw error("Cannot classify syscall SIGTRAP");
    // arm64 syscall-exit single-step can use generic user_single_step_report:
    // SIGTRAP with zero-initialized SI_USER/pid/uid, rather than TRAP_TRACE.
    // Ordinary userspace kill(SIGTRAP) carries a nonzero sender PID.
    return info.si_signo == SIGTRAP &&
           (info.si_code == TRAP_TRACE ||
            (info.si_code == SI_USER && info.si_pid == 0 && info.si_uid == 0));
}
static void verifySvc(pid_t tid, uintptr_t svc) {
    if ((svc & 3) || svc < 0x10000) throw std::runtime_error("Invalid borrowed SVC address");
    const uintptr_t aligned = svc & ~uintptr_t(7);
    errno = 0;
    long word = ptrace(PTRACE_PEEKTEXT, tid, reinterpret_cast<void*>(aligned), nullptr);
    if (word == -1 && errno) throw error("Borrowed SVC read");
    uint32_t instruction = 0;
    std::memcpy(&instruction, reinterpret_cast<unsigned char*>(&word) + (svc - aligned), 4);
    if (instruction != 0xd4000001) throw std::runtime_error("Borrowed libc instruction is not SVC #0");
}
} // namespace orion_remote

static bool remoteSyscallEligible(pid_t tid) {
    if (tid <= 1) return false;
    try { (void)orion_remote::save(tid); return true; }
    catch (const std::exception&) { return false; }
}

static uint64_t remoteSyscall(pid_t tid, uintptr_t svc, uint64_t number,
                              std::array<uint64_t, 6> args) {
    using namespace orion_remote;
    if (tid <= 1 || (number != 222 && number != 226 && number != 215 && number != 56 && number != 57))
        throw std::runtime_error("Remote syscall is outside mmap/mprotect/munmap/read-only-openat/close allowlist");
    verifySvc(tid, svc);
    const Saved saved = save(tid); // All eligibility failures precede mutation.
    Gpr call = saved.gpr; call.pc = svc; call.x[8] = number;
    for (size_t i = 0; i < args.size(); ++i) call.x[i] = args[i];
    bool running = false, stopped = true, gone = false, fpEverChanged = false;
    int pendingSignal = 0;
    try {
        set(tid, GPR_NOTE, call);
        if (ptrace(PTRACE_SINGLESTEP, tid, nullptr, nullptr) == -1)
            throw error("Remote syscall SINGLESTEP");
        running = true; stopped = false;
        Stop end = waitStopped(tid, Clock::now() + std::chrono::milliseconds(100));
        stopped = end.stopped; gone = end.gone; running = !stopped && !gone;
        pendingSignal = end.pending;
        if (gone) throw std::runtime_error("Target ended during remote syscall");
        if (!stopped) throw std::runtime_error("Remote syscall step timeout");
        const bool ownTrap = isStepTrap(tid, end.status);
        if (ownTrap) pendingSignal = 0;
        const Gpr result = get<Gpr>(tid, GPR_NOTE);
        if (!ownTrap || result.pc != svc + 4) {
            siginfo_t detail{};
            int detailRc = ptrace(PTRACE_GETSIGINFO, tid, nullptr, &detail);
            char message[256];
            snprintf(message, sizeof(message),
                "Unexpected remote syscall stop: status=0x%x pc=0x%llx svc=0x%llx siginfo_rc=%d signo=%d code=%d addr=%p own_trace=%d syscall=%llu",
                end.status, static_cast<unsigned long long>(result.pc),
                static_cast<unsigned long long>(svc), detailRc, detail.si_signo,
                detail.si_code, detail.si_addr, ownTrap,
                static_cast<unsigned long long>(number));
            throw std::runtime_error(message);
        }
        // This is our successful single-step trap, not a signal to reinject.
        pendingSignal = 0;
        restore(tid, saved, fpEverChanged);
        return result.x[0]; // Caller must reject Linux negative errno results.
    } catch (const std::exception& failure) {
        std::string reason = failure.what();
        if (running) {
            // Even EIO can mean the step stop raced INTERRUPT; still drain the
            // wait event. Never use blocking waitpid on this recovery path.
            (void)ptrace(PTRACE_INTERRUPT, tid, nullptr, nullptr);
            try {
                Stop end = waitStopped(tid, Clock::now() + std::chrono::milliseconds(100));
                stopped = end.stopped; gone = end.gone;
                if (end.pending) pendingSignal = end.pending;
                // Never reinject our delayed single-step trap as a real signal.
                if (stopped && isStepTrap(tid, end.status)) pendingSignal = 0;
            } catch (const std::exception& e) { reason += std::string("; stop recovery: ") + e.what(); }
        }
        bool restored = false;
        if (stopped && !gone) {
            try { restore(tid, saved, fpEverChanged); restored = !fpEverChanged; }
            catch (const std::exception& e) { reason += std::string("; restore: ") + e.what(); }
        }
        // Owner must abort the transaction, preserve pendingSignal, notify its
        // guardian, and exit promptly if !stopped or !restored (unless gone).
        throw RemoteSyscallFatal(tid, stopped, restored, gone, pendingSignal, reason);
    }
}
