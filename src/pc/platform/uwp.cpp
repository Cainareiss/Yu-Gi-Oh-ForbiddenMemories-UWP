#define _GNU_SOURCE
#include "platform.h"
#include "paths.h"
#include "settings.h"
#include "uwp/YuGiOhForbiddenMemoriesUWP/GameEngineBridge.h"
#include "uwp/YuGiOhForbiddenMemoriesUWP/GamepadUwp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

static int s_stateSlot = 1;
static int s_shouldQuit = 0;

int Platform_Open(const char *title)
{
    (void)title;
    return 0;
}

void Platform_ShowError(const char *title, const char *message)
{
    fprintf(stderr, "[UWP Error] %s: %s\n", title ? title : "Error", message ? message : "");
}

int Platform_SelectDisc(char *path, size_t size, char *why, size_t why_size)
{
    (void)path; (void)size; (void)why; (void)why_size;
    return 0;
}

void Platform_OpenMods(void) {}
void Platform_OpenControls(void) {}
int Platform_OpenFolder(const char *path) { (void)path; return 0; }
int Platform_OpenUrl(const char *url) { (void)url; return 0; }

void Platform_Present(const uint16_t *vram, int stride, int x, int y, int w, int h, int rgb24)
{
    (void)vram; (void)stride; (void)x; (void)y; (void)w; (void)h; (void)rgb24;
}

int Platform_Widescreen(void)
{
    return GameEngineBridge::Instance().GetSettings().widescreen ? 1 : 0;
}

int Platform_PresentPicture(const uint32_t *pixels, int stride, int x, int y, int w, int h, int scale)
{
    (void)x; (void)y; (void)scale;
    if (pixels && w > 0 && h > 0)
    {
        GameEngineBridge::Instance().OnPresent(pixels, w, h, stride);
        return 1;
    }
    return 0;
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
    return s_shouldQuit;
}

int Platform_StateSlot(void)
{
    return s_stateSlot;
}

void Platform_SetStateSlot(int slot)
{
    s_stateSlot = slot;
}

void Platform_ApplyDisplaySettings(void) {}
int Platform_HasWindowModes(void) { return 1; }
void Platform_Screenshot(int window_image) { (void)window_image; }

uint16_t Platform_Pad(int port)
{
    return GameEngineBridge::Instance().OnGetPadBits(port);
}

int Platform_PadConnected(int port)
{
    return GamepadUwp::IsConnected(port) ? 1 : 0;
}

uint16_t Platform_PadFixedBits(int port)
{
    (void)port;
    return 0;
}

void Gamepad_Poll(unsigned frame)
{
    (void)frame;
    GamepadUwp::Poll();
}

uint16_t Gamepad_Bits(int port)
{
    return GamepadUwp::GetPadBits(port);
}

int Gamepad_Connected(int port)
{
    return GamepadUwp::IsConnected(port) ? 1 : 0;
}

void Platform_Frame(unsigned frame) { (void)frame; }
void Platform_PumpEvents(void)
{
    GamepadUwp::Poll();
}

int Platform_StartAudio(void (*mix)(int16_t *frames, size_t count))
{
    return GameEngineBridge::Instance().OnStartAudio(mix) ? 0 : -1;
}

int Platform_StartSilentAudio(void (*mix)(int16_t *frames, size_t count), const char *dump_path)
{
    (void)mix; (void)dump_path;
    return 0;
}

void Platform_AudioStats(int *queued_frames, unsigned *underruns)
{
    if (queued_frames) *queued_frames = 1024;
    if (underruns) *underruns = 0;
}
