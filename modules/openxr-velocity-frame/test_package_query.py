"""Compile/run the production POSIX package query on Linux/WSL; no device I/O."""
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='gxr-package-query-') as temp:
    work = Path(temp)
    (work/'probe.cpp').write_text('''#include "package_query.h"
#include <cstdio>
int main(int argc, char** argv) {
    if (argc != 2) return 90;
    try { auto output = queryPackages(argv[1], 150); std::printf("%s", output.c_str()); return 0; }
    catch (const PackageUnavailable& e) { std::fprintf(stderr, "WAITING: %s", e.what()); return 3; }
    catch (const std::exception& e) { std::fprintf(stderr, "ERROR: %s", e.what()); return 1; }
}
''')
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(HERE/'native'), str(work/'probe.cpp'), '-o', str(work/'probe')], check=True)
    cases = [('success', "printf 'package:test.app uid:10123\\n'", 0),
             ('empty', 'exit 0', 0), ('failure', 'exit 1', 3),
             ('timeout', 'while :; do :; done', 3),
             ('no_stdout_then_hang', 'exec 1>&-; while :; do :; done', 3),
             ('large_response', "i=0; while [ $i -lt 2000 ]; do echo package:test.app$i uid:10123; i=$((i+1)); done", 0)]
    for name, body, expected in cases:
        command = work/name
        command.write_text('#!/bin/sh\n'+body+'\n')
        command.chmod(0o700)
        result = subprocess.run([str(work/'probe'), str(command)], capture_output=True, text=True, timeout=3)
        assert result.returncode == expected, (name, result.returncode, result.stderr)
        if name == 'success': assert result.stdout == 'package:test.app uid:10123\n'
        if name == 'large_response': assert len(result.stdout.splitlines()) == 2000
        if expected == 3: assert 'no target writes' in result.stderr and 'Success' not in result.stderr
        print('PASS:', name)
