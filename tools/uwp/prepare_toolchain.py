"""Extract a clean, SHA256-verified x64 toolchain into a NEW directory."""
import argparse,hashlib,shutil,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/pc'))
import fetch_tools
def main():
    p=argparse.ArgumentParser();p.add_argument('--archive',type=Path,required=True);p.add_argument('--destination',type=Path,required=True);p.add_argument('--resume',action='store_true');a=p.parse_args()
    h=hashlib.sha256()
    with a.archive.open('rb') as f:
        for block in iter(lambda:f.read(1<<20),b''):h.update(block)
    expected=fetch_tools.RELEASES['llvm-mingw'][1]
    if h.hexdigest()!=expected:raise RuntimeError('Wrong toolchain SHA256: '+h.hexdigest())
    if a.destination.exists() and any(a.destination.iterdir()) and not a.resume:raise RuntimeError('Destination must be new or empty')
    a.destination.mkdir(parents=True,exist_ok=True);count=total=0
    with zipfile.ZipFile(a.archive) as z:
        for info in z.infolist():
            parts=info.filename.split('/');rel='/'.join(parts[1:])
            if '..' in parts or info.is_dir():continue
            if not (rel.startswith('bin/') or rel.startswith('include/') or rel.startswith('x86_64-w64-mingw32/') or rel.startswith('lib/clang/')):continue
            if rel.startswith('bin/') and any(s in rel for s in ('aarch64-','arm64ec-','armv7-','i686-')) and not rel.endswith('/i686-w64-mingw32-clang.exe'):continue
            if rel.startswith('lib/clang/') and '/lib/' in rel and not rel.endswith('x86_64.a'):continue
            dest=a.destination/rel;dest.parent.mkdir(parents=True,exist_ok=True)
            with z.open(info) as src,dest.open('wb') as dst:shutil.copyfileobj(src,dst)
            count+=1;total+=info.file_size
    print(f'Clean verified compiler: {a.destination}; {count} files, {total/1048576:.1f} MiB; SHA256 {expected}',flush=True)
if __name__=='__main__':main()
