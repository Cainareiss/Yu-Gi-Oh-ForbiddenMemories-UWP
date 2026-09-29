#include "crash.h"
#include <windows.h>
#include <stdio.h>

char Crash_ReportDir[256] = ".";

void Crash_Init(void) {}
void Crash_ChooseReportDir(void) {}

void Crash_ReportSoft(const char *kind, const char *detail)
{
    if (kind && detail) {
        fprintf(stderr, "[SOFT ERROR] %s: %s\n", kind, detail);
        OutputDebugStringA("[SOFT ERROR] ");
        OutputDebugStringA(kind);
        OutputDebugStringA(": ");
        OutputDebugStringA(detail);
        OutputDebugStringA("\n");
    }
}

void Crash_ReportFatal(const char *kind, const char *detail)
{
    if (kind && detail) {
        fprintf(stderr, "[FATAL ERROR] %s: %s\n", kind, detail);
        OutputDebugStringA("[FATAL ERROR] ");
        OutputDebugStringA(kind);
        OutputDebugStringA(": ");
        OutputDebugStringA(detail);
        OutputDebugStringA("\n");
    }
}

void Crash_ReportHang(void *context)
{
    (void)context;
}
