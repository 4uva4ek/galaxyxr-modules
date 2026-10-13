#pragma once
#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace quality {
inline int integer(const std::string& s) {
    size_t n = 0;
    int value = std::stoi(s, &n);
    if (n != s.size()) throw std::runtime_error("Invalid integer: " + s);
    return value;
}
inline std::string trim(std::string s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
inline int thermal(const std::string& dump) {
    std::istringstream lines(dump);
    std::string line;
    int status = -1;
    bool normal = false;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line == "IsStatusOverride: false") normal = true;
        if (line.rfind("Thermal Status: ", 0) == 0) {
            if (status != -1) throw std::runtime_error("Duplicate thermal status");
            status = integer(line.substr(16));
        }
    }
    if (!normal || status < 0 || status > 6)
        throw std::runtime_error("Thermal status unavailable or overridden");
    return status;
}
inline bool awake(const std::string& dump) {
    std::istringstream lines(dump);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line == "mWakefulness=Awake") return true;
        if (line == "mWakefulness=Asleep" || line == "mWakefulness=Dozing" ||
            line == "mWakefulness=Dreaming") return false;
    }
    throw std::runtime_error("Wakefulness unavailable");
}
inline bool package_name(const std::string& s) {
    if (s.empty() || s.find('.') == std::string::npos) return false;
    for (unsigned char c : s)
        if (!(std::isalnum(c) || c == '_' || c == '.')) return false;
    return s != "com.android.systemui" && s != "com.samsung.android.app.xr.home";
}
// CPM supplies live compositor client IDs, not merely installed/running apps.
// The caller additionally checks /proc/<pid>/maps for the native OpenXR runtime.
inline std::vector<std::string> visible_clients(const std::string& dump) {
    std::istringstream lines(dump);
    std::string line;
    bool clients = false, found = false;
    std::map<int, std::string> names;
    std::vector<int> visible;
    int visible_fields = 0;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line == "XR Clients state:") { clients = true; found = true; continue; }
        if (line == "Stats since reset:") clients = false;
        if (clients) {
            auto pos = line.find(": id=");
            if (pos != std::string::npos) {
                auto end = line.find(',', pos);
                int id = integer(line.substr(pos + 5, end == std::string::npos ? end : end - pos - 5));
                if (names.count(id)) throw std::runtime_error("Duplicate XR client");
                names[id] = line.substr(0, pos);
            }
        }
        for (const char* prefix : {"Current Visible XR ClientId=", "Current Visible Managed FSM XR ClientId="}) {
            std::string key(prefix);
            if (line.rfind(key, 0) == 0) {
                visible.push_back(integer(line.substr(key.size())));
                ++visible_fields;
            }
        }
    }
    if (!found || visible_fields != 2) throw std::runtime_error("Incomplete CPM session state");
    std::vector<std::string> result;
    for (int id : visible) {
        if (id < 0) continue;
        if (!names.count(id)) throw std::runtime_error("Visible XR client missing from registry");
        if (package_name(names[id])) result.push_back(names[id]);
    }
    return result;
}
inline int normal_mode(int temperature, bool saver) {
    if (temperature < 0 || temperature > 6) throw std::runtime_error("Invalid thermal severity");
    // Verified device overlay: trigger=3, release=2, battery-saver policy enabled.
    return temperature >= 3 || saver ? 2 : 1;
}
inline bool hold(bool active, bool is_awake, int severity, bool& critical_latch) {
    if (severity >= 4) critical_latch = true;
    if (severity <= 2) critical_latch = false;
    return active && is_awake && !critical_latch;
}
}
