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

overlay_files = glob.glob("src/overlays/**/*.c", recursive=True)
print(f"Total overlay files to test: {len(overlay_files)}")
failed = []
passed = 0

for f in overlay_files:
    cmd = [CLANG, f] + FLAGS
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        failed.append((f, res.stderr))
        print(f"FAILED: {f}")
        for line in res.stderr.splitlines()[:5]:
            print("  ", line)
    else:
        passed += 1

print(f"\nOverlay files: Passed: {passed}, Failed: {len(failed)}")

pc_files = glob.glob("src/pc/**/*.c", recursive=True)
print(f"\nTotal pc files to test: {len(pc_files)}")
pc_failed = []
pc_passed = 0

for f in pc_files:
    cmd = [CLANG, f] + FLAGS
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        pc_failed.append((f, res.stderr))
        print(f"FAILED: {f}")
        for line in res.stderr.splitlines()[:5]:
            print("  ", line)
    else:
        pc_passed += 1

print(f"\nPC files: Passed: {pc_passed}, Failed: {len(pc_failed)}")
