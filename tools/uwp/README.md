# Native UWP core

This backend builds the upstream Windows x64 G32 core with the original
resident/overlay inventory, fixed guest symbols, module bank renames,
indirect branch gates, interpreter and saved-stack/context implementation.
Only the window, filesystem, diagnostics and entry adapters are replaced.
The game runs with the cooperative clock; no injected timer interrupts.

Build with the verified llvm-mingw toolchain (see `prepare_toolchain.py`):

```powershell
python tools/uwp/build_native.py --toolchain 'E:\yu gi oh uwp\toolchains\llvm-mingw-20260922-ucrt-x86_64' --headless
```

Outputs are in `tmp/uwp-native`: `memories-uwp-core.lib`, `native-link.props`,
`memories-uwp-headless.exe`, target-specific bundled `mods`, `languages`, SDK
and `link-info.json`. The console executable is a test harness only and
must not be packaged as the UWP application.

Import `native-link.props` after the host project's link definitions. It
selects LLVM's COFF linker with `/lldmingw`, keeps the upstream section
markers and link order, and explicitly links the MSVC/UCRT/WinRT libraries.
MSVC `link.exe` adds the image base to RIP-relative references to the
absolute guest symbols: a tested reference to `0x8009B098` became
`0xC009B098` and crashed during startup. Do not replace this linker with
`link.exe` or remove `/lldmingw` to suppress a build warning. The host PE
must retain `/BASE:0x40000000 /DYNAMICBASE:NO /HIGHENTROPYVA:NO`, AppContainer
and NX, with base relocations present. `/FIXED` prevents UWP activation on
the tested system. Its loader places the image at another low address
despite DYNAMICBASE being disabled. Game units use `-mcmodel=large`, and
`relocation-audit.json` rejects direct REL32 references to absolute guest
pins before archiving. Without this, a tested rebase from `0x40000000` to
`0x3F860000` changed an access to guest `0x8009B098` into `0x7F8FB098` and
crashed immediately. The entire image and its 128 MiB mod arena must fit
between `0x20000000` and `0x80000000`; the entry checks this. The jump-buffer
and stat ABI bindings are checked in native code.

`Memories_UWP_SetEnvironment` takes UTF-8, including paths. Environment reads
use the CRT's UTF-16 environment and convert back to UTF-8; ordinary narrow
`getenv` loses characters in user-folder names. Set the disc, program folder
and writable user folder before starting the core once on its worker
thread. Runtime settings/mod changes go through the queued bridge requests.

Guest RAM mirrors use `VirtualAlloc2FromApp` placeholders followed by
`MapViewOfFile3FromApp(MEM_REPLACE_PLACEHOLDER)`. Mapping the exact address
directly with FromApp failed with Windows error 87. File reads first use the
core's own CRT. External-disc fallback adopts the host's owned HANDLE with
`_open_osfhandle` and `_fdopen`; a host `FILE*` never crosses the CRT boundary.

To validate the MSVC host ABI without a window, build `NativeConsole.vcxproj`
using MSBuild Release/x64, then run `validate_intro.py --executable <exe>
--assets tmp/uwp-native --disc <owned-bin>`. `--mods` uses bundled defaults;
`--host-callbacks` checks `HEADLESS=0` and `NO_AUDIO=0` through the host
callbacks and writes raw signed 16-bit stereo PCM, without opening a window/audio device.
Each validation uses its own settings and user folder; player saves are
never loaded or changed. The driver confirms boot, nickname, campaign
dialogue and the first duel with a full hand using the frame control channel.
