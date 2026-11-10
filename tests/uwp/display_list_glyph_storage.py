"""Check real glyph writes at Xbox pointer width, with the old ABI as a negative control."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import re

ROOT = Path(__file__).resolve().parents[2]


def select(source, names):
    """Select complete production bodies, without unrelated renderer entry points."""
    text = source.read_text(encoding="utf-8")
    functions = []
    for name in names:
        match = re.search(r"(?m)^[A-Za-z_][^;{}]*\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", text)
        if not match:
            raise RuntimeError(f"Cannot find production function {name} in {source}")
        body = text.index("{", match.start())
        depth, end = 1, body + 1
        while depth:
            if text[end] == "{": depth += 1
            elif text[end] == "}": depth -= 1
            end += 1
        functions.append(text[match.start():end])
    return "\n\n".join(functions) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("CC"))
    args = parser.parse_args()
    compiler = args.compiler or shutil.which("x86_64-w64-mingw32-clang")
    if not compiler:
        parser.error("provide --compiler with the x64 llvm-mingw clang path")
    scratch = ROOT / "tmp/uwp-regressions/display-list"
    scratch.mkdir(parents=True, exist_ok=True)
    core = scratch / "production-core.c"
    core.write_text(select(ROOT / "src/game/display_object_core.c", [
        "DisplayObject_FindFreeGeneralSlot", "DisplayObject_AcquireSlot",
        "DisplayObject_Release", "DisplayObject_ResetPool",
        "DisplayObject_ConfigureScreenSprite",
    ]), encoding="utf-8")
    spawn = scratch / "production-spawn.c"
    spawn.write_text(select(ROOT / "src/overlays/password/name_entry_runtime.c", [
        "NameEntry_SpawnGlyphSprite",
    ]), encoding="utf-8")
    flags = [
        str(compiler), "-std=gnu11", "-O0", "-g", "-fwrapv",
        "-ffunction-sections", "-fdata-sections", "-fno-strict-aliasing",
        "-fms-extensions", "-fno-jump-tables", "-mno-ms-bitfields",
        "-Wno-incompatible-pointer-types", "-Wno-pointer-to-int-cast",
        "-Wno-gnu-folding-constant", "-DMEMORIES_PC", "-D_LANGUAGE_C", "-DLANGUAGE_C",
        "-include", "src/pc/compat/ptr32.h", "-Isrc", "-Isrc/overlays/password",
        "tests/uwp/display_list_glyph_storage_test.c", "-Wl,--gc-sections",
        f'-DDISPLAY_LIST_CORE_SOURCE="{core.as_posix()}"',
        f'-DDISPLAY_LIST_SPAWN_SOURCE="{spawn.as_posix()}"',
        "-Wl,--image-base=0x40000000", "-Wl,--disable-dynamicbase", "-Wl,--disable-high-entropy-va",
    ]
    results = []
    for label, extra, expected in [
        ("fixed", [], 0),
        ("old-abi", ["-DDISPLAY_LIST_REPRODUCE_OLD_GLYPH_ABI"], 1),
    ]:
        target = scratch / f"glyph-{label}.exe"
        build = subprocess.run([*flags, *extra, "-o", str(target)], cwd=ROOT,
                               text=True, capture_output=True, timeout=120)
        (scratch / f"compile-{label}.log").write_text(build.stdout + build.stderr, encoding="utf-8")
        if build.returncode:
            raise RuntimeError(f"Compile {label} failed; see {scratch / f'compile-{label}.log'}\n{build.stderr[-6000:]}")
        run = subprocess.run([str(target)], cwd=ROOT, text=True, capture_output=True, timeout=30)
        output = run.stdout + run.stderr
        if run.returncode != expected:
            raise RuntimeError(f"{label} returned {run.returncode}, expected {expected}:\n{output}")
        if label == "old-abi" and "glyph spawner overwrote the following display slot" not in output:
            raise RuntimeError("The old ABI failed for an unrelated reason:\n" + output)
        print(output.strip())
        results.append(label + ": " + output)
    (scratch / "result.txt").write_text("\n".join(results), encoding="utf-8")
    print("PASS: the installed 1.0.25.0 glyph ABI is rejected by the same neighbor-integrity test")


if __name__ == "__main__":
    main()
