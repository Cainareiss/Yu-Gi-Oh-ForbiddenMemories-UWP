"""Restore missing x64 compiler support libraries from upstream's pinned archive.

Existing files are preserved. The archive digest is the one pinned by
tools/pc/fetch_tools.py; only lib/clang and x86_64 runtime .a files are read.
"""
import argparse, hashlib, os, shutil, sys, urllib.request, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/pc'))
import fetch_tools
def main():
    p=argparse.ArgumentParser();p.add_argument('--toolchain',type=Path,required=True);a=p.parse_args()
    url,digest,_=fetch_tools.RELEASES['llvm-mingw']
    archive=a.toolchain.parent/'downloads'/url.rsplit('/',1)[1]
    archive.parent.mkdir(parents=True,exist_ok=True)
    if not archive.exists():
        print('Downloading',url,flush=True)
        with urllib.request.urlopen(url,timeout=300) as src,open(str(archive)+'.part','wb') as dst:shutil.copyfileobj(src,dst)
        os.replace(str(archive)+'.part',archive)
    h=hashlib.sha256()
    with archive.open('rb') as f:
        for block in iter(lambda:f.read(1<<20),b''):h.update(block)
    if h.hexdigest()!=digest:raise RuntimeError('Toolchain SHA256 differs from upstream pin: '+h.hexdigest())
    count=total=0
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            parts=info.filename.split('/');rel='/'.join(parts[1:])
            if '..' in parts or info.is_dir():continue
            if not (rel.startswith('lib/clang/') or (rel.startswith('x86_64-w64-mingw32/lib/') and rel.endswith('.a'))):continue
            dest=a.toolchain/rel
            if dest.exists():continue
            dest.parent.mkdir(parents=True,exist_ok=True)
            with z.open(info) as src,dest.open('wb') as dst:shutil.copyfileobj(src,dst)
            count+=1;total+=info.file_size
    print(f'Restored {count} missing runtime archives ({total/1048576:.1f} MiB); SHA256 {digest}',flush=True)
if __name__=='__main__':main()
