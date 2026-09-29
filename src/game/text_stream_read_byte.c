#include "../types.h"
#include "text_stream_commands.h"

u8 TextStream_ReadByte(TextStreamOwner *object)
{
#if defined(__x86_64__) || defined(_M_X64)
    u8 *ptr = (u8 *)(uintptr_t)object->streams[object->stream_index];
    u8 val = *ptr++;
    object->streams[object->stream_index] = (u32)(uintptr_t)ptr;
    return val;
#else
    return *object->streams[object->stream_index]++;
#endif
}
