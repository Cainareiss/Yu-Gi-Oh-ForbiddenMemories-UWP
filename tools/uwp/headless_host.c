/* Console validation harness only. The packaged app uses the C++/CX host. */
#include "pc/platform/uwp_bridge.h"
#include <stdio.h>
#include <math.h>
/* MSVC supplies fabsf as a header inline on x64, rather than a UCRT export.
 * The graphical host also takes its address via its UI math code. */
float (*volatile Headless_Fabsf)(float) = fabsf;
extern int Platform_StartSilentAudio(void (*mix)(int16_t *,size_t),const char *path);
extern char *Memories_Getenv(const char *name);
intptr_t UWP_OpenReadHandle(const char *path) {(void)path;return -1;}
void UWP_OnPresent(const uint32_t *p,int w,int h,int s)
{static int first=1;(void)p;if(first){fprintf(stderr,"UWP host present callback: %dx%d stride=%d\n",w,h,s);first=0;}}
uint16_t UWP_OnGetPadBits(int p) {(void)p;return 0;}
int UWP_IsWidescreen(void) {return 0;}
int UWP_GetSaveSlot(void) {return 0;}
int UWP_ShouldQuit(void) {return 0;}
int UWP_StartAudio(void(*mix)(int16_t*,size_t))
{fprintf(stderr,"UWP host audio callback reached\n");return Platform_StartSilentAudio(mix,Memories_Getenv("MEMORIES_DUMP_AUDIO"));}
void UWP_LogMessage(const char *s) {fprintf(stderr,"UWP: %s\n",s);}
int main(void) {return Memories_UWP_Main(0,NULL);}
