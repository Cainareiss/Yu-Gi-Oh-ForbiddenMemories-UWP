/* Exercise the real name glyph spawner against real 0x70-byte display slots.
 * The former UWP GlyphSprite view wrote its sequence at +0x72, which is
 * the following slot's next link. No renderer or live game is needed. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "ygo_types.h"
#include "psyq/libgte.h"
#include "psyq/libgpu.h"
#include "psyq/libgs.h"
#include "game/display_object.h"
#include "game/display_object_core.h"
#include "game/display_object_helpers.h"
#include "game/display_object_config.h"
#include "game/duel_effect.h"
#include "game/graphics_frame.h"
#include "overlays/password/name_entry_state.h"
#include "overlays/password/name_entry_keyboard.h"

_Static_assert(sizeof(void *) == 8, "exercise the Xbox host pointer width");
_Static_assert(sizeof(DisplayObject) == 0x70, "retail display-slot stride");
_Static_assert(sizeof(GlyphSprite) == 0x6c, "glyph view fits its display slot");
_Static_assert(offsetof(GlyphSprite, sourceGlyph) == 0x4c, "guest source pointer");
_Static_assert(sizeof(((GlyphSprite *)0)->sourceGlyph) == 4, "stored pointer width");
_Static_assert(offsetof(GlyphSprite, frame) == 0x60, "glyph frame counter");
_Static_assert(offsetof(GlyphSprite, textBoxSlot) == 0x67, "text-box slot");
_Static_assert(offsetof(GlyphSprite, sequence) == 0x6a, "glyph sequence byte");

DisplayObject D_800EFE48[DISPLAY_OBJECT_POOL_CAPACITY];
s16 D_800EFE38[DISPLAY_OBJECT_LIST_COUNT];
s16 D_800F2878[DISPLAY_OBJECT_LIST_COUNT];
u16 D_8009B410, D_8009B412;
volatile u16 D_8009AF74[4];
u8 tail_data_start[4];
DuelEffectChannel D_800EB0F8[DUEL_EFFECT_CHANNEL_COUNT];
SelectionFrame *G32 D_8016D404;
u8 D_8016D408;

/* The game binds this alias to the allocatable tail of the same pool. */
#define D_800F0548 (&D_800EFE48[DISPLAY_OBJECT_RESERVED_CAPACITY])
#include DISPLAY_LIST_CORE_SOURCE

#ifdef DISPLAY_LIST_REPRODUCE_OLD_GLYPH_ABI
/* Layout independently recovered from the installed 1.0.25.0 executable's
 * DWARF. Its plain 64-bit source pointer was aligned at +0x50 and shifted
 * the sequence onto +0x72, outside the display allocation. Compile the
 * unmodified spawner with this former view as the negative control. */
typedef struct {
    u8 pad_00[4];
    u32 flags;
    u8 pad_08[0x3c];
    s16 scale_x, scale_y;
    u32 field_48;
    DuelEffectEntry *sourceGlyph;
    u8 pad_50[0xa];
    s16 savedSourceX;
    u8 pad_5c[4];
    s16 frame;
    u8 pad_62[5];
    u8 textBoxSlot;
    u8 pad_68[2];
    u8 sequence;
} BaselineGlyphSprite;
_Static_assert(sizeof(BaselineGlyphSprite) == 0x78, "installed baseline size");
_Static_assert(offsetof(BaselineGlyphSprite, sequence) == 0x72,
               "installed baseline overwrites the neighbor's next byte");
#define GlyphSprite BaselineGlyphSprite
#endif
#include DISPLAY_LIST_SPAWN_SOURCE
#ifdef DISPLAY_LIST_REPRODUCE_OLD_GLYPH_ABI
#undef GlyphSprite
#endif

static DuelEffectEntry glyph;
static unsigned cases;

static void fail(const char *reason, int index, int sequence)
{
    fprintf(stderr, "%s: index=%d sequence=%d; next=%d flags=%04x\n",
            reason, index, sequence, D_800EFE48[index + 1].next,
            D_800EFE48[index + 1].flags);
    exit(1);
}

static void check_list(void)
{
    unsigned seen[DISPLAY_OBJECT_POOL_CAPACITY] = {0};
    int index = D_800EFE38[1], previous = -1, count = 0;
    while (index >= 0) {
        if (index >= DISPLAY_OBJECT_POOL_CAPACITY || seen[index]++) {
            fprintf(stderr, "display list contains an invalid index or cycle: %d\n", index);
            exit(1);
        }
        DisplayObject *object = &D_800EFE48[index];
        if (object->previous != previous || !(object->flags & DISPLAY_OBJECT_FLAG_ALLOCATED))
            abort();
        previous = index;
        index = object->next;
        ++count;
    }
    if (count != 6) abort();
}

static void spawn_case(int index, int sequence, int slot)
{
    unsigned char neighbor[DISPLAY_OBJECT_RECORD_SIZE];
    DisplayObject *object;
    s16 expectedPrevious = index;
    int i;
    memset(D_800EFE48, 0xa5, sizeof(D_800EFE48));
    DisplayObject_ResetPool();
    /* Occupy earlier general slots; the next free object is exactly index.
     * Put the following slot on the same list, so its link is live. */
    for (i = DISPLAY_OBJECT_RESERVED_CAPACITY; i < index; ++i)
        DisplayObject_AcquireSlot(i, 1);
    DisplayObject_AcquireSlot(index + 1, 1);
    memcpy(neighbor, &D_800EFE48[index + 1], sizeof(neighbor));
    /* Inserting at the head legitimately changes only neighbor.previous. */
    memcpy(neighbor, &expectedPrevious, sizeof(expectedPrevious));
    D_8016D408 = sequence;
    object = NameEntry_SpawnGlyphSprite(slot, &glyph);
    if (object != &D_800EFE48[index]) fail("allocator selected another slot", index, sequence);
    if (memcmp(neighbor, &D_800EFE48[index + 1], sizeof(neighbor)))
        fail("glyph spawner overwrote the following display slot", index, sequence);
    GlyphSprite *view = (GlyphSprite *)object;
    if (view->sequence != sequence || view->textBoxSlot != slot || view->sourceGlyph != &glyph)
        fail("glyph view did not retain its own fields", index, sequence);
    if (object->next != index + 1 || object->flags != DISPLAY_OBJECT_RENDERABLE_MASK + 8)
        fail("glyph spawner corrupted its own list fields", index, sequence);
    /* This exact state used to write 21 into object21.next and cause the
     * renderer to revisit an already released fading sprite forever. */
    if (index == 20 && sequence == 21) check_list();
    DisplayObject_Release(object);
    if (D_800EFE38[1] != index + 1 || D_800EFE48[index + 1].previous != -1)
        fail("release did not restore the live neighbor", index, sequence);
    ++cases;
}

int main(void)
{
    int index, sequence, slot;
    memset(&glyph, 0, sizeof(glyph));
    glyph.code_00 = 0x8260;
    glyph.x_0C = 40;
    glyph.y_0E = 18;
    for (slot = 0; slot < DUEL_EFFECT_CHANNEL_COUNT; ++slot) {
        D_800EB0F8[slot].field_3C = 22 + slot;
        D_800EB0F8[slot].field_40 = 24;
    }
    spawn_case(20, 21, 1);
    for (index = DISPLAY_OBJECT_RESERVED_CAPACITY; index < DISPLAY_OBJECT_POOL_CAPACITY - 1; ++index)
        for (sequence = 0; sequence < 256; ++sequence)
            for (slot = 0; slot < DUEL_EFFECT_CHANNEL_COUNT; ++slot)
                spawn_case(index, sequence, slot);
    printf("PASS: %u real glyph spawns preserve the following 0x70-byte display slot, list links and flags\n", cases);
    return 0;
}
