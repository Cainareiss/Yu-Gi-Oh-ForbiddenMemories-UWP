#define GDUEL_BEFFECTSTATE_IN_DATA
#define MAIN_MODE_STATE_NEXT_AS_SCALAR
#define MAIN_MODE_STATE_ACTIVE_AS_SCALAR
#include "../types.h"
#include "scene_script.h"
#include "duel_effect.h"
#include "fade.h"
#include "script_run_tick.h"
#include "sound.h"
#include "campaign_scene_package.h"
#include "../unmatched.h"
#include "main_mode_state.h"

#ifdef MEMORIES_UWP
#include <stdio.h>
extern void UWP_LogMessage(const char *msg);
#else
#define UWP_LogMessage(msg) ((void)0)
#endif

void Main_RunCampaign(void) {
    u8 flags = D_8009B26C;

    if ((flags & 0x40) == 0) {
        UWP_LogMessage("[CAMPAIGN] Main_RunCampaign initial step: loading scene package...");
        D_8009B26C = flags | 0x40;
        Campaign_LoadScenePackage(gCampaignSceneIndex);
        gDuel_bEffectState = 0;
        UWP_LogMessage("[CAMPAIGN] Scene package loaded, returning to frame loop");
        return;
    }

    SceneScript_UpdateSlots(D_800EAE98);
    Script_RunTick();
    if (D_8009B26C & 0x40)
        return;
    UWP_LogMessage("[CAMPAIGN] Scene completed, fading out");
    SD_BGMFadeOut();
    Fade_WaitOut();
}
