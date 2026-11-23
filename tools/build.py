#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build pinned Zephyr sources; stage the app to support checkouts with spaces."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

project = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--workspace',type=Path,default=project.parent)
p.add_argument('--zephyr-base',type=Path)
p.add_argument('--build-dir',type=Path)
p.add_argument('--sdk',type=Path)
p.add_argument('--round-display', action='store_true', help='Build the optional GC9A01 dashboard')
p.add_argument('--button-gif', type=Path, help='Play a local animated GIF on BTN0 (requires --round-display)')
a = p.parse_args()
if a.button_gif and not a.round_display:
    p.error('--button-gif requires --round-display')
workspace = a.workspace.resolve()
zephyr = (a.zephyr_base or workspace/'zephyr').resolve()
variant = ''
if a.round_display:
    variant = '-display-animation' if a.button_gif else '-display'
build = (a.build_dir or workspace/('build' + variant)).resolve()
commit = subprocess.check_output(['git','rev-parse','HEAD'],cwd=zephyr,text=True).strip()
if commit != '41d10b59730406bbab4ca9f1fbbe9e96aafb4966':
    p.error('Zephyr revision differs from west.yml; run west update')
for patch in sorted((project/'patches').glob('*.patch')):
    applied = subprocess.run(['git','apply','--reverse','--check',str(patch)],cwd=zephyr,capture_output=True).returncode==0
    if not applied:
        subprocess.run(['git','apply','--check',str(patch)],cwd=zephyr,check=True)
        subprocess.run(['git','apply',str(patch)],cwd=zephyr,check=True)
stage = build.parent/(build.name + '-opensidewalk-app-stage')
stage.mkdir(parents=True,exist_ok=True)
names = ['CMakeLists.txt', 'prj.conf', 'app.overlay', 'src']
if a.round_display:
    names.extend(['round-display.conf', 'round-display.overlay'])
for name in names:
    source,dest=project/name,stage/name
    if source.is_dir():
        shutil.copytree(source,dest,dirs_exist_ok=True)
    else:
        shutil.copy2(source,dest)
display_conf = stage/'round-display.conf' if a.round_display else ''
display_overlay = stage/'round-display.overlay' if a.round_display else ''
button_animation = ''
if a.button_gif:
    from pack_button_gif import pack_button_gif

    button_animation = stage/'button-animation.rgb4'
    try:
        pack_button_gif(a.button_gif, button_animation)
    except (OSError, ValueError) as error:
        p.error(f'could not pack {a.button_gif}: {error}')

env = dict(os.environ)
env['ZEPHYR_BASE']=str(zephyr)
if a.sdk:
    env['ZEPHYR_SDK_INSTALL_DIR']=str(a.sdk.resolve())
    env['ZEPHYR_TOOLCHAIN_VARIANT']='zephyr'
subprocess.run(['west','build','-p','auto','-b','kg100s_rb4332a','-d',str(build),str(stage),
                '--', '-DPython3_EXECUTABLE='+sys.executable,
                f'-DEXTRA_CONF_FILE={display_conf}', f'-DEXTRA_DTC_OVERLAY_FILE={display_overlay}',
                f'-DOPENSIDEWALK_BUTTON_ANIMATION={button_animation}'],
               cwd=workspace,env=env,check=True)
output = build if variant else project/'firmware'
output.mkdir(exist_ok=True)
for ext in ('hex','bin'):
    target = output/('opensidewalk-us915' + variant + '.' + ext)
    shutil.copy2(build/'zephyr'/('zephyr.'+ext),target)
    target.chmod(0o644)
print('Firmware:',output)
