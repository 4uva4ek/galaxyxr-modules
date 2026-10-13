"""Exercise the actual Action/off/status scripts with a fake native controller."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1]
SHELL = os.environ.get('GXR_TEST_SHELL', 'C:/Program Files/Git/bin/bash.exe' if os.name == 'nt' else '/bin/sh')
STOCK = 'GXR_UPDATE_STATE=STOCK'


class UpdateContract(unittest.TestCase):
    def run_case(self, scenario, mode):
        with tempfile.TemporaryDirectory(dir=MODULE/'tests') as directory:
            root = Path(directory)
            (root/'bin').mkdir()
            for name in ('action.sh', 'auto.sh'):
                script = (MODULE/'module'/name).read_text()
                if name == 'auto.sh':
                    script = script.replace('sleep 1', ': # no delay in fake controller test')
                (root/name).write_text(script, newline='\n')
            fake = '''#!/bin/sh
case "$1" in
 stop)
  case "$SCENARIO" in
   orphan|restore_failure) exit 1 ;;
   delayed) echo 2 > "$TESTROOT/count" ;;
   *) ;;
  esac
  echo STOP_REQUESTED ;;
 restore)
  [ "$SCENARIO" != restore_failure ] || exit 1
  echo 0 > "$TESTROOT/count" ;;
 status)
  case "$SCENARIO" in
   error) echo 'ERROR cannot verify ownership'; exit 1 ;;
   active|timeout) echo 'ENABLED - QUALITY held'; exit 0 ;;
   misleading) echo 'DISABLED - No controller or owned override'; exit 1 ;;
   delayed)
    n=$(cat "$TESTROOT/count")
    if [ "$n" -gt 0 ]; then echo $((n-1)) > "$TESTROOT/count"; echo 'DISABLED - Stopped; ordinary policy restored'; exit 0; fi ;;
  esac
  echo 'DISABLED - No controller or owned override' ;;
 *) echo unexpected; exit 1 ;;
esac
'''
            helper = root/'bin/gxr-quality'
            helper.write_text(fake, newline='\n')
            helper.chmod(0o755)
            env = dict(os.environ, SCENARIO=scenario, TESTROOT='.')
            result = subprocess.run([SHELL, './action.sh', mode], cwd=root, env=env, text=True, capture_output=True, timeout=8)
            return result, (root/'disable').exists()

    def test_verified_status_only(self):
        for scenario in ('stock', 'active', 'error', 'misleading'):
            with self.subTest(scenario=scenario):
                result, disabled = self.run_case(scenario, 'status')
                self.assertEqual(STOCK in result.stdout, scenario == 'stock')
                self.assertEqual(result.returncode == 0, scenario in ('stock', 'active'))
                self.assertFalse(disabled)

    def test_stop_wait_restore_and_failure(self):
        for scenario in ('stock', 'delayed', 'orphan', 'timeout', 'restore_failure'):
            with self.subTest(scenario=scenario):
                result, disabled = self.run_case(scenario, 'off')
                success = scenario in ('stock', 'delayed', 'orphan')
                self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
                self.assertEqual(STOCK in result.stdout, success)
                self.assertTrue(disabled)

    def test_default_action_is_report_only(self):
        result, disabled = self.run_case('stock', 'report')
        self.assertEqual(result.returncode, 0)
        self.assertNotIn(STOCK, result.stdout)
        self.assertIn('Action does not activate', result.stdout)
        self.assertFalse(disabled)


if __name__ == '__main__':
    unittest.main(verbosity=2)
