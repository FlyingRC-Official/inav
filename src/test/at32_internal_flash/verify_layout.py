#!/usr/bin/env python3
"""Check the actual firmware image and exercise both linker layouts/overflow guards."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, default=Path("build"))
parser.add_argument("--tool-prefix", default="arm-none-eabi-")
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
elf = args.build / "bin/FLYINGRCF435WINGMINI_CMU7_FLASH.elf"
hexfile = args.build / "inav_9.1.1_FLYINGRCF435WINGMINI_CMU7_FLASH.hex"

def execute(tool, *options, **kwargs):
    return subprocess.run([args.tool_prefix + tool, *map(str, options)], capture_output=True, text=True, **kwargs)

symbols = execute("nm", "-n", elf, check=True).stdout
assert re.search(r"^08200000 \w __flashlog_start$", symbols, re.M)
assert re.search(r"^083f0000 \w __flashlog_end$", symbols, re.M)
sections = execute("objdump", "-h", elf, check=True).stdout.splitlines()
for index, line in enumerate(sections):
    match = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
    if match and "LOAD" in sections[index + 1]:
        size, load = int(match[2], 16), int(match[4], 16)
        assert 0x08000000 <= load and load + size <= 0x08200000, line

base = 0
end = 0
for line in hexfile.read_text().splitlines():
    raw = bytes.fromhex(line[1:])
    assert sum(raw) % 256 == 0
    count, kind = raw[0], raw[3]
    if kind == 4:
        base = int.from_bytes(raw[4:6], "big") << 16
    elif kind == 0:
        address = base + int.from_bytes(raw[1:3], "big")
        assert 0x08000000 <= address and address + count <= 0x08200000, line
        end = max(end, address + count)
print(f"PASS: actual ELF load sections / HEX end 0x{end:08X}; no firmware payload in Bank 2")

linkdir = root / "src/main/target/link"
# Exercise the production linker scripts directly.
with tempfile.TemporaryDirectory(prefix="inav-flash-layout-") as temporary:
    work = Path(temporary)
    for extended in (False, True):
        source = work / "stub.s"
        source.write_text(".syntax unified\n.thumb\n.section .text\n.global Reset_Handler\n.thumb_func\nReset_Handler: bx lr\n")
        result = execute("gcc", "-mcpu=cortex-m4", "-mthumb", "-nostdlib", source,
                         "-Wl,-L" + str(linkdir), "-T" + str(linkdir / "at32_flash_f43xM_blackbox.ld"),
                         *( ["-Wl,--defsym,USE_CUSTOM_DEFAULTS_EXTENDED=1"] if extended else []),
                         "-o", work / "stub.elf")
        assert result.returncode == 0, result.stderr
        actual = execute("nm", "-n", work / "stub.elf", check=True).stdout
        assert re.search(r"^08200000 \w __flashlog_start$", actual, re.M)
        if extended:
            # Force the script to materialize the provided defaults symbols.
            source.write_text(source.read_text() + ".word __custom_defaults_start\n.word __custom_defaults_end\n")
            result = execute("gcc", "-mcpu=cortex-m4", "-mthumb", "-nostdlib", source,
                             "-Wl,-L" + str(linkdir), "-T" + str(linkdir / "at32_flash_f43xM_blackbox.ld"),
                             "-Wl,--defsym,USE_CUSTOM_DEFAULTS_EXTENDED=1", "-o", work / "stub.elf")
            assert result.returncode == 0, result.stderr
            actual = execute("nm", "-n", work / "stub.elf", check=True).stdout
            assert re.search(r"^081fc000 \w __custom_defaults_start$", actual, re.M), actual
            assert re.search(r"^08200000 \w __custom_defaults_end$", actual, re.M), actual
    source.write_text(".syntax unified\n.thumb\n.section .text\n.global Reset_Handler\nReset_Handler: .space 0x200001\n")
    result = execute("gcc", "-mcpu=cortex-m4", "-mthumb", "-nostdlib", source,
                     "-Wl,-L" + str(linkdir), "-T" + str(linkdir / "at32_flash_f43xM_blackbox.ld"), "-o", work / "overflow.elf")
    assert result.returncode != 0 and "overflowed" in result.stderr, result.stderr
print("PASS: normal/extended-defaults link layouts and oversized firmware rejection")
