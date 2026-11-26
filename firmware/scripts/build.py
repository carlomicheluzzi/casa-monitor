#!/usr/bin/env python3
"""Reproducible local build; downloads stay in .local and no device is flashed."""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / ".local/firmware-tools"
DEPS = ROOT / ".local/firmware-deps"
CLI = TOOLS / "arduino-cli"
CONFIG = TOOLS / "arduino-cli.yaml"
CORE = "3.3.11"
FQBN = "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadMode=default,UploadSpeed=921600,LoopCore=1,EventsCore=1"

def run(args):
    result = subprocess.run([str(v) for v in args], cwd=ROOT)
    if result.returncode:
        sys.exit(f"{args[0]}: uscita {result.returncode}")

def archive(url, dest, digest):
    if not dest.exists():
        temporary = dest.with_suffix(".download")
        run(["curl", "--fail", "--location", "--retry", "2", "--max-time", "180", url, "--output", temporary])
        temporary.replace(dest)
    if hashlib.sha256(dest.read_bytes()).hexdigest() != digest:
        sys.exit(f"SHA-256 non valido: {dest}. Rimuovere il download e riprovare.")

def extract(archive_path, directory):
    with tarfile.open(archive_path) as tar:
        # Python 3.10 compatibility: official archives are hash-pinned above;
        # additionally reject paths escaping the extraction directory.
        for member in tar.getmembers():
            if not (directory/member.name).resolve().is_relative_to(directory.resolve()):
                sys.exit("Percorso non valido nell’archivio.")
        if sys.version_info >= (3,12):
            tar.extractall(directory, filter="data")
        else:
            tar.extractall(directory)

def prepare(host_only=False):
    TOOLS.mkdir(parents=True, exist_ok=True)
    DEPS.mkdir(parents=True, exist_ok=True)
    if not (DEPS/"lvgl-8.4.0").is_dir():
        archive("https://github.com/lvgl/lvgl/archive/refs/tags/v8.4.0.tar.gz", DEPS/"lvgl.tar.gz",
                "3b454d602e1667b9bd2c434f5e8156753d92cf3e71fc40a0cb0d99fca9a605be")
        extract(DEPS/"lvgl.tar.gz",DEPS)
    if not (DEPS/"cJSON-1.7.19").is_dir():
        archive("https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz",DEPS/"cjson.tar.gz",
                "7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562")
        extract(DEPS/"cjson.tar.gz",DEPS)
    shutil.copy2(ROOT/"firmware/lv_conf.h", DEPS/"lv_conf.h")
    if host_only:
        return
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        sys.exit("La preparazione automatica del toolchain è per macOS Apple Silicon. Per i test host usare --host-only.")
    if not CLI.exists():
        archive("https://github.com/arduino/arduino-cli/releases/download/v1.5.1/arduino-cli_1.5.1_macOS_ARM64.tar.gz",
                TOOLS/"arduino-cli.tar.gz","cb952e8c1621c95ef5f1d17831c945e3d0ec5973f89c557a7ec8feb9c4f7d4c9")
        extract(TOOLS/"arduino-cli.tar.gz",TOOLS)
    paths = {key:str(TOOLS/name) for key,name in [("data","data"),("downloads","downloads"),("user","user")]}
    CONFIG.write_text("directories:\n"+"".join(f"  {k}: {json.dumps(v)}\n" for k,v in paths.items())+
                      "board_manager:\n  additional_urls:\n    - https://espressif.github.io/arduino-esp32/package_esp32_index.json\n")
    libraries = TOOLS/"user/libraries"
    libraries.mkdir(parents=True, exist_ok=True)
    if not (libraries/"lvgl").exists():
        (libraries/"lvgl").symlink_to(DEPS/"lvgl-8.4.0",target_is_directory=True)
    shutil.copy2(ROOT/"firmware/lv_conf.h",libraries/"lv_conf.h")
    if not (TOOLS/f"data/packages/esp32/hardware/esp32/{CORE}/platform.txt").is_file():
        run([CLI,"core","update-index","--config-file",CONFIG])
        run([CLI,"core","install",f"esp32:esp32@{CORE}","--config-file",CONFIG])

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--host-only", action="store_true", help="Scarica solo LVGL per i test desktop")
    args=parser.parse_args()
    metadata=json.loads((ROOT/"firmware/version.json").read_text())
    if not isinstance(metadata["version_code"],int) or not 0<metadata["version_code"]<=4294967295 or metadata["board"]!="waveshare-esp32-s3-touch-lcd-4.3c":
        sys.exit("Metadati versione non validi")
    marker=f'CASA_RELEASE:{metadata["board"]}:{metadata["version_code"]}:{metadata["version"]}'
    header=("#pragma once\nnamespace casa {\n"+
        f'constexpr char FIRMWARE_VERSION[]={json.dumps(metadata["version"])};\n'+
        f'constexpr unsigned FIRMWARE_CODE={metadata["version_code"]};\n'+
        f'constexpr char BOARD_ID[]={json.dumps(metadata["board"])};\n'+
        f'constexpr char FIRMWARE_MARKER[]={json.dumps(marker)};\n'+'}\n')
    version_header=ROOT/"firmware/CasaMonitor/src/version.h"
    if not version_header.exists() or version_header.read_text()!=header:version_header.write_text(header)
    if not (ROOT/"firmware/CasaMonitor/src/update_key.h").is_file():
        sys.exit("Preparare prima la chiave di rilascio con firmware/scripts/release.py --init-key")
    prepare(args.host_only)
    if not (args.prepare_only or args.host_only):
        run([CLI,"compile","--config-file",CONFIG,"--fqbn",FQBN,
             "--build-path",ROOT/".local/firmware-build","--output-dir",ROOT/".local/firmware-output",
             ROOT/"firmware/CasaMonitor"])
        print("Firmware compilato in .local/firmware-output. Nessun dispositivo è stato programmato.")
