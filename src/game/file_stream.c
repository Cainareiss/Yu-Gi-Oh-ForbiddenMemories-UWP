#include "../types.h"
#include "file_constants.h"
#include "file_cd_helpers.h"
#include "file_transfer.h"
#include "../psyq/libcd.h"
#include "../psyq/libds.h"
#include "../unmatched.h"

#if defined(MEMORIES_UWP) && defined(__x86_64__)
typedef struct {
    FileTransferDescriptor *descriptor;
    FileTransferCallback callback;
} FileTransferCallbackEntry;

static FileTransferCallbackEntry s_phase_callbacks[8];
static void (*s_control_callbacks[4])(void);

void FileTransfer_SetPhaseCallback(
    FileTransferDescriptor *descriptor,
    FileTransferCallback callback
)
{
    int i;
    int free_slot = -1;

    for (i = 0; i < (int)(sizeof(s_phase_callbacks) / sizeof(s_phase_callbacks[0])); ++i) {
        if (s_phase_callbacks[i].descriptor == descriptor) {
            s_phase_callbacks[i].callback = callback;
            return;
        }
        if (free_slot < 0 && s_phase_callbacks[i].descriptor == 0)
            free_slot = i;
    }
    if (free_slot >= 0) {
        s_phase_callbacks[free_slot].descriptor = descriptor;
        s_phase_callbacks[free_slot].callback = callback;
    }
}

FileTransferCallback FileTransfer_GetPhaseCallback(
    const FileTransferDescriptor *descriptor
)
{
    int i;
    for (i = 0; i < (int)(sizeof(s_phase_callbacks) / sizeof(s_phase_callbacks[0])); ++i) {
        if (s_phase_callbacks[i].descriptor == descriptor)
            return s_phase_callbacks[i].callback;
    }
    return 0;
}

void FileTransfer_SetControlCallback(unsigned slot, void (*callback)(void))
{
    if (slot < (unsigned)(sizeof(s_control_callbacks) / sizeof(s_control_callbacks[0])))
        s_control_callbacks[slot] = callback;
}

void (*FileTransfer_GetControlCallback(unsigned slot))(void)
{
    if (slot < (unsigned)(sizeof(s_control_callbacks) / sizeof(s_control_callbacks[0])))
        return s_control_callbacks[slot];
    return 0;
}
#endif

void File_InitTransferState(s32 value)
{
    D_8009B118 = value;
    D_8009B110 = 0;
    D_8009B108 = 0;
    D_8009B0F4 = 0;
    D_8009B120 = 0;
    D_8009B0F0 = 0;
    D_8009B134 = 0;
    D_8009B112 = 0;
    D_8009B10C = 0;
    D_8009B12C = 0;
    D_8009B124 = 0;
    D_8009B0E8 = 0;
    D_8009B130 = 0;
#if defined(MEMORIES_UWP) && defined(__x86_64__)
    FileTransfer_SetControlCallback(FILE_TRANSFER_CB_PHASE_END, 0);
    FileTransfer_SetControlCallback(FILE_TRANSFER_CB_PHASE_START, 0);
    FileTransfer_SetControlCallback(FILE_TRANSFER_CB_WAIT, 0);
#endif
    while (DsInit() == 0) {
    }
    D_8009B0FC = 1;
}

void File_GetPosition(s32 *output, const char *path)
{
    DslFILE file;

    while (DsSearchFile(&file, (char *)path) == 0) {
    }
    *output = CdPosToInt_8007E710((const CdlLOC *)&file.pos);
}

void File_SetTransferLocation(
    FileTransferDescriptor *transfer,
    s32 file_index,
    s32 sector_offset,
    s32 vertical
)
{
    transfer->total_bytes = vertical;
    file_index &= FILE_TRANSFER_FILE_INDEX_MASK;
    if (vertical < 0)
        transfer->total_bytes = -(vertical << FILE_SECTOR_SHIFT);

    if (sector_offset < 0) {
        transfer->file_bytes = 0;
        transfer->absolute_lba = -sector_offset;
    } else {
        register volatile s32 *lbas = gFile_anLba;

        transfer->file_bytes = sector_offset << FILE_SECTOR_SHIFT;
        transfer->absolute_lba = lbas[file_index] + sector_offset;
    }
}

FileTransferDescriptor *File_InitTransferDescriptor(
    FileTransferDescriptor *transfer,
    s32 flags,
    u8 *source,
    s32 sector,
    s32 vertical,
    FileTransferCallback callback,
    s32 field_40,
    s32 length
)
{
    transfer->loader_argument = source;
    File_SetTransferLocation(transfer, flags, sector, -vertical);
    transfer->done = 1;
    transfer->substate = 0;
    transfer->buffer_index = 0;
    transfer->phase_size = 0;
#if defined(MEMORIES_UWP) && defined(__x86_64__)
    transfer->phase_callback = 0;
    FileTransfer_SetPhaseCallback(transfer, callback);
#else
    transfer->phase_callback = callback;
#endif
    transfer->result = field_40;
    if (length) {
        if (flags & 0x1000000) {
            transfer->direct_destination = length;
        } else {
            transfer->phase_size = transfer->total_bytes;
            if (length < 0) {
                transfer->done = 1;
                transfer->value_0C = length;
                transfer->value_08 = length;
            } else {
                flags |= 0x10000;
                transfer->done = 2;
                transfer->field_30.h.field_32 = ((u32)length) >> 16;
                transfer->field_30.h.counter = length;
                transfer->w = FILE_TRANSFER_DEFAULT_IMAGE_WORD_WIDTH;
                transfer->h = FILE_TRANSFER_DEFAULT_IMAGE_HEIGHT;
                transfer->value_08 = D_8009B118;
                transfer->value_0C = D_8009B118 + FILE_SECTOR_SIZE;
            }
        }
    }
    transfer->status_flags = flags;
    return transfer;
}

FileTransferDescriptor *func_80013A94(s32 file_index, s32 sector_offset)
{
    FileTransferDescriptor *transfer;

    if (D_8009B0F4 & FILE_TRANSFER_STATE_SECONDARY_PENDING)
        return 0;

    transfer = &gFile_SecondaryTransferDescriptor;
    File_SetTransferLocation(
        transfer, file_index & FILE_TRANSFER_FILE_INDEX_MASK, sector_offset, 0
    );
    transfer->done = 0;
    transfer->status_flags = 0x00100000;
    D_8009B0F4 |= FILE_TRANSFER_STATE_SECONDARY_PENDING;
    return transfer;
}
