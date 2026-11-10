/* Exercise the real keyboard controller and the real eight-frame integrator.
 * The table fixture is read from the user's disc by the Python driver. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#define GINPUT_PAD1_HELD_IS_VOLATILE
#define GINPUT_PAD1_REPEAT_IS_VOLATILE
#define GINPUT_PAD1_PRESSED_IS_VOLATILE
#include "game/input.h"
#include "game/display_object_helpers.h"
#include "game/sound.h"
#include "overlays/password/name_entry_state.h"
#include "overlays/password/name_entry_keyboard.h"
#include "overlays/password/name_entry_tables.h"

_Static_assert(sizeof(void *) == 8, "exercise the Xbox host pointer width");
_Static_assert(sizeof(gPassword_ModuleState.selectionFrame) == 4,
               "a stored guest pointer must remain four bytes");
_Static_assert(offsetof(SelectionFrame, x) == 0x30, "selection x offset");
_Static_assert(offsetof(SelectionFrame, stepX) == 0x36, "selection velocity offset");
_Static_assert(offsetof(SelectionFrame, width) == 0x3c, "selection width offset");
_Static_assert(offsetof(SelectionFrame, timer) == 0x60, "selection timer offset");
_Static_assert(offsetof(DisplayObjectVelocity, fraction_x) == 0x62,
               "integrator fractions belong to the complete display object");

PasswordModuleState gPassword_ModuleState;
volatile u16 gInput_wPad1Held, gInput_wPad1Repeat, gInput_wPad1Pressed;
s8 D_8016AB38[9][15];
u8 D_8016ABC0[9][2];
static union {
    SelectionFrame selection;
    DisplayObjectVelocity motion;
    unsigned char complete[0x100];
} frame;
static int transitions, frames;

/* Bind the named retail globals to the typed state without mapping guest RAM.
 * Include the unmodified production controller after its headers are guarded. */
#define D_8016D400 (gPassword_ModuleState.flags)
#define D_8016D401 (gPassword_ModuleState.keyboardColumn)
#define D_8016D402 (gPassword_ModuleState.keyboardRow)
#define D_8016D404 (gPassword_ModuleState.selectionFrame)
#define D_8016D418 (gPassword_ModuleState.nameBuffer)
#define D_8016D426 (gPassword_ModuleState.savedKeyboardRow)
#define D_8016D42C (gPassword_ModuleState.caretIndex)
#define D_8016D434 (gPassword_ModuleState.cursorTargetX)
#define D_8016D436 (gPassword_ModuleState.cursorTargetY)
#define D_8016D4D4 (gPassword_ModuleState.keyboardFlags)
#ifndef NAME_ENTRY_SOURCE
#define NAME_ENTRY_SOURCE "overlays/password/name_entry_keyboard_update.c"
#endif
#include NAME_ENTRY_SOURCE

static void fail(const char *reason, int row, int col, int saved, int direction)
{
    fprintf(stderr,
            "%s: row=%d col=%d saved=%d action=%d; actual=(%d,%d) "
            "xy=(%d,%d) size=%d timer=%d flags=%04x\n",
            reason, row, col, saved, direction,
            (int)gPassword_ModuleState.keyboardRow,
            (int)gPassword_ModuleState.keyboardColumn,
            frame.selection.x, frame.selection.y, frame.selection.width,
            frame.selection.timer, gPassword_ModuleState.keyboardFlags);
    exit(1);
}

/* These are unrelated text/audio interfaces. Navigation must never invoke
 * the glyph functions, while position integration is linked from production. */
void SD_SEPlayFull(u32 sound) { (void)sound; }
s32 NameEntry_AdjustLength(s32 delta, s32 arg)
{ (void)delta; (void)arg; abort(); }
DuelEffectEntry *TextBox_GetGlyphAt(s32 kind, s32 x, s32 y)
{ (void)kind; (void)x; (void)y; abort(); }
void *NameEntry_SpawnGlyphSprite(s32 kind, DuelEffectEntry *node)
{ (void)kind; (void)node; abort(); }
void NameEntry_UpdateGlyphPulse(u8 *object) { (void)object; abort(); }
void NameEntry_UpdateGlyphTransfer(u8 *object) { (void)object; abort(); }

typedef struct { int row, col, width, x, y, saved; } Target;

/* Oracle reads signed descriptors separately from the controller's memory
 * walk. Negative entries redirect the column; nonzero entries select a row. */
static Target resolve(int row, int col, int saved)
{
    int value, steps = 0;
    Target target;
    for (;;) {
        if (row < 0 || row >= 9 || col < 0 || col >= 15 || ++steps > 15)
            abort();
        value = D_8016AB38[row][col];
        if (value >= 0) break;
        col += value;
    }
    target.row = value ? value & 15 : saved;
    target.col = col;
    target.width = 16 + ((value & 64) ? 20 : 0);
    target.x = col * 20 + (col >= 11 ? 42 : 22);
    target.y = target.row * 18 + 24;
    target.saved = saved;
    return target;
}

static void transition(int row, int col, int saved, int direction)
{
    static const u16 inputs[] = {
        PAD_DIRECTION_LEFT, PAD_DIRECTION_RIGHT,
        PAD_DIRECTION_UP, PAD_DIRECTION_DOWN
    };
    Target start = resolve(row, col, saved), target;
    int nextRow = row, nextCol = col, nextSaved = saved, tick;
    if (direction < 2) {
        nextCol = (col + (direction == 0 ? 14 : 1)) % 15;
    } else if (direction < 4) {
        if (col >= 11) {
            nextCol = 11;
            nextRow = D_8016ABC0[row][direction == 3];
        } else {
            nextRow = (row + (direction == 2 ? 8 : 1)) % 9;
        }
        nextSaved = nextRow;
    } else {
        nextRow = 8;
        nextCol = 14;
    }
    target = resolve(nextRow, nextCol, nextSaved);
    memset(&gPassword_ModuleState, 0, sizeof(gPassword_ModuleState));
    memset(&frame, 0, sizeof(frame));
    gPassword_ModuleState.keyboardRow = row;
    gPassword_ModuleState.keyboardColumn = col;
    gPassword_ModuleState.savedKeyboardRow = saved;
    gPassword_ModuleState.selectionFrame = &frame.selection;
    frame.selection.x = start.x;
    frame.selection.y = start.y;
    frame.selection.width = start.width;
    frame.selection.widthBonus = start.width - 16;
    gInput_wPad1Held = direction < 4 ? inputs[direction] : 0;
    gInput_wPad1Pressed = direction == 4 ? PAD_BUTTON_START : 0;
    gInput_wPad1Repeat = 0;
    NameEntry_UpdateKeyboard();
    ++transitions;
    if (gPassword_ModuleState.keyboardRow != target.row ||
        gPassword_ModuleState.keyboardColumn != target.col ||
        gPassword_ModuleState.cursorTargetX != target.x ||
        gPassword_ModuleState.cursorTargetY != target.y)
        fail("selected an invalid/invisible destination", row, col, saved, direction);
    gInput_wPad1Held = gInput_wPad1Pressed = gInput_wPad1Repeat = 0;
    for (tick = 0; tick < 8; ++tick) {
        NameEntry_UpdateKeyboard();
        ++frames;
        if (frame.selection.x < 22 || frame.selection.y < 24 ||
            frame.selection.x + frame.selection.width > 320 ||
            frame.selection.y + 16 > 192)
            fail("animation escaped the keyboard", row, col, saved, direction);
    }
    if (frame.selection.x != target.x || frame.selection.y != target.y ||
        frame.selection.width != target.width || frame.selection.timer != 0 ||
        (gPassword_ModuleState.keyboardFlags & 0x4000))
        fail("animation did not settle on its destination", row, col, saved, direction);
}

int main(int argc, char **argv)
{
    FILE *file;
    int row, col, saved, direction;
    if (argc != 2 || !(file = fopen(argv[1], "rb"))) return 2;
    if (fread(D_8016AB38, 1, sizeof(D_8016AB38), file) != sizeof(D_8016AB38) ||
        fread(D_8016ABC0, 1, sizeof(D_8016ABC0), file) != sizeof(D_8016ABC0))
        return 3;
    fclose(file);
    for (row = 0; row < 9; ++row) for (col = 0; col < 15; ++col) {
        for (saved = 0; saved < 9; ++saved) {
            Target start = resolve(row, col, saved);
            if (start.row != row || start.col != col) continue;
            for (direction = 0; direction < 5; ++direction)
                transition(row, col, saved, direction);
        }
    }
    printf("PASS: %d navigation transitions, %d real animation frames; "
           "guest pointers remain four bytes on x64\n", transitions, frames);
    return 0;
}
