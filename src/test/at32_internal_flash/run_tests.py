#!/usr/bin/env python3
"""Test the production sources with mapped Flash and injected faults.

On Apple Silicon the driver harness uses x86_64/Rosetta because native arm64
processes reserve the MCU address range inside the mandatory 4 GiB PAGEZERO.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="inav-internal-flash-") as temporary:
    for source, defines in [("tests.c", ["-DUSE_FLASH_AT32_INTERNAL"]), ("external_flashfs.c", [])]:
        binary = Path(temporary) / Path(source).stem
        command = [os.environ.get("CC", "cc"), "-std=c99", "-D_DARWIN_C_SOURCE", "-D_DEFAULT_SOURCE",
                   "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                   "-I" + str(here / "stubs"), "-I" + str(here.parents[1] / "main"),
                   str(here / source), "-o", str(binary)] + defines
        if sys.platform == "darwin" and defines:
            command += ["-arch", "x86_64", "-Wl,-pagezero_size,0x10000", "-Wl,-no_pie"]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)

    for feature in ["MSP_FIRMWARE_UPDATE", "FIRMWARE_SIZE=1024", "CONFIG_IN_EXTERNAL_FLASH", "USE_FLASH_M25P16"]:
        result = subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-fsyntax-only",
                                 "-DUSE_FLASH_AT32_INTERNAL", "-D" + feature,
                                 "-I" + str(here / "stubs"), "-I" + str(here.parents[1] / "main"),
                                 str(here.parents[1] / "main/drivers/flash_at32_internal.c")],
                                capture_output=True, text=True)
        assert result.returncode != 0 and ("cannot share" in result.stderr or "Select only" in result.stderr), result.stderr
    print("PASS: incompatible firmware/configuration/external-driver combinations are rejected")
