#include "pc/compat/fs.h"
#include "uwp_bridge.h"
#include "platform.h"
#include "settings.h"
#include "paths.h"
#include "game_files.h"
#include "pc/guest/image.h"
#include "pc/guest/state.h"
#include "pc/mods/mods.h"
#include "pc/debug/log.h"
#include "pc/debug/crash.h"
#include "pc/debug/symbols.h"
#include "pc/text/entry_layout.h"
#include "pc/text/text.h"
#include "pc/cards/cards.h"
#include "uwp_winapi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int Main_Init(void);
extern int Memories_UwpCheckJumpAbi(void);
void Psx___main(void) {}
static int start_game(void) { TextEntries_Start(); return Main_Init(); }
typedef struct { char key[128]; int value, mod; } UwpRequest;
static SRWLOCK requests_lock = SRWLOCK_INIT;
static UwpRequest requests[64];
static unsigned request_count;
static int request(const char *key, int value, int mod)
{
    unsigned i;
    if (!key || !*key || strlen(key) >= sizeof(requests[0].key)) return -1;
    AcquireSRWLockExclusive(&requests_lock);
    for (i=0;i<request_count;i++) if (requests[i].mod==mod && !strcmp(requests[i].key,key)) break;
    if (i==sizeof(requests)/sizeof(requests[0])) {
        ReleaseSRWLockExclusive(&requests_lock); return -1;
    }
    strcpy(requests[i].key,key); requests[i].value=value; requests[i].mod=mod;
    if (i==request_count) request_count++;
    ReleaseSRWLockExclusive(&requests_lock); return 0;
}
int Memories_UWP_RequestSetting(const char *key,int value) {return request(key,value,0);}
int Memories_UWP_RequestMod(const char *id,int enabled) {return request(id,!!enabled,1);}
void Memories_UWP_ServiceRequests(void)
{
    UwpRequest pending[64]; unsigned count,i; int j;
    AcquireSRWLockExclusive(&requests_lock);
    count=request_count; memcpy(pending,requests,count*sizeof(*pending)); request_count=0;
    ReleaseSRWLockExclusive(&requests_lock);
    for(i=0;i<count;i++) {
        if (pending[i].mod) {
            for(j=0;j<Mods_Count();j++) if(!strcmp(Mods_Id(j),pending[i].key)) {
                Mods_SetEnabled(j,pending[i].value); break;
            }
        } else for(j=0;j<SET_COUNT;j++) {
            const char *key=Settings_Key((SettingId)j);
            if(key && !strcmp(key,pending[i].key)) {Settings_Set((SettingId)j,pending[i].value);break;}
        }
    }
}
int Memories_UWP_SetEnvironment(const char *name, const char *value)
{
    return value ? setenv(name, value, 1) : unsetenv(name);
}
int Memories_UWP_Main(int argc, char **argv)
{
    char why[1024];
    const char *disc;
    unsigned char *data;
    size_t size = 0;
    int result;
    (void)argc; (void)argv;
    /* UWP may rebase a low-address image even without DYNAMICBASE. The
     * large-model core keeps fixed PS1 references independent of its base.
     * Keep the whole image and its 128 MiB mod arena below guest RAM, and
     * above the retail scratchpad/low-memory views. Host callbacks must fit
     * four-byte guest slots; accepting a high ASLR address would truncate. */
    {
        uintptr_t base=(uintptr_t)Memories_UwpImageBase();
        const IMAGE_DOS_HEADER *dos=(const IMAGE_DOS_HEADER *)base;
        const IMAGE_NT_HEADERS *nt=(const IMAGE_NT_HEADERS *)(base+dos->e_lfanew);
        uintptr_t end=base+nt->OptionalHeader.SizeOfImage;
        uintptr_t mod_start=(end+0xffffu)&~(uintptr_t)0xffffu;
        if (base<0x20000000u || end<base || mod_start>0x78000000u) {
            UWP_LogMessage("Fatal: native image and mod arena do not fit the low-address guest layout.");
            return 70;
        }
    }
    if (!Memories_UwpCheckJumpAbi()) {
        UWP_LogMessage("Fatal: native C runtime jump-buffer ABI check failed."); return 77;
    }
    Log_Init();
    Crash_Init();
    Symbols_Load();
    Mods_SetFunctionNames(Symbols_Lookup);
    if (!getenv("MEMORIES_DISC") || !getenv("MEMORIES_USER_DIR")) {
        UWP_LogMessage("Fatal: MEMORIES_DISC and MEMORIES_USER_DIR must be configured by the UWP host.");
        return 71;
    }
    if (Memories_GuestMap()) return 72;
    result = GameFiles_Setup(why, sizeof(why));
    disc = result > 0 ? GameFiles_Disc(why, sizeof(why)) : NULL;
    if (!disc) { UWP_LogMessage(why); return 73; }
    data = GameFiles_ReadExecutable(disc, &size);
    if (!data) { UWP_LogMessage("Could not read SLUS_014.11 from the selected disc image."); return 74; }
    result = Memories_GuestLoadExeData(data, size, disc);
    free(data);
    if (result || Memories_ModulesInit()) return 75;
    if (Platform_Open("Yu-Gi-Oh! Forbidden Memories")) return 76;
    Text_Build(); Cards_Build(); Text_SortCards();
    UWP_LogMessage("Native core v0.3.0: G32 guest layout initialized; starting the game.");
    return Memories_StateRunGame(start_game);
}
