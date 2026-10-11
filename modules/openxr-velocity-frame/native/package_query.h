#pragma once
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <string>

// This exception is only raised by discovery, before target memory is opened.
struct PackageUnavailable : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline std::string queryPackages(const char* command = "/system/bin/cmd",
                                int timeoutMs = 3000) {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) throw std::runtime_error("Package pipe failed");
    pid_t child = fork();
    if (child < 0) {
        close(fds[0]); close(fds[1]);
        throw std::runtime_error("Package fork failed");
    }
    if (child == 0) {
        // Binder forwards these descriptors to system_server. Do not pass
        // root-only worker log files as stdin/stderr.
        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd < 0) _exit(126);
        dup2(nullfd, STDIN_FILENO); dup2(nullfd, STDERR_FILENO); close(nullfd);
        dup2(fds[1], STDOUT_FILENO); close(fds[0]); close(fds[1]);
        execl(command, command, "package", "list", "packages", "-3", "-U",
              static_cast<char*>(nullptr));
        _exit(127);
    }
    close(fds[1]);
    struct Query {
        int fd; pid_t child; bool reaped = false;
        ~Query() {
            close(fd);
            if (!reaped) {
                kill(child, SIGKILL); // Only our own read-only query child.
                while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
            }
        }
    } query{fds[0], child};
    if (fcntl(query.fd, F_SETFL, O_NONBLOCK) < 0)
        throw std::runtime_error("Package pipe setup failed");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::string output;
    bool eof = false;
    int status = 0;
    while (!eof || !query.reaped) {
        char buffer[4096];
        for (;;) {
            ssize_t n = read(query.fd, buffer, sizeof(buffer));
            if (n > 0) {
                output.append(buffer, static_cast<size_t>(n));
                if (output.size() > 4 * 1024 * 1024)
                    throw std::runtime_error("Package response exceeds limit");
            } else if (n == 0) { eof = true; break; }
            else if (errno == EINTR) { continue; }
            else if (errno == EAGAIN) { break; }
            else { throw PackageUnavailable("PackageManager response interrupted; no target writes"); }
        }
        if (!query.reaped) {
            pid_t result = waitpid(child, &status, WNOHANG);
            if (result == child) query.reaped = true;
            else if (result < 0 && errno != EINTR)
                throw std::runtime_error("Package query wait failed");
        }
        if (eof && query.reaped) break;
        if (std::chrono::steady_clock::now() >= deadline)
            throw PackageUnavailable("PackageManager query timed out; no target writes");
        pollfd fd{query.fd, POLLIN, 0};
        poll(eof ? nullptr : &fd, eof ? 0 : 1, 20);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw PackageUnavailable("PackageManager query failed; no target writes");
    return output;
}
