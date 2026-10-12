#pragma once
#include <cerrno>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>

// Exit/reuse is distinct from unreadable or malformed identity evidence.
enum class ProcessState { Alive, Gone };
struct ProcessIdentity { ProcessState state; std::string started; };
struct ProcessAccessError : std::runtime_error { using std::runtime_error::runtime_error; };
struct ProcessGone : std::runtime_error { using std::runtime_error::runtime_error; };

inline bool decimalIdentity(const std::string& text) {
    return !text.empty() && text.find_first_not_of("0123456789")==std::string::npos;
}
inline ProcessIdentity parseProcessIdentity(const std::string& text, const std::string& expected) {
    const auto end=text.rfind(')');
    if(end==std::string::npos||end+2>=text.size())throw ProcessAccessError("Malformed process identity");
    std::istringstream fields(text.substr(end+2));std::string state,value;
    if(!(fields>>state)||state.size()!=1)throw ProcessAccessError("Malformed process state");
    for(int i=0;i<19;++i)
        if(!(fields>>value))throw ProcessAccessError("Short process identity");
    if(!decimalIdentity(value))throw ProcessAccessError("Malformed process start time");
    return {(state=="Z"||state=="X"||state=="x"||(!expected.empty()&&value!=expected))
        ?ProcessState::Gone:ProcessState::Alive,value};
}
inline ProcessIdentity readProcessIdentity(const std::string& path, const std::string& expected) {
    FILE* file=fopen(path.c_str(),"re");
    if(!file) {
        if(errno==ENOENT||errno==ESRCH)return {ProcessState::Gone,{}};
        throw ProcessAccessError("Cannot read process identity");
    }
    char buffer[4096];const size_t count=fread(buffer,1,sizeof(buffer),file);
    const bool bad=ferror(file)||count==sizeof(buffer);fclose(file);
    if(bad)throw ProcessAccessError("Cannot verify process identity");
    return parseProcessIdentity(std::string(buffer,count),expected);
}
