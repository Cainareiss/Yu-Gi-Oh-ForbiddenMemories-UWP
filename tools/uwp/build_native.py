#!/usr/bin/env python3
"""Build upstream's native G32 game core for the UWP C++ host.

Reuse the exact resident/overlay compilation, symbol pins, module bank
renames and branch gates of build_game32.py. Only its platform backend and
final executable link are replaced. The static library contains no window
or process launcher. The optional console harness is a validation tool and
must never be packaged as the UWP application.
"""
import argparse, hashlib, importlib.util, json, os, re, shutil, struct, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
EXCLUDE={
 'src/pc/guest/main.c','src/pc/platform/win32.c','src/pc/platform/paths.c',
 'src/pc/platform/update_check.c','src/pc/platform/update_net.c',
 'src/pc/debug/monitor.c','src/pc/debug/crash.c','src/pc/debug/crash_test.c',
}

def audit_guest_relocations(objects, pins_file, report_file):
    """Reject RIP-relative references to fixed PS1 addresses before linking.

    The UWP loader can relocate the host image even with DYNAMICBASE off.
    An ABS guest symbol does not move; a REL32 displacement to it would then
    reach guest_address + image_delta. Large-model ADDR64 references and
    ordinary .refptr cells preserve the guest address across that rebase.
    """
    pins=set(re.findall(r'^\.set (\w+),',pins_file.read_text(),re.M))
    violations=[]; references=0
    for path in objects:
        data=Path(path).read_bytes()
        machine,sections,_,symbols_at,count,optional,_=struct.unpack_from('<HHIIIHH',data)
        if machine!=0x8664:raise RuntimeError('Expected x64 COFF: '+str(path))
        strings_at=symbols_at+count*18
        def name(index):
            raw=data[symbols_at+index*18:symbols_at+index*18+8]
            if raw[:4]==b'\0'*4:
                start=strings_at+struct.unpack_from('<I',raw,4)[0]
                raw=data[start:data.index(b'\0',start)]
            return raw.rstrip(b'\0').decode('ascii')
        for index in range(sections):
            header=20+optional+index*40
            relocation_at=struct.unpack_from('<I',data,header+24)[0]
            relocation_count=struct.unpack_from('<H',data,header+32)[0]
            for at in range(relocation_at,relocation_at+relocation_count*10,10):
                offset,symbol,kind=struct.unpack_from('<IIH',data,at)
                symbol_name=name(symbol)
                if symbol_name not in pins:continue
                references+=1
                if 4<=kind<=9: # IMAGE_REL_AMD64_REL32 through REL32_5
                    violations.append({'object':str(path),'offset':offset,'symbol':symbol_name,'type':kind})
    report={'objects':len(objects),'pinned_symbols':len(pins),'guest_references':references,
            'unsafe_relative_references':violations,'passed':not violations}
    report_file.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if violations:raise RuntimeError('Relocation-unsafe guest references: '+str(violations[:8]))
    print(f'Relocation audit: {len(objects)} objects, {references} guest references, zero unsafe REL32 pins',flush=True)
def replace_function(text,name,replacement):
    pattern=re.compile(r'^[^\n;{}]*\b'+re.escape(name)+r'\([^;{}]*\)\s*\{',re.M)
    match=pattern.search(text)
    if not match:raise RuntimeError('Upstream function changed: '+name)
    at=match.end();depth=1
    while depth:
        if text[at]=='{':depth+=1
        elif text[at]=='}':depth-=1
        at+=1
    return text[:match.start()]+replacement+'\n'+text[at:]
def adapt(source,text):
    if source=='src/pc/guest/image.c':
        text=text.replace('GetModuleHandleW(NULL)','((HMODULE)Memories_UwpImageBase())')
        text=text.replace('CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, protection, 0, MEMORIES_GUEST_RAM_SIZE + 0x10000u, NULL)',
          'CreateFileMappingFromApp(INVALID_HANDLE_VALUE, NULL, protection, MEMORIES_GUEST_RAM_SIZE + 0x10000u, NULL)')
        text=text.replace('MapViewOfFileEx(', 'Memories_UwpMapView(')
    elif source=='src/pc/platform/platform_common.c':
        text=text.replace('#ifndef __ANDROID__ /* android.c: GLES only */',
          '#if !defined(__ANDROID__) && !defined(MEMORIES_UWP) /* native UWP has no GL */')
    elif source=='src/pc/compat/fs.c':
        text=text.replace('#include <windows.h>', '#include "pc/platform/uwp_winapi.h"\n#include "pc/platform/uwp_bridge.h"')
        text=text.replace('file = wide_mode ? _wfopen(wide, wide_mode) : NULL;',r'''file = wide_mode ? _wfopen(wide, wide_mode) : NULL;
    if (!file && mode[0] == 'r' && !strchr(mode, '+')) {
        intptr_t handle = UWP_OpenReadHandle(path);
        if (handle != -1) {
            int fd = _open_osfhandle(handle, _O_RDONLY | (strchr(mode,'b') ? _O_BINARY : _O_TEXT));
            if (fd < 0) CloseHandle((HANDLE)handle);
            else { file = _fdopen(fd, mode); if (!file) _close(fd); }
        }
    }''')
        text=text.replace('result = _wopen(wide, flags, mode);',r'''result = _wopen(wide, flags, mode);
    if (result < 0 && !(flags & (_O_WRONLY | _O_RDWR | _O_CREAT | _O_TRUNC | _O_APPEND))) {
        intptr_t handle = UWP_OpenReadHandle(path);
        if (handle != -1) {
            result = _open_osfhandle(handle, flags);
            if (result < 0) CloseHandle((HANDLE)handle);
        }
    }''')
        text=replace_function(text,'Memories_Getenv',r'''char *Memories_Getenv(const char *name)
{
 wchar_t *wide_name=Memories_Utf8ToWide(name);const wchar_t *wide;
 EnvironmentEntry *entry;char *value=NULL,*result=NULL;
 if(!wide_name)return NULL;
 AcquireSRWLockExclusive(&environment_lock);
 wide=_wgetenv(wide_name);if(wide)value=Memories_WideToUtf8(wide);
 for(entry=environment;entry&&strcmp(entry->name,name);entry=entry->next){}
 if(value&&!entry){entry=calloc(1,sizeof(*entry));if(entry){entry->name=strdup(name);
  if(!entry->name){free(entry);entry=NULL;}else{entry->next=environment;environment=entry;}}}
 if(entry){if(!entry->value||!value||strcmp(entry->value,value)){free(entry->value);entry->value=value;value=NULL;}result=entry->value;}
 free(value);free(wide_name);ReleaseSRWLockExclusive(&environment_lock);return result;
}''')
        text=replace_function(text,'Memories_Argv','char **Memories_Argv(int *argc) { (void)argc; return NULL; }')
        text=replace_function(text,'Memories_Readlink',r'''long Memories_Readlink(const char *path,char *out,size_t size)
{
 const char *root=Memories_Getenv("MEMORIES_PROGRAM_DIR");int n;
 if(strcmp(path,"/proc/self/exe")||!root){errno=EINVAL;return -1;}
 n=snprintf(out,size,"%s/YuGiOhForbiddenMemoriesUWP.exe",root);
 return n<0||(size_t)n>=size?-1:n;
}''')
        text=text.replace('MoveFileExW(', 'Memories_UwpMoveFile(')
        start=text.index('struct MemoriesDir {')
        end=text.index('char **Memories_Argv',start)
        text=text[:start]+r'''
struct MemoriesDir { HANDLE find; WIN32_FIND_DATAW data; int first; struct MemoriesDirent entry; };
MemoriesDir *Memories_Opendir(const char *path)
{
 size_t n=strlen(path);char *pattern=malloc(n+3);wchar_t *wide;MemoriesDir *dir;
 if(!pattern)return NULL;memcpy(pattern,path,n);memcpy(pattern+n,"/*",3);
 wide=Memories_Utf8ToWide(pattern);free(pattern);if(!wide)return NULL;
 dir=calloc(1,sizeof(*dir));
 if(dir){dir->find=FindFirstFileExFromAppW(wide,FindExInfoBasic,&dir->data,FindExSearchNameMatch,NULL,0);
  if(dir->find==INVALID_HANDLE_VALUE){free(dir);dir=NULL;}else dir->first=1;}
 free(wide);return dir;
}
struct MemoriesDirent *Memories_Readdir(MemoriesDir *dir)
{
 if(!dir->first&&!FindNextFileW(dir->find,&dir->data))return NULL;dir->first=0;
 if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,dir->data.cFileName,-1,dir->entry.d_name,sizeof(dir->entry.d_name),NULL,NULL))return NULL;
 return &dir->entry;
}
int Memories_Closedir(MemoriesDir *dir){int result=FindClose(dir->find)?0:-1;free(dir);return result;}
''' +text[end:]
    return text
class Archived(Exception):pass

def build_dependencies(toolchain, build):
    """Use fresh, pinned sources and absolute compilers; never a PC CMake cache."""
    import build_win32_deps as deps
    import fetch_tools
    out=(build/'deps').resolve()
    deps.OUT=str(out);deps.TRIPLE='x86_64-w64-mingw32'
    deps.CC=(toolchain/'bin/x86_64-w64-mingw32-clang.exe').as_posix()
    fetch_tools.TOOLS=str(out/'tools')
    cmake=fetch_tools.ensure('cmake')
    ninja=fetch_tools.ensure('ninja')
    for name,options in [
        ('zlib',['-DZLIB_BUILD_SHARED=OFF','-DZLIB_BUILD_TESTING=OFF','-DBUILD_SHARED_LIBS=OFF']),
        ('libpng',['-DPNG_SHARED=OFF','-DPNG_STATIC=ON','-DPNG_TESTS=OFF','-DPNG_TOOLS=OFF','-DPNG_FRAMEWORK=OFF',f'-DZLIB_LIBRARY={out}/lib/libzs.a',f'-DZLIB_INCLUDE_DIR={out}/include']),
        ('freetype',['-DBUILD_SHARED_LIBS=OFF','-DFT_DISABLE_ZLIB=ON','-DFT_DISABLE_BZIP2=ON','-DFT_DISABLE_PNG=ON','-DFT_DISABLE_HARFBUZZ=ON','-DFT_DISABLE_BROTLI=ON']),
    ]:
        target=out/'build'/name
        subprocess.run([cmake,'-S',deps.fetch(name),'-B',str(target),'-G','Ninja',
          '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_SYSTEM_NAME=Windows',
          '-DCMAKE_C_COMPILER='+deps.CC,'-DCMAKE_RC_COMPILER='+(toolchain/'bin/llvm-windres.exe').as_posix(),
          '-DCMAKE_AR='+(toolchain/'bin/llvm-ar.exe').as_posix(),'-DCMAKE_RANLIB='+(toolchain/'bin/llvm-ranlib.exe').as_posix(),
          '-DCMAKE_MAKE_PROGRAM='+ninja.replace('\\','/'),'-DCMAKE_INSTALL_PREFIX='+out.as_posix(),
          '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',*options],check=True)
        subprocess.run([cmake,'--build',str(target),'--target','install'],check=True)
    # Upstream's source selector only needs this directory as its presence check.
    # The UWP backend links neither SDL nor a desktop window.
    (out/'sdl').mkdir(exist_ok=True)
    return str(out).replace('\\','/')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--toolchain',type=Path,required=True)
    p.add_argument('--build',default='tmp/uwp-native')
    p.add_argument('--headless',action='store_true')
    a=p.parse_args();os.chdir(ROOT)
    a.toolchain=a.toolchain.resolve()
    # LLVM's multicall linker chooses COFF command-line syntax by argv[0].
    # The llvm-mingw archive ships it as ld.lld.exe; MSBuild needs this name.
    lld_source=a.toolchain/'bin/ld.lld.exe';lld_link=a.toolchain/'bin/lld-link.exe'
    if not lld_link.exists() or lld_link.read_bytes()!=lld_source.read_bytes():
        shutil.copyfile(lld_source,lld_link)
    build=Path(a.build);build.mkdir(parents=True,exist_ok=True)
    git_count=int(os.environ.get('GIT_CONFIG_COUNT','0'))
    os.environ[f'GIT_CONFIG_KEY_{git_count}']='safe.directory'
    os.environ[f'GIT_CONFIG_VALUE_{git_count}']=ROOT.as_posix()
    os.environ['GIT_CONFIG_COUNT']=str(git_count+1)
    os.environ['PATH']=str(a.toolchain/'bin')+os.pathsep+os.environ['PATH']
    os.environ['MEMORIES_VERSION']='v0.3.0'
    sys.path.insert(0,str(ROOT/'tools/pc'))
    sys.argv=['build_game32.py','--target','windows-x64','--backend','uwp','--build',a.build,'--release']
    import build_config
    import build_win32_deps
    # This entry point has already selected its verified compiler. The PC
    # helper's presence check is for i686, which this x64-only toolchain omits.
    build_win32_deps.use_toolchain=lambda:None
    build_config.BACKENDS['uwp']=[]
    path=ROOT/'tools/pc/build_game32.py'
    text=path.read_text(encoding='utf-8').replace('if options.backend != "sdl":','if options.backend not in ("sdl", "uwp"):')
    ns={'__name__':'memories_uwp_native_builder','__file__':str(path)}
    exec(compile(text,str(path),'exec'),ns)
    ns['CC']=str(a.toolchain/'bin/x86_64-w64-mingw32-clang.exe')
    for name,tool in [('OBJCOPY','llvm-objcopy'),('NM','llvm-nm'),('READELF','llvm-readelf'),('OBJDUMP','llvm-objdump')]:
        ns[name]=str(a.toolchain/'bin'/f'{tool}.exe')
    deps=build_dependencies(a.toolchain,build)
    old_deps=ns['WIN32_DEPS'];ns['WIN32_DEPS']=deps
    ns['NATIVE_CFLAGS']=[f.replace(old_deps,deps) for f in ns['NATIVE_CFLAGS']]
    support=[(Path(deps)/'lib'/v).resolve() for v in ['libfreetype.a','libpng16.a','libzs.a']]
    support += [a.toolchain/'x86_64-w64-mingw32/lib'/v for v in ['libwinpthread.a','libmingwex.a']]
    support += [a.toolchain/'lib/clang/23/lib/windows/libclang_rt.builtins-x86_64.a']
    core=(build/'memories-uwp-core.lib').resolve()
    from xml.sax.saxutils import escape
    dependencies=';'.join(escape(str(p)) for p in [core,*support])+ ';WindowsApp.lib;ws2_32.lib;msvcrt.lib;msvcprt.lib;vcruntime.lib;ucrt.lib;oldnames.lib;legacy_stdio_wide_specifiers.lib;runtimeobject.lib;vccorlib.lib;%(AdditionalDependencies)'
    (build/'native-link.props').write_text(
        '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        ' <PropertyGroup><LinkToolPath>'+escape(str(a.toolchain/'bin'))+'\\</LinkToolPath><LinkToolExe>lld-link.exe</LinkToolExe></PropertyGroup>\n'
        ' <ItemDefinitionGroup><Link>\n'
        '  <AdditionalLibraryDirectories>$(LibraryPath);%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>\n'
        '  <OptimizeReferences>false</OptimizeReferences><EnableCOMDATFolding>false</EnableCOMDATFolding>\n'
        '  <AdditionalDependencies>'+dependencies+'</AdditionalDependencies>\n'
        '  <AdditionalOptions>/lldmingw /nodefaultlib /WHOLEARCHIVE:&quot;'+escape(str(core))+'&quot; /FORCE:MULTIPLE '
        '/alternatename:fstat64i32=_fstat64i32 /alternatename:wstat64i32=_wstat64i32 '
        '/alternatename:__imp__setjmp=__imp___intrinsic_setjmp %(AdditionalOptions)</AdditionalOptions>\n'
        ' </Link></ItemDefinitionGroup>\n</Project>\n',encoding='utf-8')
    ns['NATIVE'][:]=[s for s in ns['NATIVE'] if s not in EXCLUDE]
    # gl_picture.c/present_pass.c are not part of the software UWP renderer.
    # Its empty GL entry points are needed by shared libgpu.c.
    ns['NATIVE'].append('src/pc/render/gl_desktop_none.c')
    # Tentative PS1 globals defined in their own translation unit otherwise
    # become direct RIP-relative references to ABS symbols. Those references
    # break when the UWP loader rebases the image. Large-model instructions
    # carry the absolute guest address; lldmingw leaves ABS pins unrelocated.
    flags=['-DMEMORIES_UWP','-include','src/pc/platform/uwp_compat.h']
    ns['CFLAGS']+=flags+['-mcmodel=large'];ns['NATIVE_CFLAGS']+=flags
    game_flags_time=ns['flags_changed'](str(build/'uwp-game-flags.txt'),ns['CFLAGS'])
    patched=build/'uwp-sources';patched.mkdir(exist_ok=True)
    original_compile=ns['compile_unit']
    def compile_unit(job):
        source,obj,flags,renames,newest=job
        newest=max(newest,game_flags_time)
        if source in {'src/pc/compat/fs.c','src/pc/guest/image.c','src/pc/platform/platform_common.c'}:
            transformed=adapt(source,Path(source).read_text(encoding='utf-8'))
            target=patched/Path(source).name
            if not target.exists() or target.read_text(encoding='utf-8')!=transformed:
                target.write_text(transformed,encoding='utf-8',newline='\n')
            flags=flags+['-I'+str(Path(source).parent)]
            source=str(target).replace('\\','/')
        original_compile((source,obj,flags,renames,newest))
    ns['compile_unit']=compile_unit
    ns['exe_resources']=lambda *args:[]
    original_run=ns['run']
    def run(command):
        # The generated export address table intentionally declares every
        # symbol as a byte array; platform function prototypes must not be
        # injected into that one address-only translation unit.
        if str(build/'mod_exports.c').replace('\\','/') in command:
            command=list(command)
            for index in range(len(command)-2,-1,-1):
                if command[index:index+2]==['-include','src/pc/platform/uwp_compat.h']:
                    del command[index:index+2]
        if '-o' in command and command[command.index('-o')+1]==a.build+'/memories-pc.exe':
            objects=[str(v) for v in command if str(v).endswith(('.o','.obj'))]
            audit_guest_relocations(objects,build/'guest_symbols.s',build/'relocation-audit.json')
            library=build/'memories-uwp-core.lib'
            original_run([str(a.toolchain/'bin/llvm-ar.exe'),'rcs',str(library),*objects])
            (build/'core-objects.rsp').write_text(''.join('"'+str((ROOT/v).resolve())+'"\n' for v in objects),encoding='utf-8')
            (build/'link-info.json').write_text(json.dumps({
                'upstream':'v0.3.0','commit':'663626b1d38355dd11f5532894a2b94169e7f349',
                'library':str(library.resolve()),'objects':len(objects),
                'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest(),
                'flags':['/BASE:0x40000000','/DYNAMICBASE:NO','/HIGHENTROPYVA:NO','/FORCE:MULTIPLE'],
                'host_image':{'preferred_base':'0x40000000','exact_preferred_base_required':False,
                              'minimum_base':'0x20000000','image_and_mod_arena_end_maximum':'0x80000000',
                              'mod_arena_size':'0x08000000','base_relocations_required':True,
                              'game_code_model':'large','native_code_model':'small',
                              'relocation_audit':'relocation-audit.json'},
                'libraries':['libfreetype.a','libpng16.a','libzs.a','libwinpthread.a','libmingwex.a','WindowsApp.lib'],
                'bridge':'src/pc/platform/uwp_bridge.h'},indent=2),encoding='utf-8')
            (build/'checkout.txt').write_text(os.path.realpath(ROOT)+'\n',encoding='utf-8')
            print(f'Native UWP core: {library} ({len(objects)} objects)',flush=True)
            os.environ['MEMORIES_MOD_CC']=ns['CC']
            ns['build_mods'](a.build,release=True,target='x86_64-windows')
            ns['copy_languages'](a.build,release=True)
            if a.headless:
                out=str(build/'memories-uwp-headless.exe')
                original_run([ns['CC'],*ns['NATIVE_CFLAGS'],'-c','tools/uwp/headless_host.c','-o',str(build/'headless-host.o')])
                original_run([ns['CC'],'-o',out,'-Wl,--image-base=0x40000000','-Wl,--disable-dynamicbase',
                    '-Wl,--disable-high-entropy-va','-Wl,--allow-multiple-definition',*objects,str(build/'headless-host.o'),
                    ns['WIN32_DEPS']+'/lib/libfreetype.a',ns['WIN32_DEPS']+'/lib/libpng16.a',ns['WIN32_DEPS']+'/lib/libzs.a',
                    '-lwindowsapp','-lws2_32','-static','-lpthread'])
                print('Validation harness: '+out,flush=True)
            raise Archived()
        return original_run(command)
    ns['run']=run
    try:ns['main']()
    except Archived:pass
if __name__=='__main__':main()
