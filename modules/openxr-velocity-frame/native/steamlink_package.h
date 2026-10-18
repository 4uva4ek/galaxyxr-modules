#pragma once
#include <string>

// Streaming client package families: the package must start with the first
// three labels of the client; anything after them (.x_23, .test, a renamed
// last label) is ignored. The APK version is not implied; each component keeps
// its own code pins.
inline bool packageFamily(const std::string& name, const std::string& base) {
    return name.compare(0, base.size(), base) == 0;
}
inline bool steamLinkPackage(const std::string& name) { return packageFamily(name, "com.valvesoftware.steamlinkvr"); }
inline bool chronoLinkPackage(const std::string& name) { return packageFamily(name, "dev.chronolink.client"); }
// Clients served by Steam Link Suite components that act only on the OpenXR runtime.
inline bool streamingClientPackage(const std::string& name) { return steamLinkPackage(name) || chronoLinkPackage(name); }
// Process names: "package:name" is a secondary process of the app, never a target.
inline bool mainProcess(const std::string& name) { return name.find(':') == std::string::npos; }
