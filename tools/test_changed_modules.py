"""Plan rules for the CI build matrix; offline against the real tree."""
import unittest

from changed_modules import EXCLUDED, build_all, plan, registry

ENTRIES = registry()


class PlanTests(unittest.TestCase):
    def build_names(self, *changed):
        builds, _, _ = plan(changed, ENTRIES)
        return [build['module'] for build in builds]

    def test_module_file_change_builds_only_that_module(self):
        self.assertEqual(self.build_names('modules/ota-control/module/service.sh'), ['ota-control'])
        self.assertEqual(self.build_names('modules/haptic-main/module/module.prop'), ['haptic-main'])

    def test_two_module_changes_build_both(self):
        self.assertEqual(self.build_names('modules/controller-whitening/native/main.cpp', 'modules/haptic-main/README.md'),
                         ['controller-whitening', 'haptic-main'])

    def test_shared_tools_file_builds_its_declared_consumers(self):
        self.assertEqual(self.build_names('tools/cold-boot-reset.sh'),
                         ['controller-timestamp-precision', 'controller-whitening', 'hal-cache-bypass', 'haptic-main'])
        self.assertEqual(self.build_names('tools/action-report.sh'), ['haptic-main'])

    def test_native_builder_change_rebuilds_every_native_module(self):
        self.assertEqual(self.build_names('tools/build.py'),
                         ['controller-timestamp-precision', 'controller-whitening', 'hal-cache-bypass', 'haptic-main'])

    def test_metadata_docs_and_pipeline_changes_build_nothing(self):
        self.assertEqual(self.build_names('changelogs/ota-control.md', 'modules.json', 'updates/ota-control.json',
                                          'docs/CI.md', 'README.md', '.github/workflows/build-modules.yml',
                                          'tools/changed_modules.py'), [])

    def test_excluded_modules_never_build_and_report_their_reason(self):
        for name, reason in EXCLUDED.items():
            builds, excluded, _ = plan([f'modules/{name}/module/service.sh'], ENTRIES)
            self.assertEqual(builds, [])
            self.assertEqual(excluded, [{'module': name, 'reason': reason}])

    def test_unknown_module_is_reported_not_built(self):
        builds, excluded, unchanged = plan(['modules/mystery/x'], {'mystery': None})
        self.assertEqual((builds, unchanged), ([], []))
        self.assertEqual(excluded, [{'module': 'mystery', 'reason': 'no CI build entry'}])

    def test_every_module_directory_is_covered(self):
        # Deliberate canary: extend it when a module is added or its CI entry changes.
        self.assertEqual(sorted(n for n, e in ENTRIES.items() if e and 'excluded' not in e),
                         ['controller-timestamp-precision', 'controller-whitening', 'hal-cache-bypass',
                          'haptic-main', 'ota-control'])
        self.assertEqual(sorted(n for n, e in ENTRIES.items() if e and 'excluded' in e), sorted(EXCLUDED))
        self.assertEqual([n for n, e in ENTRIES.items() if e is None], [])

    def test_full_build_covers_buildable_and_reports_excluded(self):
        builds, excluded, unchanged = build_all(ENTRIES)
        self.assertEqual([build['module'] for build in builds],
                         ['controller-timestamp-precision', 'controller-whitening', 'hal-cache-bypass',
                          'haptic-main', 'ota-control'])
        self.assertEqual(sorted(item['module'] for item in excluded), sorted(EXCLUDED))
        self.assertEqual(unchanged, [])

    def test_matrix_entries_carry_the_build_metadata(self):
        builds, _, _ = plan(['modules/ota-control/module/ota-ctl.sh'], ENTRIES)
        self.assertEqual(builds[0]['command'], 'python3 modules/ota-control/build.py')
        self.assertTrue(builds[0]['artifact'].startswith('gxr-ota-control-'))
        self.assertFalse(builds[0]['needs_ndk'])
        native, _, _ = plan(['modules/haptic-main/module/action.sh'], ENTRIES)
        self.assertEqual(native[0]['command'], 'python3 tools/build.py haptic-main --ndk "$ANDROID_NDK_HOME" --variant auto')
        self.assertTrue(native[0]['needs_ndk'])

    def test_shared_and_own_change_reports_both_reasons(self):
        builds, _, _ = plan(['modules/haptic-main/module/action.sh', 'tools/cold-boot-reset.sh'], ENTRIES)
        self.assertEqual([build['module'] for build in builds],
                         ['controller-timestamp-precision', 'controller-whitening', 'hal-cache-bypass', 'haptic-main'])
        haptic = builds[-1]
        self.assertIn('tools/cold-boot-reset.sh', haptic['why'])
        self.assertIn('modules/haptic-main/', haptic['why'])


if __name__ == '__main__':
    unittest.main()
