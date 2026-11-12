#!/usr/bin/env python3
"""Package a trusted project destpkg archive as a runtime IPK (host ar required)."""
import argparse
import pathlib
import re
import shutil
import subprocess
import tarfile
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('package', choices=['expat', 'dbus'])
p.add_argument('archive', type=pathlib.Path)
p.add_argument('output', type=pathlib.Path)
p.add_argument('--architecture', required=True)
p.add_argument('--release', default='1')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    stage = root / 'stage'
    with tarfile.open(a.archive) as src:
        src.extractall(stage, filter='data')
    payload, = stage.iterdir()
    pc = payload / 'lib/pkgconfig' / ('dbus-1.pc' if a.package == 'dbus' else 'expat.pc')
    version = re.search(r'^Version:\s*(\S+)', pc.read_text(), re.M)[1]
    # Development files remain available in the original destpkg archive.
    for path in list(payload.rglob('*')):
        if path.is_file() and path.suffix in ('.a', '.la'):
            path.unlink()
    for relative in ('include', 'lib/pkgconfig', 'lib/cmake', 'lib/dbus-1.0', 'share/doc', 'share/man'):
        shutil.rmtree(payload / relative, ignore_errors=True)
    control = root / 'control'
    control.mkdir()
    metadata = (f'Package: {a.package}\nVersion: {version}-{a.release}\n'
                f'Architecture: {a.architecture}\nMaintainer: algae-bp\n'
                f'Description: {a.package} runtime for algae-bp\n')
    if a.package == 'dbus':
        metadata += 'Depends: expat\n'
    (control / 'control').write_text(metadata)
    configs = sorted('/' + str(f.relative_to(payload)) for f in payload.rglob('*')
                     if f.is_file() and (str(f.relative_to(payload)).startswith('etc/')
                                        or f.name in ('system.conf', 'session.conf')))
    if configs:
        (control / 'conffiles').write_text('\n'.join(configs) + '\n')
    for name, directory in [('control', control), ('data', payload)]:
        with tarfile.open(root / f'{name}.tar.gz', 'w:gz') as dst:
            for child in sorted(directory.iterdir()):
                dst.add(child, arcname='./' + child.name)
    (root / 'debian-binary').write_text('2.0\n')
    output = (a.output / f'{a.package}_{version}-{a.release}_{a.architecture}.ipk').resolve()
    output.unlink(missing_ok=True)
    subprocess.run(['ar', 'qc', str(output), 'debian-binary', 'control.tar.gz', 'data.tar.gz'], cwd=root, check=True)
    print(output)
