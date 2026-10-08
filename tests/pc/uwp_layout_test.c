#include "psyq/libgte.h"
#include "psyq/libgpu.h"
#include "psyq/libgs.h"
#include "game/graphics_frame_buffer.h"
#include "game/duel_effect.h"
#include "game/display_object_position.h"
#include "overlays/main_menu/frontend.h"
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(GsOT) == 20, "PS1 ordering table descriptor must stay 20 bytes");
_Static_assert(offsetof(GsOT, org) == 4, "org must be a guest word");
_Static_assert(offsetof(GsOT, tag) == 16, "tag must be a guest word");
_Static_assert(sizeof(GraphicsFrameBuffer) == 0x5160, "frame stride must match guest RAM");
_Static_assert(sizeof(DuelEffectChannel) == 0x64, "text channel must retain PS1 stride");
_Static_assert(offsetof(DuelEffectChannel, field_30) == 0x30, "text object pointer offset");
_Static_assert(sizeof(gMain_apMenuEntries[0]) == 4, "menu entries are PS1 pointers");
_Static_assert(sizeof(D_80184558) == 4, "menu singleton occupies one PS1 word");
_Static_assert(sizeof(DisplayPositionGroup) == 0x38, "effect group stride");
_Static_assert(offsetof(DisplayPositionGroup, x) == 0x34, "effect position must follow three guest pointers");

int main(void)
{
    GsOT ot = {0};
    ot.org = (GsOT_TAG *)(uintptr_t)0x8009b4a8u;
    /* __ptr32 without __uptr sign-extends the PS1's high addresses on x64. */
    return (uintptr_t)ot.org != (uintptr_t)0x8009b4a8u;
}
