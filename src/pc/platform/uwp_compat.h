#ifndef MEMORIES_UWP_COMPAT_H
#define MEMORIES_UWP_COMPAT_H
/* Included before every C unit. The guest ABI remains upstream's G32,
 * including pointer ZERO extension; this header changes platform services. */
#if !defined(__ASSEMBLER__)
#include <stddef.h>
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000005
#endif
void *Memories_UwpImageBase(void);
void *Memories_UwpMapView(void *section,unsigned long access,unsigned long high,unsigned long low,size_t size,void *base);
int Memories_UwpMoveFile(const unsigned short *from,const unsigned short *to,unsigned long flags);
/* The app-container APIs have the same signatures as these desktop APIs. */
#define VirtualAlloc VirtualAllocFromApp
#define VirtualProtect VirtualProtectFromApp
#endif
#endif
