"""Verify the native UWP payload before signing/deployment (no game data required)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from xml.etree import ElementTree as ET
from zipfile import ZipFile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    with ZipFile(args.package) as archive:
        names = set(archive.namelist())
        manifest = ET.fromstring(archive.read('AppxManifest.xml'))
        ns = {'p': 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'}
        identity = manifest.find('p:Identity', ns)
        app = manifest.find('p:Applications/p:Application', ns)
        executable = app.attrib['Executable']
        assert executable == 'YuGiOhForbiddenMemoriesUWP.exe', executable
        assert [name for name in names if name.lower().endswith('.exe')] == [executable]
        assert app.attrib['EntryPoint'] == 'YuGiOhForbiddenMemoriesUWP.App'
        for required in ('YuGiOhForbiddenMemoriesUWP.winmd', 'resources.pri', 'Assets/YuGiOhMenuTheme.mp3'):
            assert required in names, required
        mods = ['3d-monsters', 'ai-hard-mode', 'drop-missing-cards', 'hand-camera', 'yamyi-mods']
        for mod in mods:
            assert f'mods/{mod}/mod.json' in names, mod
        for mod in set(mods) - {'drop-missing-cards'}:
            assert f'mods/{mod}/{mod}.x86_64-windows.o' in names, mod
        assert len([name for name in names if name.startswith('languages/') and name.endswith('.txt')]) == 5
        image = archive.read(executable)
        pe = struct.unpack_from('<I', image, 0x3c)[0]
        assert image[pe:pe+4] == b'PE\0\0'
        assert struct.unpack_from('<H', image, pe+4)[0] == 0x8664
        file_flags = struct.unpack_from('<H', image, pe+22)[0]
        opt = pe + 24
        assert struct.unpack_from('<H', image, opt)[0] == 0x20b
        image_base = struct.unpack_from('<Q', image, opt+24)[0]
        image_size = struct.unpack_from('<I', image, opt+56)[0]
        dll_flags = struct.unpack_from('<H', image, opt+70)[0]
        reloc_rva, reloc_size = struct.unpack_from('<II', image, opt+112+5*8)
        assert image_base == 0x40000000
        assert image_base + image_size < 0x80000000
        assert dll_flags & 0x1000 and dll_flags & 0x100, 'AppContainer and NX required'
        assert not dll_flags & 0x20, 'High Entropy can place G32 callbacks above 4 GiB'
        assert not file_flags & 1 and reloc_rva and reloc_size, 'UWP requires a relocatable image'
        report = {'passed': True, 'package': str(args.package.resolve()),
                  'package_sha256': hashlib.sha256(args.package.read_bytes()).hexdigest(),
                  'version': identity.attrib['Version'], 'files': len(names),
                  'executable_sha256': hashlib.sha256(image).hexdigest(),
                  'image_base': f'0x{image_base:08X}', 'image_size': image_size,
                  'appcontainer': True, 'nx': True, 'base_relocations': reloc_size,
                  'bundled_mods': mods, 'menu_theme_bytes': archive.getinfo('Assets/YuGiOhMenuTheme.mp3').file_size}
    result = json.dumps(report, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(result, encoding='utf-8')
    print(result)


if __name__ == '__main__':
    main()
