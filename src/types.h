#ifndef YUGIOH_TYPES_H
#define YUGIOH_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* MinGW defines __ptr32 as an empty compatibility macro. Clang's MS
 * extension is needed for pointers stored in the original PS1 data layout. */
#if defined(MEMORIES_UWP) && defined(__x86_64__)
#undef __ptr32
#define PSX_PTR32 __ptr32 __uptr
#define PSX_PTR32_VALUE(member) (*((const u32 *)(const void *)&(member)))
#define PSX_PTR32_HOST(type, member) ((type *)(uintptr_t)PSX_PTR32_VALUE(member))
#else
#define PSX_PTR32
#define PSX_PTR32_VALUE(member) (member)
#define PSX_PTR32_HOST(type, member) ((type *)(member))
#endif

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
#if defined(MEMORIES_PC) && defined(__i386__)
/* 32-bit x86 Linux aligns a 64-bit integer to 4 inside a structure; the
 * retail layout (MIPS) and 32-bit Windows align it to 8. This makes all three
 * agree, which the game's memory image and a mod built once for both systems
 * rely on (tools/pc/check_layouts.py). */
typedef signed long long s64 __attribute__((aligned(8)));
typedef unsigned long long u64 __attribute__((aligned(8)));
#else
typedef signed long long s64;
typedef unsigned long long u64;
#endif

#endif
