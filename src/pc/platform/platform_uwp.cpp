#define _GNU_SOURCE
#include "platform.h"
#include "platform_uwp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// Decoupled C hooks provided by the UWP frontend (GameEngineBridge.cpp)
extern "C" {
    void UWP_OnPresent(const uint32_t *pixels, int width, int height, int stride);
    uint16_t UWP_OnGetPadBits(int port);
    int UWP_IsWidescreen(void);
    int UWP_GetSaveSlot(void);
    int UWP_ShouldQuit(void);
    int UWP_StartAudio(void (*mix)(int16_t *frames, size_t count));
}

static std::vector<uint32_t> s_conversionBuffer;

extern "C" {

void Platform_UWP_Init(void)
{
    s_conversionBuffer.resize(1024 * 512);
}

int Platform_Open(const char *title)
{
    (void)title;
    Platform_UWP_Init();
    return 0;
}

void Platform_ShowError(const char *title, const char *message)
{
    (void)title;
    if (message)
    {
        fprintf(stderr, "UWP Error [%s]: %s\n", title ? title : "", message);
#ifdef _WIN32
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
#endif
    }
}

int Platform_SelectDisc(char *path, size_t size, char *why, size_t why_size)
{
    const char *disc = getenv("MEMORIES_DISC");
    if (disc && disc[0])
    {
        strncpy(path, disc, size - 1);
        path[size - 1] = '\0';
        return 1;
    }
    if (why && why_size)
    {
        snprintf(why, why_size, "MEMORIES_DISC is not set");
    }
    return 0;
}

void Platform_OpenMods(void) {}
void Platform_OpenControls(void) {}
int Platform_OpenFolder(const char *path) { (void)path; return 0; }
int Platform_OpenUrl(const char *url) { (void)url; return 0; }
int Platform_RestartGame(void) { return -1; }

void Platform_Present(const uint16_t *vram, int stride, int x, int y, int w, int h, int rgb24)
{
    if (!vram || w <= 0 || h <= 0) return;

    if (s_conversionBuffer.size() < (size_t)(w * h))
    {
        s_conversionBuffer.resize(w * h);
    }

    uint32_t *dst = s_conversionBuffer.data();

    if (rgb24)
    {
        // 24-bit RGB packed
        const uint8_t *src_bytes = (const uint8_t *)vram;
        for (int row = 0; row < h; row++)
        {
            const uint8_t *src_row = src_bytes + (y + row) * stride * 2 + x * 3;
            for (int col = 0; col < w; col++)
            {
                uint8_t r = src_row[col * 3 + 0];
                uint8_t g = src_row[col * 3 + 1];
                uint8_t b = src_row[col * 3 + 2];
                dst[row * w + col] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    }
    else
    {
        // 15-bit RGB555 (with bit 15 mask)
        for (int row = 0; row < h; row++)
        {
            const uint16_t *src_row = vram + (y + row) * stride + x;
            for (int col = 0; col < w; col++)
            {
                uint16_t pix = src_row[col];
                uint32_t r = ((pix & 0x001F)) * 255 / 31;
                uint32_t g = ((pix & 0x03E0) >> 5) * 255 / 31;
                uint32_t b = ((pix & 0x7C00) >> 10) * 255 / 31;
                dst[row * w + col] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
    }

    UWP_OnPresent(dst, w, h, w);
}

int Platform_Widescreen(void)
{
    return UWP_IsWidescreen();
}

int Platform_PresentPicture(const uint32_t *pixels, int stride, int x, int y, int w, int h, int scale)
{
    (void)scale;
    if (!pixels || w <= 0 || h <= 0) return 0;
    const uint32_t *src = pixels + y * stride + x;
    UWP_OnPresent(src, w, h, stride);
    return 1;
}

int Platform_ReadPicture(uint32_t *out, int x, int y, int w, int h)
{
    (void)out; (void)x; (void)y; (void)w; (void)h;
    return 0;
}

int Platform_PresentWidePicture(int x, int y, int w, int h, int wide_w, int scale)
{
    (void)x; (void)y; (void)w; (void)h; (void)wide_w; (void)scale;
    return 0;
}

int Platform_ReadWidePicture(uint32_t *out, int x, int y, int w, int h, int wide_w, int scale)
{
    (void)out; (void)x; (void)y; (void)w; (void)h; (void)wide_w; (void)scale;
    return 0;
}

int Platform_ShouldQuit(void)
{
    return UWP_ShouldQuit();
}

int Platform_StateSlot(void)
{
    return UWP_GetSaveSlot();
}

void Platform_SetStateSlot(int slot)
{
    (void)slot;
}

void Platform_ApplyDisplaySettings(void) {}
int Platform_HasWindowModes(void) { return 0; }
void Platform_Screenshot(int window_image) { (void)window_image; }

uint16_t Platform_Pad(int port)
{
    return UWP_OnGetPadBits(port);
}

int Platform_PadConnected(int port)
{
    return port == 0 ? 1 : 0;
}

uint16_t Platform_PadFixedBits(int port)
{
    (void)port;
    return 0;
}

void Gamepad_Poll(unsigned frame) { (void)frame; }
uint16_t Gamepad_Bits(int port) { return Platform_Pad(port); }
int Gamepad_Connected(int port) { return Platform_PadConnected(port); }

void Platform_PumpEvents(void) {}

int Platform_StartAudio(void (*mix)(int16_t *frames, size_t count))
{
    return UWP_StartAudio(mix);
}

void Platform_AudioStats(int *queued_frames, unsigned *underruns)
{
    if (queued_frames) *queued_frames = 0;
    if (underruns) *underruns = 0;
}

int Platform_VolumeKey(int key, int down)
{
    (void)key; (void)down;
    return 0;
}

} // extern "C"
