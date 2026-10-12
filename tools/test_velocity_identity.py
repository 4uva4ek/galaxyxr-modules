"""Compile the production process identity parser; exercise real process exits on Linux."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VelocityIdentity(unittest.TestCase):
    def test_production_identity_with_live_zombie_reaped_and_reused_processes(self):
        if os.name != 'posix':
            self.skipTest('Run this native lifecycle test on Linux/WSL')
        compiler = shutil.which('c++')
        self.assertIsNotNone(compiler, 'C++ compiler required')
        code = r'''
#include "process_identity.h"
#include <cassert>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
int main() {
    const auto path=[](int pid){return "/proc/"+std::to_string(pid)+"/stat";};
    auto self=readProcessIdentity(path(getpid()),{});
    assert(self.state==ProcessState::Alive && decimalIdentity(self.started));
    assert(readProcessIdentity(path(getpid()),self.started+"1").state==ProcessState::Gone);
    assert(readProcessIdentity("/definitely-not-an-existing-proc-stat",{}).state==ProcessState::Gone);
    const auto stat=[](char state){return std::string("123 (name with ) spaces) ")+state+
        " 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 12345";};
    assert(parseProcessIdentity(stat('S'),"12345").state==ProcessState::Alive);
    for(char state: {'Z','X','x'})assert(parseProcessIdentity(stat(state),"12345").state==ProcessState::Gone);
    for(const auto& invalid: {std::string(),std::string("123 no delimiter"),std::string("123 (x) S 0"),stat('S')+"x"}) {
        bool rejected=false;
        try{(void)parseProcessIdentity(invalid,{});}catch(const ProcessAccessError&){rejected=true;}
        assert(rejected);
    }
    assert(!decimalIdentity("") && !decimalIdentity("1 x") && !decimalIdentity("-1"));
    const pid_t child=fork();assert(child>=0);
    if(child==0){for(;;)pause();}
    const auto before=readProcessIdentity(path(child),{});assert(before.state==ProcessState::Alive);
    assert(kill(child,SIGKILL)==0); // Our own disposable test child, never an Android app.
    siginfo_t info{};assert(waitid(P_PID,child,&info,WEXITED|WNOWAIT)==0);
    assert(readProcessIdentity(path(child),before.started).state==ProcessState::Gone);
    assert(waitpid(child,nullptr,0)==child);
    assert(readProcessIdentity(path(child),before.started).state==ProcessState::Gone);
    bool unreadable=false;
    try{(void)readProcessIdentity("/proc",{});}catch(const ProcessAccessError&){unreadable=true;}
    assert(unreadable);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            source = work/'identity.cpp'
            source.write_text(code)
            binary = work/'identity'
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT/'modules/openxr-velocity-frame/native'),
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
