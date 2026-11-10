#include "pc/compat/fs.h"
#include "paths.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void (*write_watch)(int,const char *);
const char *Paths_UserDir(void) { const char *v=getenv("MEMORIES_USER_DIR"); return v&&*v?v:"."; }
const char *Paths_ProgramDir(void) { const char *v=getenv("MEMORIES_PROGRAM_DIR"); return v&&*v?v:"."; }
int Paths_MakeDirs(const char *path)
{
    char copy[2048];size_t i,n=strlen(path);struct stat st;
    if(n>=sizeof(copy))return -1;memcpy(copy,path,n+1);
    for(i=3;i<=n;i++)if(copy[i]=='/'||copy[i]=='\\'||!copy[i]) {
        char c=copy[i];copy[i]=0;
        if(*copy&&mkdir(copy,0777)&& (stat(copy,&st)||!S_ISDIR(st.st_mode)))return -1;
        copy[i]=c;
    }
    return 0;
}
int Paths_Contained(const char *s)
{
    const char *at=s;
    if(!s||!*s||*s=='/'||strchr(s,'\\')||strchr(s,':'))return 0;
    while(*at) {const char *end=strchr(at,'/');size_t n=end?(size_t)(end-at):strlen(at);
        if(!n||(n==1&&at[0]=='.')||(n==2&&at[0]=='.'&&at[1]=='.'))return 0;
        if(!end)break;at=end+1;
    }return 1;
}
static int join(char *out,size_t size,const char *root,const char *relative,int make)
{
    char parent[2048],*slash;int n;
    if(!Paths_Contained(relative))return -1;
    n=snprintf(out,size,"%s/%s",root,relative);
    if(n<0||(size_t)n>=size)return -1;
    if(make&&strlen(out)<sizeof(parent)) {strcpy(parent,out);slash=strrchr(parent,'/');if(slash){*slash=0;return Paths_MakeDirs(parent);}}
    return 0;
}
int Paths_User(char *out,size_t size,const char *relative) { return join(out,size,Paths_UserDir(),relative,1); }
int Paths_Program(char *out,size_t size,const char *relative) { return join(out,size,Paths_ProgramDir(),relative,0); }
void Paths_MigrateLegacySaves(void) {}
void Paths_WriteBegin(void) { errno=0; }
const char *Paths_WriteReason(char *out,size_t size,const char *path)
{(void)path;snprintf(out,size,"%s",strerror(errno));if(write_watch)write_watch(0,out);return out;}
const char *Paths_WriteError(char *out,size_t size,const char *path)
{snprintf(out,size,"%s: %s",path,strerror(errno));if(write_watch)write_watch(0,out);return out;}
void Paths_WriteDone(const char *path) {(void)path;if(write_watch)write_watch(1,"");}
void Paths_WatchUserDir(void(*watch)(int,const char *)) {write_watch=watch;}
