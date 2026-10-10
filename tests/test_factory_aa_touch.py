#!/usr/bin/env python3
"""Host-side smoke tests for the isolated factory AA touch USB tweak.

No CMU required. Runs the actual generated-script fragments against a fake
filesystem, never against /jci or /tmp/mnt/data_persist on the host.
"""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TWEAKS = ROOT / "app/files/tweaks"
LIBRARY = TWEAKS / "factory-aa-touch/libmazda_touch.so"


class FactoryTouchTests(unittest.TestCase):
    def test_payload_and_generator(self):
        self.assertTrue(LIBRARY.is_file())
        self.assertEqual(LIBRARY.read_bytes()[:4], b"\\x7fELF")
        generator = (ROOT / "app/assets/js/build-tweaks.js").read_text()
        for needle in ("options.includes(29)", "options.includes(129)",
                       "29_factoryaatouch-i.txt", "29_factoryaatouch-u.txt",
                       "factory-aa-touch/libmazda_touch.so"):
            self.assertIn(needle, generator)

    def test_install_uninstall_reinstall(self):
        with tempfile.TemporaryDirectory() as td:
            temp = pathlib.Path(td)
            jci = temp / "jci/sm"
            jci.mkdir(parents=True)
            target = jci / "sm.conf"
            persist = temp / "data_persist/mazda-touch"
            source = temp / "usb/config/factory-aa-touch"
            source.mkdir(parents=True)
            (source / "libmazda_touch.so").write_bytes(LIBRARY.read_bytes())
            original = ('<service name="other" path="/x">\\n'
                        '</service>\\n'
                        '<service\\n name="jciAAPA" path="/jci/aapa/blmjciaapa.so">\\n'
                        '  <environ_var env_name="EXISTING" env_value="yes"/>\\n'
                        '</service>\\n')
            target.write_text(original)
            def run(name, firmware="70.00.335C"):
                code = (TWEAKS / name).read_text()
                code = code.replace("/jci/sm/sm.conf", str(target))
                code = code.replace("/tmp/mnt/data_persist/mazda-touch", str(persist))
                code = code.replace("/data_persist/mazda-touch/libmazda_touch.so",
                                    str(persist / "libmazda_touch.so"))
                code = code.replace('FAA_SOURCE="${MYDIR}/config/factory-aa-touch/libmazda_touch.so"',
                                    'FAA_SOURCE="' + str(source / "libmazda_touch.so") + '"')
                prelude = ('show_message() { :; }\\nlog_message() { :; }\\n'
                           "get_cmu_sw_version() { echo '" + firmware + "'; }\\n")
                return subprocess.run(["sh", "-c", prelude + code],
                                      capture_output=True, text=True)
            install = "29_factoryaatouch-i.txt"
            uninstall = "29_factoryaatouch-u.txt"
            self.assertNotEqual(run(install, "70.00.324A").returncode, 0)
            self.assertEqual(target.read_text(), original)
            result = run(install)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(target.read_text().count("LD_PRELOAD"), 1)
            self.assertTrue((persist / "sm.conf.original").exists())
            self.assertNotEqual(run(install).returncode, 0)
            result = run(uninstall)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(target.read_text(), original)
            result = run(install)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(target.read_text().count("LD_PRELOAD"), 1)

    def test_refuse_existing_preload(self):
        script = (TWEAKS / "29_factoryaatouch-i.txt").read_text()
        self.assertIn('inside && /env_name="LD_PRELOAD"/', script)


if __name__ == "__main__":
    unittest.main()
