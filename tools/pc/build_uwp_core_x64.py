#!/usr/bin/env python3
"""Build the entire game engine core as an x64 UWP static library: build_x64/memories_core.lib"""

import os, sys, glob, struct, subprocess, hashlib, csv, concurrent.futures, shutil, json

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(ROOT)

TOOLCHAIN_BIN = os.path.join(ROOT, "tmp", "pc", "tools", "llvm-mingw", "llvm-mingw-20260922-ucrt-x86_64", "bin")
CLANG = os.path.join(TOOLCHAIN_BIN, "x86_64-w64-mingw32uwp-clang.exe")
CLANGXX = os.path.join(TOOLCHAIN_BIN, "x86_64-w64-mingw32uwp-clang++.exe")
AR = os.path.join(TOOLCHAIN_BIN, "llvm-ar.exe")
NM = os.path.join(TOOLCHAIN_BIN, "llvm-nm.exe")
OBJCOPY = os.path.join(TOOLCHAIN_BIN, "llvm-objcopy.exe")
OBJDUMP = os.path.join(TOOLCHAIN_BIN, "llvm-objdump.exe")

BUILD_DIR = "build_x64"
OBJ_DIR = os.path.join(BUILD_DIR, "obj")
os.makedirs(OBJ_DIR, exist_ok=True)
RAW_DIR = os.path.join(BUILD_DIR, "raw")
os.makedirs(RAW_DIR, exist_ok=True)
# Object rewriting below is destructive. Never feed yesterday's rewritten
# symbols back into discovery, and invalidate compilation when headers change.
HEADER_DIGEST = hashlib.sha256()
for header in sorted(glob.glob("src/**/*.h", recursive=True)):
    HEADER_DIGEST.update(header.encode())
    with open(header, "rb") as handle:
        HEADER_DIGEST.update(handle.read())
HEADER_DIGEST = HEADER_DIGEST.hexdigest()

ADDRESSES = "config/pc/guest_addresses.txt"

MODULES = [
    ("main_menu", "src/overlays/main_menu/*.c", 0x0F, 0),
    ("password", "src/overlays/password/*.c", 0x15, 0x80168000),
    ("overworld", "src/overlays/overworld/*.c", 0x14, 0x80168000),
    ("free_duel", "src/overlays/free_duel/*.c", 0x13, 0x80168000),
]
MODULE_CONFIG = {"overworld": "overworld_before_coup"}

CFLAGS = [
    "-std=gnu11", "-w", "-O0", "-g", "-fno-strict-aliasing",
    "-fwrapv", "-fcommon", "-fno-stack-protector",
    "-DMEMORIES_PC", "-DMEMORIES_UWP", "-D_LANGUAGE_C", "-DLANGUAGE_C", "-Isrc",
    "-Wno-incompatible-pointer-types", "-Wno-int-conversion", "-Wno-pointer-to-int-cast",
    "-mno-ms-bitfields", "-include", "src/pc/compat/pgxp_game.h",
    "-mcmodel=large", "-fms-extensions"
]

NATIVE_CFLAGS = [
    "-std=gnu11", "-O2", "-g", "-w", "-fno-omit-frame-pointer", "-fno-strict-aliasing",
    "-DMEMORIES_PC", "-DMEMORIES_UWP", "-D_LANGUAGE_C", "-DLANGUAGE_C", "-Isrc",
    "-mno-ms-bitfields", "-DNO_FREETYPE", "-DNO_PNG",
    "-mcmodel=large", "-fms-extensions"
]

NATIVE_CXXFLAGS = [
    "-std=c++17", "-O2", "-g", "-w", "-fno-omit-frame-pointer", "-fno-strict-aliasing",
    "-DMEMORIES_PC", "-DMEMORIES_UWP", "-Isrc", "-mno-ms-bitfields"
]

HOST_LIBC = {
    "printf", "sprintf", "snprintf", "strcmp", "strcpy", "bzero", "qsort", "memcpy",
    "memset", "memmove", "strlen", "strcat", "strncmp", "strncpy", "memcmp", "malloc",
    "free", "realloc", "calloc", "abs", "atoi", "atol", "strchr", "strrchr", "strstr"
}

FRONTEND_HOOKS = {
    "UWP_LogMessage", "UWP_SetLastError", "UWP_OnPresent", "UWP_OnGetPadBits",
    "UWP_IsWidescreen", "UWP_GetSaveSlot", "UWP_ShouldQuit", "UWP_StartAudio"
}

def run(cmd):
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Error running command: {' '.join(cmd[:8])}")
        print(res.stderr)
        sys.exit(1)
    return res.stdout

def c_name(sym):
    # On x86_64 Windows PE+, C symbols do not have a leading underscore
    return sym if (sym and not sym.startswith(".")) else None

def symbols(objects):
    defined, tentative, undefined = set(), set(), set()
    for line in run([NM, "-g", *objects]).splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] in "UCTDBRVW" and not line.endswith(":") and c_name(parts[-1]):
            {"U": undefined, "C": tentative}.get(parts[-2], defined).add(c_name(parts[-1]))
    return defined, tentative, undefined

def rename_coff_sections(path, renames):
    with open(path, "rb") as handle:
        data = bytearray(handle.read())
    _, count, _, symbols_at, symbol_count, optional_size, _ = struct.unpack_from("<HHIIIHH", data, 0)
    strings_at = symbols_at + symbol_count * 18
    strings_size = struct.unpack_from("<I", data, strings_at)[0]
    extra = bytearray()
    for index in range(count):
        at = 20 + optional_size + index * 40
        field = bytes(data[at:at + 8]).rstrip(b"\0")
        if field.startswith(b"/"):
            start = strings_at + int(field[1:])
            field = bytes(data[start:data.index(b"\0", start)])
        new = renames.get(field.decode())
        if new is None:
            continue
        encoded = new.encode()
        if len(encoded) > 8:
            reference = f"/{strings_size + len(extra)}".encode()
            extra += encoded + b"\0"
            encoded = reference
        data[at:at + 8] = encoded.ljust(8, b"\0")
    if extra:
        data += extra
        struct.pack_into("<I", data, strings_at, strings_size + len(extra))
    with open(path, "wb") as handle:
        handle.write(data)

def unset_coff_commons(path, names):
    with open(path, "rb") as handle:
        data = bytearray(handle.read())
    _, _, _, symbols_at, symbol_count, _, _ = struct.unpack_from("<HHIIIHH", data, 0)
    strings_at = symbols_at + symbol_count * 18
    changed, index = False, 0
    while index < symbol_count:
        at = symbols_at + index * 18
        value, section, _, storage, auxiliary = struct.unpack_from("<IhHBB", data, at + 8)
        if section == 0 and value and storage == 2:  # IMAGE_SYM_CLASS_EXTERNAL
            raw = bytes(data[at:at + 8])
            if raw[:4] == b"\0\0\0\0":
                start = strings_at + struct.unpack_from("<I", raw, 4)[0]
                raw = bytes(data[start:data.index(b"\0", start)])
            sym = c_name(raw.rstrip(b"\0").decode())
            if sym in names:
                struct.pack_into("<I", data, at + 8, 0)
                changed = True
        index += 1 + auxiliary
    if changed:
        with open(path, "wb") as handle:
            handle.write(data)

def guest_addresses():
    tables, text, section = {}, (0, 0), None
    with open(ADDRESSES) as handle:
        for line in handle:
            parts = line.split()
            if not parts or parts[0].startswith("#"):
                continue
            if parts[0] == "text":
                text = (int(parts[1], 16), int(parts[2], 16))
            elif parts[0].startswith("["):
                section = tables.setdefault(parts[0][1:-1], {})
            else:
                section[parts[0]] = int(parts[1], 16)
    return tables["SLUS_014.11"], {name: tables.get(name, {}) for name, _, _, _ in MODULES}, text

def obj_path(source):
    clean = source.replace("/", "_").replace("\\", "_")
    return os.path.join(OBJ_DIR, f"{clean}.o")

def compile_c(job):
    src, obj, flags = job
    compiler = CLANGXX if src.endswith(".cpp") else CLANG
    raw = os.path.join(RAW_DIR, os.path.basename(obj))
    with open(src, "rb") as handle:
        digest = hashlib.sha256(handle.read() + json.dumps(
            [flags, HEADER_DIGEST, os.path.getmtime(compiler)]).encode()).hexdigest()
    stamp = raw + ".sha256"
    if os.path.exists(stamp):
        with open(stamp, "rb") as handle:
            cached = handle.read().decode("ascii", errors="ignore")
    else:
        cached = ""
    if not os.path.exists(raw) or cached != digest:
        run([compiler, "-c", src, "-o", raw] + flags)
        with open(stamp, "w") as handle:
            handle.write(digest)
    shutil.copyfile(raw, obj)

def main():
    print("=== Building Yu-Gi-Oh! Forbidden Memories Recompiled - UWP Core x64 ===")
    
    # 1. Collect sources
    resident_game = sorted(glob.glob("src/game/*.c"))
    resident_pc_game = sorted(glob.glob("src/pc/game/*.c"))
    resident = resident_game + resident_pc_game
    print(f"Resident sources: {len(resident)} files")

    module_sources = {name: sorted(glob.glob(pattern)) for name, pattern, _, _ in MODULES}
    total_mod_sources = sum(len(s) for s in module_sources.values())
    print(f"Overlay module sources: {total_mod_sources} files across {len(MODULES)} overlays")

    native_c = [
        "src/pc/guest/main.c",
        "src/pc/guest/image.c",
        "src/pc/guest/state.c",
        "src/pc/guest/modules.c",
        "src/pc/guest/setjmp_x64.S",
        "src/pc/rng.c",
        "src/pc/compat/fs.c",
        "src/pc/compat/gte.c",
        "src/pc/compat/pgxp.c",
        "src/pc/compat/libgs_ot.c",
        "src/pc/render/packets.c",
        "src/pc/render/soft_gpu.c",
        "src/pc/render/texture_dump.c",
        "src/pc/audio/spu.c",
        "src/pc/audio/replace.c",
        "src/pc/debug/log.c",
        "src/pc/debug/profile.c",
        "src/pc/debug/crash_uwp.c",
        "src/pc/platform/paths.c",
        "src/pc/platform/platform_common.c",
        "src/pc/cards/cards.c",
        "src/pc/cards/card_browse.c",
        "src/pc/cards/disc_art.c",
        "src/pc/cards/drops.c",
        "src/pc/cards/free_duel_progress.c",
        "src/pc/cards/free_duel_progress_view.c",
        "src/pc/cards/fusion.c",
        "src/pc/cards/fusion_helper.c",
        "src/pc/cards/library_panels.c",
        "src/pc/cards/passwords.c",
        "src/pc/cards/rank.c",
        "src/pc/cards/rank_meter.c",
        "src/pc/cards/starter.c",
        "src/pc/cards/tables.c",
        "src/pc/cards/art_uwp.c",
        "src/pc/saves/deck_menu.c",
        "src/pc/saves/deck_shop.c",
        "src/pc/saves/deck_slots.c",
        "src/pc/saves/save_cards.c",
        "src/pc/saves/save_menu.c",
        "src/pc/saves/save_slots.c",
        "src/pc/mods/mods.c",
        "src/pc/mods/hooks.c",
        "src/pc/mods/events.c",
        "src/pc/mods/json.c",
        "src/pc/mods/manager.c",
        "src/pc/mods/object_loader.c",
        "src/pc/sdk/deferred.c",
        "src/pc/sdk/libapi_uwp.c",
        "src/pc/sdk/libds.c",
        "src/pc/sdk/libetc.c",
        "src/pc/sdk/libgpu.c",
        "src/pc/sdk/libgs.c",
        "src/pc/sdk/libgs_unit.c",
        "src/pc/sdk/libgte.c",
        "src/pc/sdk/libgte_extra.c",
        "src/pc/sdk/libmcrd.c",
        "src/pc/sdk/libpress.c",
        "src/pc/sdk/libspu.c",
        "src/pc/platform/platform_uwp.c",
        "src/pc/free_duel/duelists.c",
        "src/pc/free_duel/page_box.c",
        "src/pc/cards/font_art.c",
        "src/pc/cards/rank_art.c",
        "src/pc/platform/game_files.c",
        "src/pc/platform/settings.c",
        "src/pc/platform/credits.c",
        "src/pc/platform/title_jump.c",
        "src/pc/platform/ai_trace.c",
        "src/pc/text/entry_layout.c",
        "src/pc/text/glyphs.c",
        "src/pc/text/language.c",
        "src/pc/text/listing.c",
        "src/pc/text/menu_cut.c",
        "src/pc/text/pal_text.c",
        "src/pc/text/translation.c",
        "src/pc/text/utf8.c",
        "src/pc/third_party/stb_vorbis.c",
        "src/pc/guest/mips.c",
        "src/pc/guest/resolve.c",
        "src/pc/guest/state_remap.c",
        "src/pc/debug/symbols.c",
        "src/pc/mods/mod_libc.c",
        "src/pc/uwp_stubs.c",
    ] + sorted(glob.glob("src/pc/overrides/*.c")) + sorted(glob.glob("src/pc/overlays/*.c"))
    
    native_cpp = []
    print(f"Native port sources: {len(native_c)} C files")

    # 2. Compile resident & modules & native
    game_sources = resident + [s for m in module_sources.values() for s in m]
    compile_jobs = [(s, obj_path(s), CFLAGS) for s in game_sources]
    compile_jobs += [(s, obj_path(s), NATIVE_CFLAGS) for s in native_c]
    compile_jobs += [(s, obj_path(s), NATIVE_CXXFLAGS) for s in native_cpp]

    print(f"Compiling {len(compile_jobs)} units in parallel...")
    # The LLVM UWP toolchain becomes unreliable when hundreds of clang
    # processes are launched on high-core-count hosts. Keep a bounded queue;
    # this is still parallel, but deterministic enough for repeated builds.
    with concurrent.futures.ThreadPoolExecutor(min(os.cpu_count() or 4, 8)) as pool:
        results = list(pool.map(compile_c, compile_jobs))
    print("Compilation completed successfully!")

    # 3. Handle Overlay Symbol Renaming
    resident_elf, module_elf, text = guest_addresses()
    module_symbols = {name: symbols([obj_path(s) for s in module_sources[name]]) for name, _, _, _ in MODULES}
    resident_defined = symbols([obj_path(s) for s in resident])[0]
    renamed, sections = {}, {}
    for name, _, _, bank in MODULES:
        if not bank:
            continue
        defined, common, wanted_here = module_symbols[name]
        others = [other for other, _, _, _ in MODULES if other != name]
        clash = {symbol for symbol in defined | common
                 if symbol in resident_defined or any(symbol in module_symbols[o][0] | module_symbols[o][1] for o in others)}
        clash |= {symbol for symbol in (wanted_here | common) - defined
                  if symbol in module_elf[name] and symbol not in resident_defined and any(
                      places.get(symbol, module_elf[name][symbol]) != module_elf[name][symbol]
                      for places in [resident_elf] + [module_elf[o] for o in others])}
        renamed[name] = {symbol: f"{name}__{symbol}" for symbol in clash if not symbol.startswith(f"{name}__")}
        for source in module_sources[name]:
            obj = obj_path(source)
            command = [OBJCOPY]
            for old, new in sorted(renamed[name].items()):
                command.append(f"--redefine-sym={old}={new}")
            rename_coff_sections(obj, {".data": f"ovl_{name}_data$m", ".bss": f"ovl_{name}_bss$m"})
            if len(command) > 1:
                run(command + [obj])
        headers_text = run([OBJDUMP, "-h", *[obj_path(s) for s in module_sources[name]]])
        sections[name] = [kind for kind in ("data", "bss") if f"ovl_{name}_{kind}" in headers_text]

    renames_file = "config/pc/host_symbol_renames.txt"
    def prepare_game(source):
        obj = obj_path(source)
        if os.path.exists(renames_file):
            run([OBJCOPY, f"--redefine-syms={renames_file}", obj])
        rename_coff_sections(obj, {".text": "game_text$m", ".rdata": "game_rodata$m",
                                   ".data": "game_data$m", ".bss": "game_bss$m"})
    with concurrent.futures.ThreadPoolExecutor(min(os.cpu_count() or 4, 8)) as pool:
        list(pool.map(prepare_game, game_sources))

    # 4. Resolve symbols and stubs
    native_objs = [obj_path(s) for s in native_c + native_cpp]
    game_objs = [obj_path(s) for s in game_sources]
    game_defined, tentative, undefined = symbols(game_objs)
    native_defined, _, native_undefined = symbols(native_objs)

    with open("config/slus_01411/functions.csv") as handle:
        rows = list(csv.DictReader(handle))
    functions = {row["name"]: row["status"] for row in rows}
    overlay_rows = []
    for name, _, identifier, bank in MODULES:
        csv_path = f"config/slus_01411/overlays/{MODULE_CONFIG.get(name, name)}_functions.csv"
        if os.path.exists(csv_path):
            with open(csv_path) as handle:
                for row in csv.DictReader(handle):
                    row["name"] = renamed.get(name, {}).get(row["name"], row["name"])
                    row["bank"], row["identifier"] = bank, identifier if bank else 0
                    overlay_rows.append(row)
    by_address = {int(row["address"], 16): row["name"] for row in rows}
    addresses = dict(resident_elf)
    for name, _, _, _ in MODULES:
        for symbol, address in module_elf[name].items():
            addresses.setdefault(renamed.get(name, {}).get(symbol, symbol), address)

    # These data tables are generated below in stubs.c.  They can legitimately
    # be referenced by newly compiled game objects, so never turn them into
    # fake function stubs during the undefined-symbol pass.
    generated_data = {"Memories_FunctionMap", "Memories_FunctionMapCount",
                      "Memories_Modules", "Memories_ModuleCount"}
    wanted = (undefined | tentative) - game_defined - native_defined - HOST_LIBC - FRONTEND_HOOKS - generated_data
    wanted = {name for name in wanted if not name.startswith("UWP_")}
    pinned, stubs, unknown, aliases = {}, [], [], {}
    for name in sorted(wanted):
        address = addresses.get(name)
        current = by_address.get(address if address is not None else
                                 int(name[5:], 16) if name.startswith("func_8") and len(name) == 13 else -1)
        if current and current != name and current in game_defined | native_defined:
            aliases[name] = current
            continue
        if name in functions or (address is not None and text[0] <= address < text[1]):
            stubs.append(name)
        elif address is not None:
            pinned[name] = address
        else:
            unknown.append(name)

    for name in native_undefined - game_defined - native_defined:
        if name.startswith("D_8") and name in addresses:
            pinned[name] = addresses[name]
    stubs += [name for name in unknown if name in undefined and name not in FRONTEND_HOOKS and
              name not in generated_data and not name.startswith("UWP_")]

    print(f"Symbols summary: {len(pinned)} pinned addresses, {len(stubs)} stubs, {len(aliases)} aliases")

    # 5. Generate guest_symbols.s (x64 COFF)
    guest_syms_path = os.path.join(BUILD_DIR, "guest_symbols.s")
    with open(guest_syms_path, "w") as handle:
        handle.writelines(f".globl {name}\n.set {name}, 0x{address:08X}\n" for name, address in sorted(pinned.items()))

    for source in game_sources:
        unset_coff_commons(obj_path(source), set(pinned))

    if aliases:
        aliases_path = os.path.join(BUILD_DIR, "aliases.txt")
        with open(aliases_path, "w") as handle:
            handle.writelines(f"{name} {target}\n" for name, target in sorted(aliases.items()))
        # objcopy ignores names absent from an object; no per-object nm needed.
        with concurrent.futures.ThreadPoolExecutor(min(os.cpu_count() or 4, 8)) as pool:
            list(pool.map(lambda source: run([OBJCOPY, f"--redefine-syms={aliases_path}", obj_path(source)]), game_sources))

    # 6. Generate section_markers.s
    section_markers_path = os.path.join(BUILD_DIR, "section_markers.s")
    with open(section_markers_path, "w") as handle:
        marked = [("game_text", "xr"), ("game_data", "dw"), ("game_bss", "bw")]
        marked += [(f"ovl_{name}_{kind}", "dw" if kind == "data" else "bw")
                   for name, _, _, bank in MODULES if bank for kind in sections.get(name, [])]
        for section, flags in marked:
            handle.write(f'.section {section}$a,"{flags}"\n.globl __start_{section}\n__start_{section}:\n')
            handle.write(f'.section {section}$z,"{flags}"\n.globl __stop_{section}\n__stop_{section}:\n')

    # 7. Generate stubs.c
    digest = hashlib.sha256(b"uwp_core_x64").hexdigest()[:8]
    stubs_path = os.path.join(BUILD_DIR, "stubs.c")
    with open(stubs_path, "w") as handle:
        handle.write('#include "pc/guest/image.h"\n')
        handle.write(f"const unsigned Memories_GameFingerprint = 0x{digest}u;\n")
        handle.writelines(f'void {name}(void) {{ Memories_Unimplemented("{name}"); }}\n'
                          for name in sorted(stubs))
        linked = game_defined | native_defined | set(stubs) | set(aliases)
        mapped = [(int(row["address"], 16), row["name"], row.get("bank", 0), row.get("identifier", 0))
                  for row in rows + overlay_rows if row["name"] in linked]
        mapped += [(0x8013A004, "Memories_ModelPrimaryControlA", 0, 0),
                   (0x8013B004, "Memories_ModelVariantControlA", 0, 0),
                   (0x801462B0, "Memories_DuelEffectControl", 0, 0),
                   (0x8017A004, "Memories_ModelPrimaryControlB", 0, 0),
                   (0x8017B004, "Memories_ModelVariantControlB", 0, 0)]
        mapped.sort()
        handle.writelines(f"extern void {name}(void);\n" for name in sorted({m[1] for m in mapped} - set(stubs)))
        handle.write("const MemoriesGuestFunction Memories_FunctionMap[] = {\n")
        handle.writelines(f"    {{0x{address:08X}u, {name}, 0x{bank:08X}u, 0x{identifier:X}u}},\n"
                          for address, name, bank, identifier in mapped)
        handle.write(f"}};\nconst unsigned Memories_FunctionMapCount = {len(mapped)};\n")
        shared = [(name, identifier, bank) for name, _, identifier, bank in MODULES if bank]
        for name, _, _ in shared:
            for kind in sections.get(name, []):
                handle.write(f"extern char __start_ovl_{name}_{kind}[], __stop_ovl_{name}_{kind}[];\n")
        handle.write("const MemoriesModule Memories_Modules[] = {\n")
        for name, identifier, bank in shared:
            ranges = [f"__start_ovl_{name}_{kind}, __stop_ovl_{name}_{kind}" if kind in sections.get(name, []) else "0, 0"
                      for kind in ("data", "bss")]
            handle.write(f'    {{"{name}", 0x{bank:08X}u, 0x{identifier:X}u, {", ".join(ranges)}}},\n')
        handle.write(f"}};\nconst unsigned Memories_ModuleCount = {len(shared)};\n")

    # 8. Compile generated stubs & asm
    run([CLANG, "-c", guest_syms_path, "-o", os.path.join(BUILD_DIR, "guest_symbols.o")])
    run([CLANG, "-c", section_markers_path, "-o", os.path.join(BUILD_DIR, "section_markers.o")])
    run([CLANG, *NATIVE_CFLAGS, "-c", stubs_path, "-o", os.path.join(BUILD_DIR, "stubs.o")])

    # 9. Archive into memories_core.lib
    lib_path = os.path.join(BUILD_DIR, "memories_core.lib")
    all_objs = game_objs + native_objs + [
        os.path.join(BUILD_DIR, "guest_symbols.o"),
        os.path.join(BUILD_DIR, "section_markers.o"),
        os.path.join(BUILD_DIR, "stubs.o")
    ]
    print(f"Archiving {len(all_objs)} object files into {lib_path}...")
    
    # Write response file to avoid Windows command line length limit (32k)
    rsp_path = os.path.join(BUILD_DIR, "objects.rsp")
    with open(rsp_path, "w") as handle:
        handle.write("\n".join(f'"{os.path.abspath(o).replace(os.sep, "/")}"' for o in all_objs))

    run([AR, "rcs", lib_path, f"@{rsp_path}"])
    size_mb = os.path.getsize(lib_path) / (1024 * 1024)
    print(f"SUCCESS: Created {lib_path} ({size_mb:.2f} MB)")

if __name__ == "__main__":
    main()
