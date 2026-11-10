#include "pc/compat/fs.h"
#include "uwp_bridge.h"
#include "paths.h"
#include "win32.h"
#include "pc/debug/monitor.h"
#include "pc/debug/crash.h"
#include "pc/debug/crash_test.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
char Crash_ReportDir[640];
volatile int CrashTest_TickHang;
static MonitorShared shared;
MonitorShared *Monitor_Shared(void) { return &shared; }
int Monitor_Active(void) { return 0; }
int Monitor_Main(int argc,char **argv,int *status) {(void)argc;(void)argv;(void)status;return 0;}
void Monitor_Modal(int on) { if(on)shared.modal++;else if(shared.modal)shared.modal--; }
void Monitor_Fact(const char *key,const char *format,...)
{
    char text[1024];va_list ap;va_start(ap,format);vsnprintf(text,sizeof(text),format,ap);va_end(ap);
    char line[1200];snprintf(line,sizeof(line),"%s: %s",key,text);UWP_LogMessage(line);
}
size_t Monitor_Facts(char *out,size_t size) {if(size)*out=0;return 0;}
void Monitor_NoteSystem(void) {Monitor_Fact("platform","native UWP x64, G32");}
void Crash_ChooseReportDir(void) {snprintf(Crash_ReportDir,sizeof(Crash_ReportDir),"%s/reports",Paths_UserDir());Paths_MakeDirs(Crash_ReportDir);}
static void report_exception(unsigned long code,uintptr_t fault,uintptr_t pc,uintptr_t sp,uintptr_t fp)
{
    char detail[256];
    snprintf(detail,sizeof(detail),"exception 0x%08lx fault=%llx RIP=%llx RSP=%llx RBP=%llx",code,
        (unsigned long long)fault,(unsigned long long)pc,(unsigned long long)sp,(unsigned long long)fp);
    Crash_ReportFatal("native exception",detail);
}
void Crash_Init(void) {Crash_ChooseReportDir();Win32_SetCrashReporter(report_exception);}
static void report(const char *kind,const char *detail)
{
    char message[2048],path[1024];FILE *f;
    snprintf(message,sizeof(message),"%s: %s",kind,detail?detail:"");UWP_LogMessage(message);
    snprintf(path,sizeof(path),"%s/native-core.log",Crash_ReportDir);f=fopen(path,"ab");
    if(f){fprintf(f,"%s\n",message);fclose(f);}
}
void Crash_ReportSoft(const char *kind,const char *detail) {report(kind,detail);}
void Crash_ReportFatal(const char *kind,const char *detail) {report(kind,detail);}
void Crash_ReportHang(void *context) {(void)context;report("hang","the game stopped reaching VBlank");}
void CrashTest_Init(void) {}
void CrashTest_Frame(void) {}
void CrashTest_Present(void) {}
void Update_Start(void) {}
void Update_Frame(void) {}
void Update_CheckNow(void) {}
void Update_OpenReleases(void) {}
const char *Update_VersionLabel(void) {return "Native UWP core v0.3.0";}
