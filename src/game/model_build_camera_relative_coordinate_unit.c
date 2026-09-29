#include "../types.h"
#include "model_update_view_metrics.h"
#include "../psyq/libgte.h"
#include "../psyq/libgpu.h"
#include "../psyq/libgs.h"
#include "../psyq/libhmd.h"
#include "../psyq/memory.h"
#include "model.h"

/* Builds `unit`'s local matrix from a camera-relative direction vector
   (derived from D_8009B47A/D_8009B47C) combined with the per-slot base
   angles on D_800F2C40[arg0]'s own coordinate unit, then parents it. */
void Model_BuildCameraRelativeCoordinateUnit(
    s32 arg0,
    GsCOORDUNIT *parent,
    GsCOORDUNIT *unit
) {
    SVECTOR packed;
    SVECTOR raw;
    MATRIX sp20;
    ModelSlot *slot;

    memset(&raw, 0, sizeof(raw));
    raw.vy = (s16)(MODEL_ANGLE_FULL_TURN - D_8009B47A);
    raw.vz = (s16)D_8009B47C;

    slot = &D_800F2C40[arg0];

    packed = raw;

    unit->flg = 0;
#if defined(__x86_64__) || defined(_M_X64)
#define D18_ROT(s) (((struct _GsCOORDUNIT *)(uintptr_t)(s)->field_D18)->rot)
    unit->rot.vx =
        (s16)(MODEL_ANGLE_FULL_TURN - D18_ROT(slot).vx);
    unit->rot.vy =
        (s16)(MODEL_ANGLE_FULL_TURN - D18_ROT(slot).vy);
    unit->rot.vz =
        (s16)(MODEL_ANGLE_FULL_TURN - D18_ROT(slot).vz);
#undef D18_ROT
#else
    unit->rot.vx =
        (s16)(MODEL_ANGLE_FULL_TURN - slot->field_D18->rot.vx);
    unit->rot.vy =
        (s16)(MODEL_ANGLE_FULL_TURN - slot->field_D18->rot.vy);
    unit->rot.vz =
        (s16)(MODEL_ANGLE_FULL_TURN - slot->field_D18->rot.vz);
#endif

    RotMatrixZXY(&unit->rot, &unit->matrix);
    RotMatrix_gte(&packed, &sp20);
    MulMatrix(&unit->matrix, &sp20);

    unit->matrix.t[2] = 0;
    unit->matrix.t[1] = 0;
    unit->matrix.t[0] = 0;
    unit->super = parent;
}
