#ifndef MEMORIES_UWP_BRIDGE_H
#define MEMORIES_UWP_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native UWP host ABI. Call the entry once on the dedicated game thread.
 * Set MEMORIES_DISC, MEMORIES_PROGRAM_DIR and MEMORIES_USER_DIR first. */
int Memories_UWP_Main(int argc, char **argv);
int Memories_UWP_SetEnvironment(const char *name, const char *value);
/* UI-thread requests are applied at the next game frame, never while the
 * renderer, audio mixer, or guest callbacks are midway through an update. */
int Memories_UWP_RequestSetting(const char *key, int value);
int Memories_UWP_RequestMod(const char *id, int enabled);
void Memories_UWP_ServiceRequests(void);
/* Read-only external-disc fallback. Success transfers HANDLE ownership to
 * the core, which creates its descriptor and FILE in its own C runtime. */
intptr_t UWP_OpenReadHandle(const char *pathUtf8);
void *Memories_UwpImageBase(void);
void UWP_OnPresent(const uint32_t *rgba, int width, int height, int stridePixels);
/* PS1 wire bit order, ACTIVE HIGH: Select=1, Start=8, Up=0x10,
 * Right=0x20, Down=0x40, Left=0x80, Cross=0x4000, Circle=0x2000. */
uint16_t UWP_OnGetPadBits(int port);
int UWP_IsWidescreen(void);
int UWP_GetSaveSlot(void);
int UWP_ShouldQuit(void);
int UWP_StartAudio(void (*mix)(int16_t *frames, size_t count));
void UWP_LogMessage(const char *message);
#ifdef __cplusplus
}
#endif
#endif
