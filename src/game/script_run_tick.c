#define D_800EB12C_IN_DATA
#include "../types.h"
#include "../ygo_types.h"
#include "../psyq/rand.h"
#include "duel_effect.h"
#include "func_80039794.h"
#include "script_op_show_dialog.h"
#include "script_command_table.h"
#include "script_run_tick.h"
#include "script_state.h"

#ifdef MEMORIES_UWP
#include <stdio.h>
extern void UWP_LogMessage(const char *msg);
#endif

void Script_RunTick(void)
{
    u16 f;
    s32 g;
    s32 b;

    rand();
    f = D_8009B2A4;
    if (f & DUEL_EVENT_SCRIPT_FLAG_DIALOG_ACTIVE) {
        func_80039794();
#ifdef MEMORIES_UWP
        {
            static int dlg_logged = 0;
            if (dlg_logged++ < 10 || (D_800EB12C & TEXT_BOX_COMPLETION_MASK) == TEXT_BOX_FLAG_DONE) {
                char buf[128];
                snprintf(buf, sizeof(buf), "[RUN_TICK] Dialog active: D_800EB12C=0x%08X, D_8009B2A4=0x%04X",
                         (unsigned)D_800EB12C, (unsigned)D_8009B2A4);
                UWP_LogMessage(buf);
            }
        }
#endif
        if ((D_800EB12C & TEXT_BOX_COMPLETION_MASK) == TEXT_BOX_FLAG_DONE) {
            D_8009B2A4 &= ~DUEL_EVENT_SCRIPT_FLAG_DIALOG_ACTIVE;
        }
        return;
    }
    if (f == 0) {
        return;
    }
    if ((f & DUEL_EVENT_SCRIPT_FLAG_STARTED) == 0) {
        g = D_8009B2A4;
        D_8009B290 = D_801A8000 + ((u16 *)D_801A8000)[g];
        b = *D_8009B290++;
        D_8009B28C = 0;
        D_8009B2A4 = g | DUEL_EVENT_SCRIPT_FLAG_STARTED;
        D_8009B27C = b;
#ifdef MEMORIES_UWP
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "[RUN_TICK] Script started: scene_idx=%d, D_8009B290=%p, first_op=0x%02X",
                     (int)g, (void *)D_8009B290, (unsigned)b);
            UWP_LogMessage(buf);
        }
#endif
    }
    if (D_8009B28C != 0) {
        D_8009B27C = D_8009B28C;
    }
    while (1) {
#ifdef MEMORIES_UWP
        {
            static int tick_ops = 0;
            if (tick_ops++ < 30) {
                char buf[128];
                snprintf(buf, sizeof(buf), "[RUN_TICK] Dispatching script op 0x%02X (handler index %d)",
                         (unsigned)D_8009B27C, (int)(D_8009B27C & 0x1F));
                UWP_LogMessage(buf);
            }
        }
#endif
        D_80090C50[D_8009B27C & 0x1F]();
        if (D_8009B27C != 0) {
            return;
        }
        D_8009B27C = *D_8009B290++;
    }
}
