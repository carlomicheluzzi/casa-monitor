#!/usr/bin/env python3
"""Compile the actual portable model and LVGL UI; render at the board's 800x480."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import subprocess
import sys
from test_release import fixtures

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware"
SRC = FW / "CasaMonitor/src"
LVGL = ROOT / ".local/firmware-deps/lvgl-8.4.0"
CJSON = ROOT / ".local/firmware-deps/cJSON-1.7.19"
BUILD = ROOT / ".local/firmware-host"
BUILD.mkdir(parents=True, exist_ok=True)
if not LVGL.is_dir() or not CJSON.is_dir():
    sys.exit("Prima eseguire: python3 firmware/scripts/build.py --prepare-only")

def run(args):
    result = subprocess.run([str(v) for v in args], cwd=ROOT)
    if result.returncode:
        sys.exit(f"{args[0]}: uscita {result.returncode}")

cpp = os.environ.get("CXX", "c++")
cc = os.environ.get("CC", "cc")
common = ["-DLV_CONF_INCLUDE_SIMPLE", "-I", FW, "-I", LVGL, "-I", LVGL / "src", "-I", SRC,"-I",CJSON]
run([cpp,"-std=c++17","-Wall","-Wextra","-Werror","-I",SRC,
     FW/"tests/test_model.cpp",SRC/"wifi_model.cpp","-o",BUILD/"test_model"])
run([BUILD/"test_model"])
run([cc,"-std=c99","-O0","-I",CJSON,"-c",CJSON/"cJSON.c","-o",BUILD/"cJSON.o"])
run([cpp,"-std=c++17","-Wall","-Wextra","-Werror",*common,
     FW/"tests/test_energy.cpp",SRC/"energy_model.cpp",BUILD/"cJSON.o","-o",BUILD/"test_energy"])
run([BUILD/"test_energy"])

fixtures(BUILD)
run([cpp,"-std=c++17","-Wall","-Wextra","-Werror",*common,"-I","/opt/homebrew/opt/openssl@3/include",
     FW/"tests/test_device.cpp",SRC/"usb_update_model.cpp",SRC/"device_model.cpp",SRC/"crypto_util.cpp",SRC/"energy_model.cpp",BUILD/"cJSON.o",
     "-L","/opt/homebrew/opt/openssl@3/lib","-lcrypto","-o",BUILD/"test_device"])
run([BUILD/"test_device",BUILD])

def compile_c(source):
    # Use unique relative paths, keeping the cache strictly inside .local.
    relative = source.relative_to(ROOT)
    target = BUILD / (str(relative).replace("/", "_") + ".o")
    if not target.exists() or max(source.stat().st_mtime,(FW/"lv_conf.h").stat().st_mtime)>target.stat().st_mtime:
        run([cc,"-std=c99","-O0","-w",*common,"-c",source,"-o",target])
    return target

sources = sorted((LVGL/"src").rglob("*.c")) + sorted((SRC/"fonts").glob("*.c"))
with ThreadPoolExecutor(max_workers=min(os.cpu_count() or 2,8)) as pool:
    objects = list(pool.map(compile_c,sources))
run([cpp,"-std=c++17","-Wall","-Wextra","-Werror",*common,
     FW/"tests/test_ui.cpp",SRC/"device_ui.cpp",SRC/"device_model.cpp",SRC/"network_ui.cpp",SRC/"wifi_model.cpp",SRC/"energy_ui.cpp",SRC/"energy_model.cpp",BUILD/"cJSON.o",*objects,"-o",BUILD/"test_ui"])
run([BUILD/"test_ui",BUILD])
try:
    from PIL import Image
except ImportError:
    print("Render PPM disponibili in",BUILD)
else:
    for source in BUILD.glob("*.ppm"):
        Image.open(source).save(source.with_suffix(".png"))
    print("Anteprime PNG:",BUILD)
