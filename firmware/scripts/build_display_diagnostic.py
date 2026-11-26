#!/usr/bin/env python3
"""Build the static LCD diagnostic in isolation; never flash a device."""
from pathlib import Path
import shutil
import build

target=build.ROOT/'.local/firmware-diagnostic/DisplayDiagnostic'
target.mkdir(parents=True,exist_ok=True)
shutil.copy2(build.ROOT/'firmware/DisplayDiagnostic/DisplayDiagnostic.ino',target/'DisplayDiagnostic.ino')
source=build.ROOT/'firmware/CasaMonitor/src'
shutil.copytree(source,target/'src',dirs_exist_ok=True)
build.run([build.CLI,'compile','--config-file',build.CONFIG,'--fqbn',build.FQBN,
           '--build-path',build.ROOT/'.local/firmware-diagnostic-build',
           '--output-dir',build.ROOT/'.local/firmware-diagnostic-output',target])
