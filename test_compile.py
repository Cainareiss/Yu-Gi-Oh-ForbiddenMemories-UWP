import os
import subprocess
import glob
import sys

CLANG = r"E:\yu gi oh uwp\tmp\pc\tools\llvm-mingw\llvm-mingw-20260922-ucrt-x86_64\bin\x86_64-w64-mingw32uwp-clang.exe"
FLAGS = [
    "-c",
    "-o", "NUL",
    "-w",
    "-O0",
    "-g",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fcommon",
    "-fno-stack-protector",
    "-DMEMORIES_PC",
    "-D_LANGUAGE_C",
    "-DLANGUAGE_C",
    "-Isrc",
    "-Wno-incompatible-pointer-types",
    "-Wno-int-conversion",
    "-Wno-pointer-to-int-cast",
    "-mno-ms-bitfields"
]

files = glob.glob("src/game/*.c")
print(f"Total files to test: {len(files)}")
failed = []
passed = 0

for f in files:
    cmd = [CLANG, f] + FLAGS
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        failed.append((f, res.stderr))
        print(f"FAILED: {f}")
        for line in res.stderr.splitlines()[:5]:
            print("  ", line)
    else:
        passed += 1

print(f"\nPassed: {passed}, Failed: {len(failed)}")
if failed:
    sys.exit(1)
