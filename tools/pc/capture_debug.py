import ctypes
import sys
import time

PAGE_READWRITE = 0x04
FILE_MAP_READ = 0x0004
WAIT_OBJECT_0 = 0x00000000
WAIT_TIMEOUT = 0x00000102
SYNCHRONIZE = 0x00100000

kernel32 = ctypes.windll.kernel32

def main():
    buffer_ready = kernel32.CreateEventW(None, False, True, "DBWIN_BUFFER_READY")
    if not buffer_ready:
        print("Failed to create DBWIN_BUFFER_READY", file=sys.stderr)
        return
    data_ready = kernel32.CreateEventW(None, False, False, "DBWIN_DATA_READY")
    if not data_ready:
        print("Failed to create DBWIN_DATA_READY", file=sys.stderr)
        return

    buffer_size = 4096
    file_map = kernel32.CreateFileMappingW(
        ctypes.c_void_p(-1), None, PAGE_READWRITE, 0, buffer_size, "DBWIN_BUFFER"
    )
    if not file_map:
        print("Failed to create DBWIN_BUFFER file mapping", file=sys.stderr)
        return

    shared_mem = kernel32.MapViewOfFile(file_map, FILE_MAP_READ, 0, 0, buffer_size)
    if not shared_mem:
        print("Failed to map view of file", file=sys.stderr)
        return

    print("Debug output listener started. Waiting for messages...", flush=True)

    start_time = time.time()
    try:
        while True:
            kernel32.SetEvent(buffer_ready)
            ret = kernel32.WaitForSingleObject(data_ready, 1000)
            if ret == WAIT_OBJECT_0:
                pid = ctypes.c_ulong.from_address(shared_mem).value
                msg_bytes = ctypes.string_at(shared_mem + 4, 4092)
                # Null-terminated string
                null_idx = msg_bytes.find(b'\x00')
                if null_idx != -1:
                    msg_bytes = msg_bytes[:null_idx]
                try:
                    msg = msg_bytes.decode('utf-8', errors='replace')
                except Exception:
                    msg = repr(msg_bytes)
                if any(tag in msg for tag in ["[UWP", "[BRIDGE]", "[CRASH]", "[START", "[MAIN", "[GAME", "YuGiOh"]):
                    print(f"[{pid}] {msg}", flush=True)
                else:
                    # Also print if it contains exception or fatal
                    if "exception" in msg.lower() or "error" in msg.lower() or "fault" in msg.lower():
                        print(f"[{pid}?] {msg}", flush=True)
            elif ret == WAIT_TIMEOUT:
                # check if timeout exceeded for standalone run
                if len(sys.argv) > 1 and sys.argv[1] == "--timeout":
                    timeout_val = float(sys.argv[2])
                    if time.time() - start_time > timeout_val:
                        break
    except KeyboardInterrupt:
        pass
    finally:
        kernel32.UnmapViewOfFile(shared_mem)
        kernel32.CloseHandle(file_map)
        kernel32.CloseHandle(data_ready)
        kernel32.CloseHandle(buffer_ready)

if __name__ == "__main__":
    main()
