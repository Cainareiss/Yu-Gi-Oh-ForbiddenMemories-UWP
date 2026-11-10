"""Check the Xbox-width keyboard with the user's own disc and real motion code.

No game data is distributed: the tiny navigation fixture is read from WA_MRG.
The unsigned-load negative control must fail, so this also witnesses the
specific regression from the former UWP pointer-safety patch.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def run(args, *, expected=0):
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True, timeout=120)
    if result.returncode != expected:
        log = ROOT / "tmp/uwp-regressions/name-cursor/last-failure.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        raise RuntimeError(
            f"Unexpected exit {result.returncode}: {args[0]}; full log: {log}\n"
            f"{(result.stdout + result.stderr)[-6000:]}"
        )
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--compiler", default=os.environ.get("CC"))
    options = parser.parse_args()
    compiler = options.compiler or shutil.which("x86_64-w64-mingw32-clang")
    if not compiler:
        parser.error("provide --compiler with the x64 llvm-mingw clang path")
    sys.path.insert(0, str(ROOT / "tools/pc/fm_editor"))
    from disc import DiscImage

    scratch = ROOT / "tmp/uwp-regressions/name-cursor"
    scratch.mkdir(parents=True, exist_ok=True)
    fixture = scratch / "navigation-from-user-disc.bin"
    with DiscImage(str(options.disc.resolve())) as disc:
        wa = disc.find("DATA/WA_MRG.MRG")
        module = disc.read(wa[0] + 7968, 15 * 2048)
    if int.from_bytes(module[:4], "little") != 0x15:
        raise RuntimeError("The supplied disc does not contain the expected password overlay")
    fixture.write_bytes(module[0x2B38:0x2B38 + 135] + module[0x2BC0:0x2BD2])

    # COFF resolves references in unused renderer sections before GC. Compile
    # the motion functions verbatim from production instead of pulling the
    # unrelated GPU dependencies into this geometry test.
    helpers = (ROOT / "src/game/display_object_helpers.c").read_text(encoding="utf-8")
    begin = helpers.index("void DisplayObject_ResetVelocity(")
    end = helpers.index("void DisplayObject_StepPositionXYZ(", begin)
    motion = scratch / "production-motion.c"
    motion.write_text('#include "game/display_object_helpers.h"\n' + helpers[begin:end],
                      encoding="utf-8")

    flags = [
        str(compiler), "-std=gnu11", "-O0", "-g", "-fwrapv",
        "-ffunction-sections", "-fdata-sections", "-fno-strict-aliasing",
        "-fms-extensions", "-fno-jump-tables", "-mno-ms-bitfields",
        "-Wno-incompatible-pointer-types", "-Wno-pointer-to-int-cast",
        "-Wno-gnu-folding-constant",
        "-DMEMORIES_PC", "-D_LANGUAGE_C", "-DLANGUAGE_C",
        "-include", "src/pc/compat/ptr32.h",
        "-Isrc", "-Isrc/overlays/password",
        "tests/uwp/name_cursor_geometry_test.c",
        str(motion),
        "-Wl,--gc-sections", "-Wl,--image-base=0x40000000",
        "-Wl,--disable-dynamicbase", "-Wl,--disable-high-entropy-va",
    ]
    fixed = scratch / "cursor-fixed.exe"
    result = run([*flags, "-o", str(fixed)])
    (scratch / "compile-fixed.log").write_text(result.stderr, encoding="utf-8")
    fixed_result = run([str(fixed), str(fixture)])
    print(fixed_result.stdout.strip())

    source = ROOT / "src/overlays/password/name_entry_keyboard_update.c"
    broken = source.read_text(encoding="utf-8")
    if broken.count("*(s8 *G32)") != 2:
        raise RuntimeError("Controller changed: review the unsigned-load negative control")
    broken = broken.replace("*(s8 *G32)", "*(u8 *G32)")
    mutation = scratch / "unsigned-descriptor-regression.c"
    mutation.write_text(broken, encoding="utf-8")
    mutant = scratch / "cursor-unsigned-regression.exe"
    include = f'-DNAME_ENTRY_SOURCE="{mutation.as_posix()}"'
    result = run([*flags, include, "-o", str(mutant)])
    (scratch / "compile-negative-control.log").write_text(result.stderr, encoding="utf-8")
    result = run([str(mutant), str(fixture)], expected=1)
    if "selected an invalid/invisible destination" not in result.stderr:
        raise RuntimeError("Negative control failed for an unrelated reason:\n" + result.stderr)
    print("PASS: the prior unsigned-descriptor bug is detected by the same test")
    (scratch / "result.txt").write_text(
        "Production controller: " + fixed_result.stdout
        + "Prior unsigned-load regression: expected failure\n" + result.stderr,
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
