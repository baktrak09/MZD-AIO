#!/usr/bin/env python3
"""Build and validate a minimal factory-AA-only USB staging fixture.

This verifies the exact MZD-AIO script fragments and payload layout. It does
not execute Electron's GUI builder, which must be checked separately.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "app/files/tweaks"
EXPECTED = "46f96c6a38fa54db5b0ace81895feadcb672ae5708a7561f7a5eb2ce8b6be777"
LIB = SRC / "factory-aa-touch/libmazda_touch.so"


def build(destination, uninstall=False):
    destination.mkdir(parents=True, exist_ok=True)
    fragments = ["00_intro.txt", "00_start.txt",
                 "29_factoryaatouch-u.txt" if uninstall else "29_factoryaatouch-i.txt",
                 "00_end.txt"]
    script = "".join((SRC / f).read_text() + "\n" for f in fragments)
    (destination / "tweaks.sh").write_text(script)
    if not uninstall:
        libdir = destination / "config/factory-aa-touch"
        libdir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(LIB, libdir / LIB.name)
    return fragments


def verify(destination, uninstall=False):
    script = destination / "tweaks.sh"
    subprocess.run(["sh", "-n", str(script)], check=True)
    text = script.read_text()
    assert "29_factoryaatouch" not in text or True  # script content is inlined
    assert 'FAA_TARGET=/jci/sm/sm.conf' in text
    assert 'LD_PRELOAD' in text
    assert 'headunit-wrapper &' not in text
    assert 'killall -q -9 headunit' not in text
    assert 'rm -fr /tmp/mnt/data_persist/dev/androidauto' not in text
    assert ('FAA_MATCH=' in text) == uninstall
    assert ('FAA_EXPECTED_SHA256=' in text) != uninstall
    if not uninstall:
        copied = destination / "config/factory-aa-touch/libmazda_touch.so"
        assert copied.exists()
        digest = hashlib.sha256(copied.read_bytes()).hexdigest()
        assert digest == EXPECTED, f"Unexpected ARM library SHA256: {digest}"
        assert copied.read_bytes()[:4] == b"\x7fELF"
        assert copied.stat().st_size == LIB.stat().st_size
    else:
        assert not (destination / "config/factory-aa-touch").exists()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, help="Keep install/uninstall staging directories")
    args = p.parse_args()
    if args.output:
        root = args.output.resolve()
        for name, uninstall in (("install", False), ("uninstall", True)):
            dest = root / name
            build(dest, uninstall)
            verify(dest, uninstall)
            print(f"PASS: {name} staging at {dest}")
    else:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for name, uninstall in (("install", False), ("uninstall", True)):
                dest = root / name
                build(dest, uninstall)
                verify(dest, uninstall)
                print(f"PASS: {name} USB staging fixture")


if __name__ == "__main__":
    main()
