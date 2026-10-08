#define MAIN_MODE_STATE_NEXT_AS_SCALAR
#define MAIN_MODE_STATE_ACTIVE_AS_SCALAR
#include "../types.h"
#include "main_frame.h"
#include "fade.h"
#include "main_modes.h"
#include "main_debug.h"
#include "main_loop.h"
#include "main_reset_frontend_runtime.h"
#include "../unmatched.h"
#include "main_mode_state.h"
#ifdef MEMORIES_PC
#include "pc/platform/title_jump.h"
#include "pc/saves/deck_menu.h"
#endif

#ifdef MEMORIES_UWP
#include <stdio.h>
extern void UWP_LogMessage(const char *msg);
#else
#define UWP_LogMessage(msg) ((void)0)
#endif

void Main_Loop(void) {
    UWP_LogMessage("[MAIN_LOOP] Main_PrepareFrontendLoop()...");
    Main_PrepareFrontendLoop();
    UWP_LogMessage("[MAIN_LOOP] Starting loop");
    for (;;) {
        u8 v;
        Main_AdvanceFrame();
#ifdef MEMORIES_PC
        TitleJump_Poll();
        DeckMenu_Poll(DECK_MENU_MAIN_LOOP);
#endif
        v = D_8009B26C;
        if ((v & 0x80) == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "[MAIN_LOOP] Resetting frontend runtime (mode=0x%02X)", v);
            UWP_LogMessage(buf);
            D_8009B26C = v | 0x80;
            Main_ResetFrontendRuntime();
        } else {
            static u8 last_mode = 0xFF;
            if (last_mode != (v & 0x1F)) {
                char buf[64];
                snprintf(buf, sizeof(buf), "[MAIN_LOOP] Running mode runner %u (0x%02X)", (v & 0x1F), v);
                UWP_LogMessage(buf);
                last_mode = (v & 0x1F);
            }
            gMain_apfnModeRunner[v & 0x1F]();
            if ((D_8009B26C & 0x40) == 0) Fade_WaitOut();
        }
    }
}
