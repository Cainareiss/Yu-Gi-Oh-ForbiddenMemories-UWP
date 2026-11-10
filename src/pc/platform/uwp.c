#include "pc/compat/fs.h"
#include "platform.h"
#include "uwp_bridge.h"
#include "settings.h"
#include "menu.h"
#include "controls_runtime.h"
#include "pc/render/soft_gpu.h"
#include "pc/audio/spu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t *rgba;
static size_t rgba_capacity;
static volatile uint16_t pads[2], scripted[2];
static volatile int quit;
static int slot = -1;
static int display_scale = 4;
static int env_on(const char *name)
{
    const char *value = getenv(name);
    return value && *value && strcmp(value, "0") != 0;
}
static int reserve_pixels(int w, int h)
{
    size_t count;
    uint32_t *next;
    if (w <= 0 || h <= 0 || w > 8192 || h > 4096) return 0;
    count = (size_t)w * (size_t)h;
    if (count <= rgba_capacity) return 1;
    next = realloc(rgba, count * sizeof(*next));
    if (!next) return 0;
    rgba = next; rgba_capacity = count; return 1;
}
static uint32_t rgba_from_rgb(uint32_t rgb)
{
    return 0xff000000u | ((rgb & 0xffu) << 16) | (rgb & 0xff00u) | ((rgb >> 16) & 0xffu);
}
static uint32_t rgba_from_psx(uint16_t p)
{
    unsigned r=p&31u, g=(p>>5)&31u, b=(p>>10)&31u;
    return 0xff000000u | ((b*255u/31u)<<16) | ((g*255u/31u)<<8) | (r*255u/31u);
}
int Platform_Open(const char *title)
{
    (void)title;
    ControlsRuntime_Init();
    Menu_LoadSettings();
    Menu_Init();
    Platform_SetPresentRefresh(60.0f);
    return 0;
}
void Platform_ShowError(const char *title, const char *message)
{
    char text[2048]; snprintf(text,sizeof(text),"%s: %s",title,message); UWP_LogMessage(text);
}
int Platform_SelectDisc(char *path,size_t size,char *why,size_t why_size)
{
    const char *disc=getenv("MEMORIES_DISC");
    if (!disc || !*disc || strlen(disc)>=size) {
        snprintf(why,why_size,"Select a disc in the UWP launcher."); return -1;
    }
    memcpy(path,disc,strlen(disc)+1); return 1;
}
void Platform_Present(const uint16_t *vram,int stride,int x,int y,int w,int h,int rgb24)
{
    int col,row;
    if (!reserve_pixels(w,h)) return;
    for(row=0;row<h;row++) for(col=0;col<w;col++) {
        if(rgb24) {
            const unsigned char *p=(const unsigned char *)(vram+(y+row)*stride+x)+col*3;
            rgba[(size_t)row*w+col]=0xff000000u|((uint32_t)p[2]<<16)|((uint32_t)p[1]<<8)|p[0];
        } else rgba[(size_t)row*w+col]=rgba_from_psx(vram[(y+row)*stride+x+col]);
    }
    if(!env_on("MEMORIES_HEADLESS")) UWP_OnPresent(rgba,w,h,w);
}
int Platform_PresentPicture(const uint32_t *pixels,int stride,int x,int y,int w,int h,int scale)
{
    int row,col; (void)scale;
    if(!pixels || !reserve_pixels(w,h)) return 0;
    for(row=0;row<h;row++) for(col=0;col<w;col++)
        rgba[(size_t)row*w+col]=rgba_from_rgb(pixels[(size_t)(y+row)*stride+x+col]);
    if(!env_on("MEMORIES_HEADLESS")) UWP_OnPresent(rgba,w,h,w);
    return 1;
}
int Platform_ReadPicture(uint32_t *out,int x,int y,int w,int h)
{ (void)out;(void)x;(void)y;(void)w;(void)h; return 0; }
int Platform_PresentWidePicture(int x,int y,int w,int h,int wide_w,int scale)
{ (void)x;(void)y;(void)w;(void)h;(void)wide_w;(void)scale; return 0; }
int Platform_ReadWidePicture(uint32_t *out,int x,int y,int w,int h,int wide_w,int scale)
{ (void)out;(void)x;(void)y;(void)w;(void)h;(void)wide_w;(void)scale; return 0; }
int Platform_Widescreen(void) { return Settings_Get(SET_ASPECT)==2 || UWP_IsWidescreen(); }
int Platform_HasDesktopGL(void) { return 0; }
int Platform_GuestMemoryHelp(char *why,size_t size)
{ snprintf(why,size,"Cannot reserve the native core's fixed guest memory. Restart the app."); return 1; }
void Platform_Frame(unsigned frame)
{
    Memories_UWP_ServiceRequests();
    pads[0]=UWP_OnGetPadBits(0); pads[1]=UWP_OnGetPadBits(1);
    scripted[0]=Platform_ScriptedBits(frame); scripted[1]=Platform_ScriptedBits2(frame);
}
uint16_t Platform_Pad(int port) { return port>=0&&port<2 ? pads[port]|scripted[port] : 0; }
uint16_t Platform_PadFixedBits(int port) { return port>=0&&port<2 ? scripted[port] : 0; }
int Platform_PadConnected(int port) { return port==0 || (port==1&&(pads[1]||Platform_ScriptedPad2())); }
void Gamepad_Poll(unsigned frame) { (void)frame; }
uint16_t Gamepad_Bits(int port) { return port>=0&&port<2 ? pads[port] : 0; }
int Gamepad_Connected(int port) { return Platform_PadConnected(port); }
int Platform_ShouldQuit(void) { return quit||UWP_ShouldQuit(); }
void Platform_RequestQuit(void) { quit=1; }
int Platform_StateSlot(void) { return slot<0 ? UWP_GetSaveSlot() : slot; }
void Platform_SetStateSlot(int value) { slot=value; }
void Platform_PumpEvents(void) {}
void Platform_ApplyDisplaySettings(void) {}
int Platform_HasWindowModes(void) { return 0; }
int Platform_Scale(void) { return display_scale; }
void Platform_SetScale(int value) { display_scale=value; }
void Platform_ToggleFullscreen(void) {}
void Platform_Screenshot(int window_image) { (void)window_image; }
int Platform_CopyText(const char *text) { (void)text; return -1; }
void Platform_OpenMods(void) { UWP_LogMessage("Open Mods from the UWP launcher."); }
void Platform_OpenControls(void) {}
int Platform_OpenFolder(const char *path) { (void)path; return -1; }
int Platform_OpenUrl(const char *url) { (void)url; return -1; }
int Platform_RestartGame(void) { return -1; }
int Platform_PickModZip(char *why,size_t size) { snprintf(why,size,"Import mods from the UWP launcher."); return -1; }
int Platform_PickedModZip(char *why,size_t size) { (void)why;(void)size; return -1; }
int Platform_FetchModZip(char *path,size_t size,char *why,size_t why_size)
{ (void)path;(void)size;snprintf(why,why_size,"Import mods from the UWP launcher.");return 0; }
const struct HdNet *Platform_HdNet(void) { return NULL; }
int Platform_StartAudio(void (*mix)(int16_t *,size_t))
{
    if(env_on("MEMORIES_NO_AUDIO") || env_on("MEMORIES_HEADLESS"))
        return Platform_StartSilentAudio(mix,getenv("MEMORIES_DUMP_AUDIO"));
    return UWP_StartAudio(mix);
}
void Platform_AudioStats(int *queued,unsigned *underruns)
{ if(queued)*queued=0;if(underruns)*underruns=0; }
