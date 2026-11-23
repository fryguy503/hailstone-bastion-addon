#!/usr/bin/env python3
"""Build an addon package using Hailstone's official packaging tool."""
import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--client', type=pathlib.Path, required=True, help='Hailstone checkout with tools/make_addon.py')
p.add_argument('--build', type=pathlib.Path, default=root / 'build')
p.add_argument('--platform', required=True)
p.add_argument('--version', default='0.1.0-preview.9')
p.add_argument('--out', type=pathlib.Path, default=root / 'release')
a = p.parse_args()
build = a.build.resolve()
tool = a.client.resolve() / 'tools' / 'make_addon.py'
suffix = '.dll' if a.platform.startswith('windows-') else '.dylib' if a.platform.startswith('macos-') else '.so'
library = build / ('bastion' + suffix)
if not tool.is_file() or not library.is_file():
    p.error('Build the plugin and provide a Hailstone checkout with tools/make_addon.py')
caps = 'read,commands,target,actions,move,chat,events,ui,packets,extend,login'
with tempfile.TemporaryDirectory(prefix='bastion-package-', dir=build) as temporary:
    stage = pathlib.Path(temporary).resolve()
    if not stage.is_relative_to(build):
        raise RuntimeError('Package stage escaped its build directory')
    shutil.copy2(library, stage / library.name)
    shutil.copytree(root / 'ui', stage / 'ui')
    shutil.copytree(root / 'docs', stage / 'docs')
    for filename in ('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD_PARTY.md'):
        shutil.copy2(root / filename, stage / filename)
    shutil.copy2(root / 'sdk' / 'LICENSE', stage / 'HAILSTONE-SDK-LICENSE.txt')
    (stage / 'sdk').mkdir()
    shutil.copy2(root / 'sdk' / 'README.md', stage / 'sdk' / 'README.md')
    shutil.copy2(root / 'sdk' / 'LICENSE', stage / 'sdk' / 'LICENSE')
    subprocess.run([sys.executable, str(tool), '--name', 'bastion', '--kind', 'plugin',
        '--version', a.version, '--min-client', '0.7.1-beta', '--author', 'Fryguy',
        '--description', 'Bastion server tools, settings, raid bars and encounter displays.',
        '--caps', caps, '--platform', f'{a.platform}={stage}', '--out', str(a.out.resolve())], check=True)
shutil.copy2(a.out.resolve() / 'hailstone-addon.txt', root / 'hailstone-addon.txt')
print('Local preview package created; no release has been published.')
