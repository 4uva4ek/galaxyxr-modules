// Session-scoped vendor mode controller. Original system files are never modified.
#include "policy.hpp"
static std::string host_identity();
#include "vendor.hpp"
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/system_properties.h>
#include <fcntl.h>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

using namespace quality;
static const std::string root = "/dev/gxr-quality-session";
static const std::string module = "/data/adb/modules/gxr_openxr_quality";
static bool module_owner = false;
static std::string generation;
static std::string self_path;
static volatile sig_atomic_t stopping = 0;
static void stop_signal(int) { stopping = 1; }
static long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static std::string read(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot read " + path);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
static bool exists(const std::string& path) { return access(path.c_str(), F_OK) == 0; }
static void safe_dir(const std::string& path) {
    if (mkdir(path.c_str(), 0700) && errno != EEXIST) throw std::runtime_error("Cannot create " + path);
    struct stat info{};
    if (lstat(path.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != 0 || (info.st_mode & 077))
        throw std::runtime_error("Unsafe directory " + path);
}
static void state_write(const std::string& leaf, const std::string& value) {
    std::string path = module + "/state/" + leaf;
    std::string temp = path + "." + std::to_string(getpid()) + ".tmp";
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error("Cannot write module state");
    bool ok = write(fd, value.data(), value.size()) == static_cast<ssize_t>(value.size());
    if (ok) ok = fsync(fd) == 0;
    close(fd);
    if (!ok || rename(temp.c_str(), path.c_str())) { unlink(temp.c_str()); throw std::runtime_error("Incomplete module state write"); }
}
static bool module_continue() {
    if (!module_owner) return true;
    try {
        return !exists(module + "/disable") && !exists(module + "/remove") &&
            !exists("/dev/ksu-restart-stage/busy") && !exists("/dev/ksu-restart-stage/error") &&
            trim(read("/dev/ksu-restart-stage/ready")) == "ready" &&
            trim(read("/dev/ksu-restart-stage/generation")) == generation;
    } catch (...) { return false; }
}
static bool uuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != '-') return false; }
        else if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}
static void module_failure(const std::string& reason) {
    if (!module_owner) return;
    int fd = open((module + "/disable").c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd >= 0) close(fd); else std::fprintf(stderr, "ERROR cannot disable module\n");
    try { state_write("failure", reason + "\n"); } catch (...) { std::fprintf(stderr, "ERROR cannot save failure reason\n"); }
}
static void save(const std::string& name, const std::string& value) {
    auto tmp = root + "/" + name + "." + std::to_string(getpid()) + ".tmp";
    { std::ofstream f(tmp); f << value << '\n'; if (!f) throw std::runtime_error("State write failed"); }
    if (rename(tmp.c_str(), (root + "/" + name).c_str())) throw std::runtime_error("State rename failed");
}
static void report(const std::string& value) {
    static std::string previous;
    save("status", value);
    if (value != previous) { std::printf("%lld %s\n", now_ms(), value.c_str()); std::fflush(stdout); previous = value; }
}
// Bounded subprocess with argv, no shell interpolation. Each dump can block at
// most 2.5s, and incomplete/oversized output is never accepted as a snapshot.
static std::string command(std::vector<std::string> args, int timeout = 2500) {
    int fd[2];
    if (pipe2(fd, O_CLOEXEC)) throw std::runtime_error("pipe failed");
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid == 0) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) _exit(127);
        signal(SIGTERM, SIG_DFL); signal(SIGINT, SIG_DFL);
        dup2(fd[1], STDOUT_FILENO); dup2(fd[1], STDERR_FILENO);
        close(fd[0]); close(fd[1]);
        std::vector<char*> argv;
        for (auto& arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);
        execv(argv[0], argv.data()); _exit(127);
    }
    close(fd[1]);
    if (pid < 0) { close(fd[0]); throw std::runtime_error("fork failed"); }
    fcntl(fd[0], F_SETFL, O_NONBLOCK);
    std::string output;
    long long deadline = now_ms() + timeout;
    bool finished = false, eof = false, failed = false;
    int status = 0;
    while (!finished || !eof) {
        char buf[8192]; ssize_t n;
        while ((n = ::read(fd[0], buf, sizeof(buf))) > 0) {
            output.append(buf, n);
            if (output.size() > 2 * 1024 * 1024) { failed = true; break; }
        }
        if (n == 0) eof = true;
        if (!finished) finished = waitpid(pid, &status, WNOHANG) == pid;
        if (finished && eof) break;
        if (failed || now_ms() >= deadline) { failed = true; break; }
        pollfd p{fd[0], POLLIN, 0}; poll(&p, 1, 20);
    }
    close(fd[0]);
    if (!finished) { kill(pid, SIGKILL); while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
    if (failed || !WIFEXITED(status) || WEXITSTATUS(status))
        throw std::runtime_error("Command failed/timed out: " + args[0] + " " + output.substr(0, 200));
    return output;
}
static std::string dump(const char* service) { return command({"/system/bin/dumpsys", "-t", "2", service}); }
static std::string identity(const std::string& pid) {
    auto stat = read("/proc/" + pid + "/stat");
    auto end = stat.rfind(')');
    if (end == std::string::npos) throw std::runtime_error("Invalid process identity");
    std::istringstream fields(stat.substr(end + 2)); std::string field;
    for (int i = 3; i <= 22; ++i) if (!(fields >> field)) throw std::runtime_error("Missing process start time");
    return pid + " " + field;
}
static std::string host_identity() {
    std::string pid = trim(command({"/system/bin/pidof", "android.hardware.perceptionhost"}));
    integer(pid); return identity(pid);
}
static bool openxr_loaded(const std::string& package) {
    DIR* dir = opendir("/proc");
    if (!dir) throw std::runtime_error("Cannot enumerate processes");
    bool found = false;
    while (auto* entry = readdir(dir)) {
        std::string pid(entry->d_name);
        if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos) continue;
        try {
            auto name = read("/proc/" + pid + "/cmdline"); name.resize(name.find('\0'));
            if (name != package && name.rfind(package + ":", 0) != 0) continue;
            auto maps = read("/proc/" + pid + "/maps");
            if (maps.find("/system/lib64/libopenxr_android.so") != std::string::npos ||
                maps.find("/system/lib/libopenxr_android.so") != std::string::npos) { found = true; break; }
        } catch (const std::exception&) { /* Exiting processes are not active evidence. */ }
    }
    closedir(dir); return found;
}
static std::string set_mode(bool force, int mode) {
    auto output = trim(command({self_path, force ? "vendor-force-quality" : mode == 1 ? "vendor-normal-quality" : "vendor-normal-efficiency",
                               trim(read(root + "/owned"))}));
    auto wanted = std::string("Successfully ") + (force ? "force-set-mode" : "set-mode");
    if (output.rfind(wanted, 0) != 0) throw std::runtime_error("Perception mode not accepted: " + output);
    return output;
}
static void release() {
    if (!exists(root + "/owned")) return;
    auto owner = trim(read(root + "/owned"));
    if (host_identity() != owner) {
        unlink((root + "/owned").c_str());
        report("DISABLED - Previous perception process exited; its runtime override is gone"); return;
    }
    int mode = 2; // If telemetry fails, clear the force latch conservatively to EFFICIENCY.
    try {
        int t = thermal(dump("thermalservice"));
        auto saver = trim(command({"/system/bin/settings", "get", "global", "low_power"}));
        if (saver != "0" && saver != "1") throw std::runtime_error("Unknown power saver state");
        mode = normal_mode(t, saver == "1");
    } catch (const std::exception& e) { std::fprintf(stderr, "RESTORE telemetry: %s; use EFFICIENCY\n", e.what()); }
    // set-mode explicitly clears the vendor's force flag AND refreshes camera properties.
    set_mode(false, mode);
    unlink((root + "/owned").c_str());
    report("DISABLED - Override cleared; normal mode " + std::to_string(mode) + " restored");
}
static void verify() {
    char model[PROP_VALUE_MAX]{}; __system_property_get("ro.product.model", model);
    if (std::strcmp(model, "SM-I610")) throw std::runtime_error("Unsupported model");
    auto hashes = command({"/system/bin/sha256sum", "/vendor/bin/hw/android.hardware.perceptionhost", "/system/framework/services.jar"});
    for (const char* hash : {"562d027ff604a428b4004882589910be5a21dc1ee7972556929784abd8f44ac5",
                             "1e6f0e8e85020b0ed25bcb387bf7604c9d38c73519ad85df04e15968290b1018"})
        if (hashes.find(hash) == std::string::npos) throw std::runtime_error("Unsupported perception host or system policy");
    for (auto pair : {std::pair<const char*, const char*>{"config_perceptionEfficiencyTriggerThermalSeverity", "3"},
                      {"config_perceptionEfficiencyReleaseThermalSeverity", "2"},
                      {"config_perceptionEfficiencyToggledByPowerSaver", "true"}}) {
        std::string type = std::string(pair.first).find("Toggled") != std::string::npos ? "bool/" : "integer/";
        auto value = trim(command({"/system/bin/cmd", "overlay", "lookup", "android", "android:" + type + pair.first}));
        if (value != pair.second) throw std::runtime_error("Unsupported thermal policy overlay");
    }
    auto help = command({self_path, "vendor-help"});
    if (help.find("force-set-mode") == std::string::npos || help.find("next set-mode command") == std::string::npos)
        throw std::runtime_error("Vendor force mode API unavailable");
}
static void monitor(int heartbeat, const std::string& host) {
    bool critical = false;
    while (!stopping) {
        if (!module_continue()) break;
        char c = 'h'; if (write(heartbeat, &c, 1) != 1) throw std::runtime_error("Guardian lost");
        if (host_identity() != host) throw std::runtime_error("Perception process changed; stopping this boot's controller");
        int severity = thermal(dump("thermalservice"));
        bool is_awake = awake(dump("power"));
        std::string active;
        if (is_awake && severity < 4 && (!critical || severity <= 2)) {
            for (const auto& package : visible_clients(dump("cpm")))
                if (openxr_loaded(package)) { active = package; break; }
        }
        bool desired = hold(!active.empty(), is_awake, severity, critical);
        if (desired && !exists(root + "/owned")) {
            // Ownership is recorded before the command, including partial/timeout failure.
            save("owned", host);
            set_mode(true, 1);
        } else if (!desired) release();
        if (desired) report("ENABLED - QUALITY held for " + active + "; thermal=" + std::to_string(severity));
        else report(std::string("ENABLED - Waiting: ") + (critical ? "critical thermal guard; resume at severity <=2" :
            !is_awake ? "headset asleep; ordinary policy active" : "no visible OpenXR application; ordinary policy active"));
        for (int i = 0; i < 10 && !stopping; ++i) usleep(100000);
    }
    release();
}
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) throw std::runtime_error("Usage: gxr-quality check | run | run-module | status | stop | restore");
        std::string action(argv[1]);
        if (action.rfind("vendor-", 0) == 0) { argv[1] += 7; return vendor_main(argc, argv); }
        if (argc != 2) throw std::runtime_error("Unexpected argument");
        char path[4096]; ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (length < 0 || length == static_cast<ssize_t>(sizeof(path) - 1)) throw std::runtime_error("Cannot resolve executable");
        path[length] = 0; self_path = path;
        if (action == "check") {
            verify();
            std::printf("IDENTITY_OK thermal=%d awake=%d host=%s\n", thermal(dump("thermalservice")), awake(dump("power")), host_identity().c_str());
            auto clients = visible_clients(dump("cpm"));
            for (const auto& client : clients) std::printf("VISIBLE %s OpenXR=%d\n", client.c_str(), openxr_loaded(client));
            return 0;
        }
        if (action == "status" || action == "stop") {
            if (!exists(root + "/pid")) {
                if (exists(root + "/owned")) throw std::runtime_error("Interrupted override remains; restoration required");
                std::puts("DISABLED - No controller or owned override"); return 0;
            }
            auto pid = trim(read(root + "/pid"));
            auto split = pid.find(' ');
            if (identity(pid.substr(0, split)) != pid) throw std::runtime_error("Controller process no longer exists");
            struct stat self{}, remote{};
            auto exe = "/proc/" + pid.substr(0, split) + "/exe";
            if (stat("/proc/self/exe", &self) || stat(exe.c_str(), &remote) || self.st_dev != remote.st_dev || self.st_ino != remote.st_ino)
                throw std::runtime_error("Controller executable identity mismatch");
            if (action == "stop") {
                if (kill(integer(pid.substr(0, split)), SIGTERM)) throw std::runtime_error("Stop signal failed");
                std::puts("STOP_REQUESTED - Check status/log for confirmed restoration");
            } else std::printf("%s", read(root + "/status").c_str());
            return 0;
        }
        if (action != "run" && action != "run-module" && action != "restore") throw std::runtime_error("Unknown command");
        safe_dir(root);
        int lock = open((root + "/lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (lock < 0) throw std::runtime_error("Cannot open controller lock");
        if (flock(lock, LOCK_EX | LOCK_NB)) { std::puts("ENABLED - Controller already running; no second instance started"); return action == "restore" ? 1 : 0; }
        if (action == "restore") { verify(); release(); unlink((root + "/pid").c_str()); std::puts("DISABLED - No owned override remains"); return 0; }
        if (action == "run-module") {
            if (self_path != module + "/bin/gxr-quality") throw std::runtime_error("Unexpected module executable path");
            safe_dir(module + "/state");
            auto boot = trim(read("/proc/sys/kernel/random/boot_id"));
            module_owner = true;
            if (!uuid(boot)) throw std::runtime_error("Invalid current boot identity");
            bool previous = exists(module + "/state/boot_id");
            auto old_boot = previous ? trim(read(module + "/state/boot_id")) : "";
            if ((previous && !uuid(old_boot)) || (!previous && (exists(module + "/state/failure") || exists(module + "/state/interrupted"))))
                throw std::runtime_error("Cannot establish previous boot identity; guards retained");
            if (!previous || old_boot != boot) {
                unlink((module + "/state/failure").c_str()); unlink((module + "/state/interrupted").c_str());
                state_write("boot_id", boot + "\n");
            }
            if (exists(module + "/disable") || exists(module + "/remove")) { std::puts("DISABLED - KernelSU preference is OFF"); return 0; }
            if (exists(module + "/state/failure") || exists(module + "/state/interrupted"))
                throw std::runtime_error("Same-boot failure/interruption guard; restore and investigate before retry");
            try { generation = trim(command({"/system/bin/sh", module + "/gate.sh"}, 3500)); }
            catch (const std::exception& e) { report(std::string("DISABLED - WAITING: ") + e.what()); return 0; }
            state_write("interrupted", boot + "\n");
        }
        verify();
        if (exists(root + "/owned")) { release(); throw std::runtime_error("Interrupted override restored; inspect before restarting"); }
        signal(SIGTERM, stop_signal); signal(SIGINT, stop_signal); signal(SIGHUP, SIG_IGN); signal(SIGPIPE, SIG_IGN);
        save("pid", identity(std::to_string(getpid())));
        save("boot_id", trim(read("/proc/sys/kernel/random/boot_id")));
        std::string host = host_identity();
        int pipefd[2]; if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK)) throw std::runtime_error("Heartbeat pipe failed");
        pid_t worker = fork();
        if (worker < 0) throw std::runtime_error("Worker fork failed");
        if (!worker) {
            close(pipefd[0]);
            try { monitor(pipefd[1], host); _exit(0); }
            catch (const std::exception& e) {
                std::fprintf(stderr, "WORKER_ERROR %s\n", e.what());
                try { release(); } catch (const std::exception& error) { std::fprintf(stderr, "RESTORE_ERROR %s\n", error.what()); }
                module_failure(e.what());
                _exit(1);
            }
        }
        close(pipefd[1]);
        long long last = now_ms(); bool done = false; int status = 0;
        while (!stopping) {
            if (!module_continue()) { stopping = 1; break; }
            char buf[32]; if (::read(pipefd[0], buf, sizeof(buf)) > 0) last = now_ms();
            if (waitpid(worker, &status, WNOHANG) == worker) { done = true; break; }
            if (now_ms() - last > 12000) { std::fprintf(stderr, "GUARDIAN: heartbeat expired\n"); break; }
            usleep(200000);
        }
        if (!done) {
            kill(worker, SIGTERM);
            for (int i = 0; i < 35; ++i) { if (waitpid(worker, &status, WNOHANG) == worker) { done = true; break; } usleep(200000); }
            if (!done) { kill(worker, SIGKILL); while (waitpid(worker, &status, 0) < 0 && errno == EINTR) {} }
        }
        // Child is stopped before cleanup, so it cannot race a new force command.
        release();
        report(stopping ? "DISABLED - Stopped; ordinary policy restored" : "DISABLED - Worker exited; ordinary policy restored; inspect log");
        unlink((root + "/pid").c_str());
        if (module_owner) {
            if (stopping) unlink((module + "/state/interrupted").c_str());
            else module_failure("Worker failed or heartbeat expired; normal policy restored; inspect runtime.log");
        }
        return stopping ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR %s\n", e.what());
        module_failure(e.what());
        return 1;
    }
}
