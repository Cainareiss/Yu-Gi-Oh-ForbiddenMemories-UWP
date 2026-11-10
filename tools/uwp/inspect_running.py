"""Observe the installed game with ReadProcessMemory; never send input or write RAM."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import re
import struct
import time

ROOT = Path(__file__).resolve().parents[2]
EXE = 'YuGiOhForbiddenMemoriesUWP.exe'
kernel = c.WinDLL('kernel32', use_last_error=True)


class ProcessEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('pid', w.DWORD),
                ('heap', c.c_size_t), ('module', w.DWORD), ('threads', w.DWORD),
                ('parent', w.DWORD), ('priority', w.LONG), ('flags', w.DWORD),
                ('name', w.WCHAR * 260)]


class ModuleEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('id', w.DWORD), ('pid', w.DWORD),
                ('global_usage', w.DWORD), ('process_usage', w.DWORD),
                ('base', c.c_void_p), ('length', w.DWORD), ('module', w.HMODULE),
                ('name', w.WCHAR * 256), ('path', w.WCHAR * 260)]


kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel.OpenProcess.restype = w.HANDLE
kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
kernel.CloseHandle.argtypes = [w.HANDLE]


def entries(flags, pid, kind, first, following):
    snapshot = kernel.CreateToolhelp32Snapshot(flags, pid)
    if snapshot == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    try:
        entry = kind()
        entry.size = c.sizeof(entry)
        begin, advance = getattr(kernel, first), getattr(kernel, following)
        begin.argtypes = advance.argtypes = [w.HANDLE, c.POINTER(kind)]
        ok = begin(snapshot, c.byref(entry))
        while ok:
            yield kind.from_buffer_copy(entry)
            ok = advance(snapshot, c.byref(entry))
    finally:
        kernel.CloseHandle(snapshot)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int)
    parser.add_argument('--samples', type=int, default=2)
    parser.add_argument('--interval', type=float, default=1.0)
    parser.add_argument('--map', type=Path, default=ROOT / 'uwp' / EXE[:-4] / 'x64/Release' / EXE[:-4] / (EXE[:-4] + '.map'))
    args = parser.parse_args()
    pids = [args.pid] if args.pid else [p.pid for p in entries(2, 0, ProcessEntry, 'Process32FirstW', 'Process32NextW') if p.name.lower() == EXE.lower()]
    if len(pids) != 1:
        print(json.dumps({'running_processes': pids, 'error': 'Expected one installed game process'}))
        return
    pid = pids[0]
    module = next(m for m in entries(0x18, pid, ModuleEntry, 'Module32FirstW', 'Module32NextW') if m.name.lower() == EXE.lower())
    process = kernel.OpenProcess(0x10 | 0x1000, False, pid)
    if not process:
        raise c.WinError(c.get_last_error())
    def read(address, size):
        result, count = c.create_string_buffer(size), c.c_size_t()
        if not kernel.ReadProcessMemory(process, address, result, size, c.byref(count)) or count.value != size:
            raise c.WinError(c.get_last_error())
        return result.raw
    try:
        monitor = None
        observed = {}
        if args.map.is_file():
            text = args.map.read_text(encoding='utf-8', errors='replace')
            address = re.search(r'\bMonitor_Shared\s+([0-9a-fA-F]{16})\b', text)
            base = re.search(r'Preferred load address is\s+([0-9a-fA-F]+)', text)
            if address and base:
                link_base = int(base[1], 16)
                for key, needle, length in (('controller_bits', '?s_padBits@@', 4), ('keyboard_bits', '?s_keyboardBits@@', 2), ('keyboard_pulse', '?s_keyboardPulse@@', 2), ('scripted_bits', 'scripted', 4)):
                    for line in text.splitlines():
                        fields = line.split()
                        if len(fields) > 2 and (fields[1].startswith(needle) if needle.startswith('?') else fields[1] == needle):
                            observed[key] = (module.base + int(fields[2], 16) - link_base, length)
                            break
                getter = module.base + int(address[1], 16) - int(base[1], 16)
                code = read(getter, 32)
                for opcode in (b'\x48\x8d\x05', b'\x48\x8b\x05'):
                    at = code.find(opcode)
                    if at >= 0:
                        target = getter + at + 7 + struct.unpack_from('<i', code, at + 3)[0]
                        monitor = target if opcode[1] == 0x8d else struct.unpack('<Q', read(target, 8))[0]
                        break
        for sample in range(max(1, args.samples)):
            state = {'pid': pid, 'image_base': f'0x{module.base:08X}', 'image_size': module.length, 'executable': module.path, 'sample': sample}
            for key, (location, length) in observed.items():
                state[key] = read(location, length).hex()
            if monitor:
                fields = struct.unpack('<13I', read(monitor, 52))
                state.update(dict(zip(('magic', 'version', 'generation', 'heartbeat', 'running', 'paused', 'modal', 'exiting', 'restart', 'error_shown', 'reported', 'frame', 'vblank'), fields)))
            try:
                state['guest_mode'] = read(0x8009B26C, 1)[0] & 31
                state['nickname_hex'] = read(0x801D060C, 24).split(b'\x00')[0].hex()
                state['pad_held'] = f'0x{struct.unpack("<H", read(0x8009B3A4, 2))[0]:04X}'
                state['pad_pressed'] = f'0x{struct.unpack("<H", read(0x8009B398, 2))[0]:04X}'
                state['raw_pad'] = read(0x800EF668, 4).hex()
            except OSError as error:
                state['guest_unavailable'] = str(error)
            print(json.dumps(state, ensure_ascii=True), flush=True)
            if sample + 1 < args.samples:
                time.sleep(max(0.0, min(args.interval, 60.0)))
    finally:
        kernel.CloseHandle(process)


if __name__ == '__main__':
    main()
