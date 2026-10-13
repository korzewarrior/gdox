#!/usr/bin/env python3
"""Stage the pinned Linux Dolphin build inside its compatibility container."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
if a.output.exists():
    raise SystemExit('Output must be a new directory')
root = a.output
(root / 'bin').mkdir(parents=True)
(root / 'lib').mkdir()
(root / 'licenses').mkdir()
shutil.copy2(a.build / 'Binaries/dolphin-emu', root / 'bin/dolphin-emu')
shutil.copy2('/usr/bin/nbdfuse', root / 'bin/nbdfuse')
shutil.copytree(a.source / 'Data/Sys', root / 'bin/Sys')
shutil.copy2(a.source / 'COPYING', root / 'licenses/Dolphin-COPYING')
plugins = Path('/usr/lib/x86_64-linux-gnu/qt6/plugins')
for category in ('platforms', 'platforminputcontexts', 'imageformats',
                 'xcbglintegrations', 'wayland-decoration-client',
                 'wayland-graphics-integration-client', 'wayland-shell-integration'):
    if (plugins / category).is_dir():
        shutil.copytree(plugins / category, root / 'plugins' / category)
(root / 'bin/qt.conf').write_text('[Paths]\nPlugins=../plugins\n')
shutil.copytree('/etc/fonts', root / 'fonts', symlinks=False)
fonts_config = root / 'fonts/fonts.conf'
fonts_config.write_text(fonts_config.read_text().replace(
    '<include ignore_missing="yes">conf.d</include>',
    '<include ignore_missing="yes" prefix="relative">conf.d</include>'))
# glibc and the host graphics driver stack remain platform prerequisites.
host_libraries = {'libc.so.6','libm.so.6','libpthread.so.0','libdl.so.2','librt.so.1',
                  'ld-linux-x86-64.so.2','libresolv.so.2','libstdc++.so.6','libgcc_s.so.1'}
queue = [root / 'bin/dolphin-emu', root / 'bin/nbdfuse', *(root / 'plugins').rglob('*.so')]
seen = set()
packages = set()
def add_package(path):
    owner = subprocess.check_output(['dpkg-query', '-S', str(path)], text=True).split(': /', 1)[0]
    packages.add(owner)
add_package('/usr/bin/nbdfuse')
add_package('/etc/fonts/fonts.conf')
for plugin in (root / 'plugins').rglob('*.so'):
    add_package(plugins / plugin.relative_to(root / 'plugins'))
for elf in queue:
    output = subprocess.check_output(['ldd', str(elf)], text=True)
    if 'not found' in output:
        raise SystemExit(f'Unresolved dependency of {elf}: {output}')
    for name, path in re.findall(r'^\s*(\S+) => (/\S+) ', output, re.M):
        if name in host_libraries or name in seen:
            continue
        seen.add(name)
        origin = Path(path).resolve()
        destination = root / 'lib' / name
        shutil.copy2(origin, destination)
        queue.append(destination)
        try:
            owner = subprocess.check_output(['dpkg-query','-S',str(origin)], text=True).split(': /',1)[0]
        except subprocess.CalledProcessError:
            owner = subprocess.check_output(['dpkg-query','-S',str(Path(path))], text=True).split(': /',1)[0]
        packages.add(owner)
# Use local rpaths so the bundle does not depend on a global library-path edit.
for elf in [root / 'bin/dolphin-emu', root / 'bin/nbdfuse', *(root / 'lib').iterdir()]:
    rpath = '$ORIGIN/../lib' if elf.parent.name == 'bin' else '$ORIGIN'
    subprocess.run(['patchelf','--set-rpath',rpath,str(elf)],check=True)
for elf in (root / 'plugins').rglob('*.so'):
    subprocess.run(['patchelf','--set-rpath','$ORIGIN/../../lib',str(elf)],check=True)
(root / 'dolphin').write_text('''#!/bin/sh
set -eu
runtime_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# Qt must use the plugins matched to this bundled Qt build.
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
export FONTCONFIG_FILE="$runtime_dir/fonts/fonts.conf"
export FONTCONFIG_PATH="$runtime_dir/fonts"
exec "$runtime_dir/bin/dolphin-emu" "$@"
''')
(root / 'dolphin').chmod(0o755)
metadata=[]
for package in sorted(packages):
    row=subprocess.check_output(['dpkg-query','-W','-f=${binary:Package}\t${Version}\t${source:Package}\t${source:Version}',package],text=True).split('\t')
    metadata.append(dict(zip(('binary','version','source','source_version'),row)))
    copyright=Path('/usr/share/doc') / package.split(':')[0] / 'copyright'
    if not copyright.is_file(): raise SystemExit(f'Missing notice: {package}')
    shutil.copy2(copyright, root/'licenses'/f'{package.replace(":","-")}.copyright')
(root/'dependencies.json').write_text(json.dumps(metadata,indent=2)+'\n')
files={str(f.relative_to(root)):{'size':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in sorted(root.rglob('*')) if f.is_file()}
(root/'FILES.json').write_text(json.dumps(files,indent=2)+'\n')
print(json.dumps({'files':len(files),'payload':files['bin/dolphin-emu']}))
