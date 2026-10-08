#define SDVALUE_CUSTOM_EXTERN
#include "../types.h"
#include "sound.h"
#include "sound_output_state.h"
#include "file_transfer.h"

void SD_ArmBusyCallback(void) {
    SDValue *state;
#if defined(MEMORIES_UWP) && defined(__x86_64__)
    state = (SDValue *)(uintptr_t)(*(u32 *)(uintptr_t)0x8009B45C);
    state->busy = 1;
    FileTransfer_SetControlCallback(FILE_TRANSFER_CB_BUSY, SD_ClearBusyFlag);
#else
    /* The absolute spelling is load-bearing in this -G8 unit. */
    state = *(SDValue **)0x8009B45C;
    state->busy = 1;
    *(void (**)(void))0x8009B128 = SD_ClearBusyFlag;
#endif
}
