/* Windows platform facts needed by the guest core, without a desktop
 * window, child-process monitor, injected clock or shell APIs. */
#include "pc/compat/fs.h"
#include "win32.h"
#include "uwp_bridge.h"
#include "pc/compat/signal.h"
#include "uwp_winapi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
_Static_assert(sizeof(struct stat)==sizeof(struct _stat64i32),"MSVC stat alias must preserve layout");
_Static_assert(offsetof(struct stat,st_size)==offsetof(struct _stat64i32,st_size),"MSVC stat size offset");
_Static_assert(offsetof(struct stat,st_mtime)==offsetof(struct _stat64i32,st_mtime),"MSVC stat time offset");
_Static_assert(sizeof(jmp_buf)==256,"MSVC and MinGW share the Windows x64 jump buffer");
int Memories_UwpCheckJumpAbi(void)
{
    jmp_buf point;
    volatile int reached=0;
    int value=setjmp(point);
    if (!value) {reached=1;longjmp(point,7);}
    return value==7 && reached==1;
}
extern IMAGE_DOS_HEADER __ImageBase;
void *Memories_UwpImageBase(void) {return &__ImageBase;}
void *Memories_UwpMapView(void *section,unsigned long access,unsigned long high,unsigned long low,size_t size,void *base)
{
    void *placeholder, *mapped;
    DWORD error;
    (void)access;
    if (!base)
        return MapViewOfFile3FromApp(section,NULL,NULL,((uint64_t)high<<32)|low,size,0,PAGE_READWRITE,NULL,0);
    /* FromApp only accepts an explicit base when replacing a placeholder.
     * Reserve exactly the guest view first; never let the allocator choose
     * a nearby address, since every guest pointer refers to this address. */
    placeholder=VirtualAlloc2FromApp(NULL,base,size,MEM_RESERVE|MEM_RESERVE_PLACEHOLDER,PAGE_NOACCESS,NULL,0);
    if (!placeholder) return NULL;
    mapped=MapViewOfFile3FromApp(section,NULL,placeholder,((uint64_t)high<<32)|low,size,
                               MEM_REPLACE_PLACEHOLDER,PAGE_READWRITE,NULL,0);
    if (!mapped) {
        error=GetLastError(); VirtualFree(placeholder,0,MEM_RELEASE); SetLastError(error);
    }
    return mapped;
}
int Memories_UwpMoveFile(const unsigned short *from,const unsigned short *to,unsigned long flags)
{
    if(flags&MOVEFILE_REPLACE_EXISTING) {
        if(ReplaceFileFromAppW((const wchar_t *)to,(const wchar_t *)from,NULL,0,NULL,NULL))return 1;
        if(GetLastError()!=ERROR_FILE_NOT_FOUND&&GetLastError()!=ERROR_PATH_NOT_FOUND)return 0;
    }
    return MoveFileFromAppW((const wchar_t *)from,(const wchar_t *)to);
}
void bzero(void *p,size_t n) {memset(p,0,n);}
int Memories_SigProcMask(int how,const sigset_t *set,sigset_t *previous)
{(void)how;(void)set;if(previous)*previous=0;return 0;}
int Win32_StartInterrupt(void(*tick)(uintptr_t,void *)) {(void)tick;return -1;}
int Win32_StartWatch(void) {return 0;}
void Win32_StopInterrupt(void) {}
void Win32_SetStallReporter(void(*report)(void *),unsigned seconds) {(void)report;(void)seconds;}
void Win32_Heartbeat(void) {}
int Win32_UndoInterruptedFault(void *context) {(void)context;return 0;}
void Win32_ServiceInterrupt(void) {}
void Win32_ClockRepairs(unsigned *a,unsigned *b,unsigned *c) {if(a)*a=0;if(b)*b=0;if(c)*c=0;}
void Win32_ContextRegisters(const void *ctx,uintptr_t *pc,uintptr_t *sp,uintptr_t *fp)
{const CONTEXT *c=ctx;*pc=c->Rip;*sp=c->Rsp;*fp=c->Rbp;}
void Win32_ImageRange(uintptr_t *low,uintptr_t *high)
{const unsigned char *base=(const unsigned char *)&__ImageBase;const IMAGE_NT_HEADERS *nt=(const void *)(base+__ImageBase.e_lfanew);*low=(uintptr_t)base;*high=*low+nt->OptionalHeader.SizeOfImage;}
void Win32_StackRange(uintptr_t *low,uintptr_t *high)
{__asm__ volatile("movq %%gs:0x10,%0\n\tmovq %%gs:8,%1":"=r"(*low),"=r"(*high));}
int Win32_ModuleName(uintptr_t address,char *out,unsigned size,uintptr_t *offset)
{uintptr_t low,high;Win32_ImageRange(&low,&high);if(address<low||address>=high)return 0;snprintf(out,size,"YuGiOhForbiddenMemoriesUWP.exe");*offset=address-low;return 1;}
int Win32_UnwindCallers(uintptr_t pc,uintptr_t sp,uintptr_t fp,uintptr_t *callers,int count)
{(void)sp;(void)fp;if(count>0){callers[0]=pc;return 1;}return 0;}
int Win32_CurrentCallers(int skip,uintptr_t *callers,int count)
{(void)skip;if(count>0){callers[0]=(uintptr_t)__builtin_return_address(0);return 1;}return 0;}
int Win32_Restart(void) {return -1;}
const char *Win32_FontPath(int japanese)
{const char *p=getenv(japanese?"MEMORIES_JAPANESE_FONT":"MEMORIES_FONT");return p&&*p?p:NULL;}
const char *Win32_SerifFontPath(void) {return Win32_FontPath(0);}
void Win32_GuardStack(uintptr_t low,unsigned room)
{DWORD old;VirtualProtect((void *)(low+room),0x1000,PAGE_READWRITE|PAGE_GUARD,&old);}
static Win32CrashReport crash_report;
static LONG CALLBACK on_unclaimed(EXCEPTION_POINTERS *p)
{
    DWORD code=p->ExceptionRecord->ExceptionCode;
    uintptr_t pc=(uintptr_t)p->ExceptionRecord->ExceptionAddress,low,high;
    if(code!=EXCEPTION_ACCESS_VIOLATION&&code!=EXCEPTION_ILLEGAL_INSTRUCTION&&code!=EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    Win32_ImageRange(&low,&high);
    if(crash_report && ((pc>=low&&pc<high)||(pc>=0x80000000u&&pc<0x80200000u)))
        crash_report(code,p->ExceptionRecord->NumberParameters>=2?p->ExceptionRecord->ExceptionInformation[1]:0,
            p->ContextRecord->Rip,p->ContextRecord->Rsp,p->ContextRecord->Rbp);
    return EXCEPTION_CONTINUE_SEARCH;
}
void Win32_SetCrashReporter(Win32CrashReport report)
{crash_report=report;AddVectoredExceptionHandler(0,on_unclaimed);}
