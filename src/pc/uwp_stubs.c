#include <stdint.h>
#include <stddef.h>
#include <string.h>

// HD Text stubs (FreeType not used on UWP core)
int HdText_Enabled(void) { return 0; }
void HdText_TitleUploaded(int card, int x, int y)
{
    (void)card; (void)x; (void)y;
}
int HdText_NameEnabled(void) { return 0; }
const uint32_t *HdText_NamePixels(uint32_t word, int *width, int *height)
{
    (void)word;
    if (width) *width = 0;
    if (height) *height = 0;
    return NULL;
}

// Menu bar stubs (UWP uses XAML Fluent UI)
int Menu_Scale(void) { return 1; }
int Menu_Height(void) { return 0; }
void Menu_SetItemEnabled(int item, int enabled) { (void)item; (void)enabled; }
int Menu_TextWidthScaled(const char *text, int scale) { (void)text; (void)scale; return 0; }
void Menu_DrawTextScaled(int x, int y, const char *text, int scale) { (void)x; (void)y; (void)text; (void)scale; }

// Overlay text stubs
int OverlayText_Blend(void) { return 0; }
void OverlayText_Draw(int x, int y, const char *text) { (void)x; (void)y; (void)text; }
int OverlayText_Width(const char *text) { (void)text; return 0; }

// Texture pack stubs (libpng not used on UWP core)
void TexturePack_AddMade(void) {}
void TexturePack_Service(void) {}

// Notice & monitor stubs
void Menu_ShowNotice(const char *text) { (void)text; }
int Monitor_Active(void) { return 0; }

const uint32_t *HdText_PlayerName(const char *name, int *width, int *height)
{
    (void)name;
    if (width) *width = 0;
    if (height) *height = 0;
    return NULL;
}

unsigned Memories_ModExportCount = 0;
const void *Memories_ModExports = NULL;

void bzero(void *s, size_t n)
{
    memset(s, 0, n);
}

int Psx_open(const char *name, int flags, ...) { (void)name; (void)flags; return -1; }
int Psx_close(int fd) { (void)fd; return 0; }
int Psx_read(int fd, void *buf, int count) { (void)fd; (void)buf; (void)count; return 0; }
int Psx_write(int fd, const void *buf, int count) { (void)fd; (void)buf; (void)count; return 0; }
int Psx_lseek(int fd, int offset, int whence) { (void)fd; (void)offset; (void)whence; return -1; }
void Psx_exit(int code) { (void)code; }

extern int _setjmp(void *, void *);
int _uwp_setjmp(void *env)
{
    return _setjmp(env, NULL);
}
void *__imp__setjmp = (void *)_uwp_setjmp;

struct _stat64i32;
extern int _fstat64i32(int, struct _stat64i32 *);
extern int _wstat64i32(const wchar_t *, struct _stat64i32 *);

int fstat64i32(int fd, struct _stat64i32 *buf)
{
    return _fstat64i32(fd, buf);
}

int wstat64i32(const wchar_t *path, struct _stat64i32 *buf)
{
    return _wstat64i32(path, buf);
}


#ifdef _WIN32
#include <windows.h>

struct timespec64 {
    int64_t tv_sec;
    long tv_nsec;
};

int clock_gettime64(int clock_id, struct timespec64 *tp)
{
    (void)clock_id;
    if (!tp) return -1;
    static LARGE_INTEGER freq;
    static int initialized = 0;
    if (!initialized)
    {
        QueryPerformanceFrequency(&freq);
        initialized = 1;
    }
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    tp->tv_sec = count.QuadPart / freq.QuadPart;
    tp->tv_nsec = (long)(((count.QuadPart % freq.QuadPart) * 1000000000ULL) / freq.QuadPart);
    return 0;
}

int nanosleep64(const struct timespec64 *req, struct timespec64 *rem)
{
    (void)rem;
    if (!req) return -1;
    DWORD ms = (DWORD)(req->tv_sec * 1000 + (req->tv_nsec + 999999) / 1000000);
    if (ms == 0 && req->tv_nsec > 0) ms = 1;
    Sleep(ms);
    return 0;
}
#endif

void CrashTest_Frame(void) {}
void CrashTest_TickHang(void) {}
