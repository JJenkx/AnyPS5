#!/usr/bin/env python3
"""Add synthetic process metadata to this test's own ELF, never to a game image."""

from pathlib import Path
import shutil
import struct
import sys


def fixtures(data):
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("fixture requires a little-endian ELF64 executable")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    _, kind, machine, _, _, phoff, shoff, _, _, phsize, phcount, shsize, shcount, names = header
    if kind not in (2, 3) or machine != 62 or phsize != 56 or shsize != 64 or names >= shcount:
        raise ValueError("unsupported fixture ELF headers")
    if phoff + phsize * phcount > len(data) or shoff + shsize * shcount > len(data):
        raise ValueError("truncated fixture ELF headers")
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shsize) for i in range(shcount)]
    strings = sections[names]
    if strings[4] + strings[5] > len(data):
        raise ValueError("truncated fixture section names")
    strings = data[strings[4]:strings[4] + strings[5]]
    matches = [section for section in sections if strings[section[0]:].split(b"\0", 1)[0] == b".sdkproc"]
    if len(matches) != 1:
        raise ValueError("fixture requires exactly one .sdkproc section")
    section = matches[0]
    if section[1] != 1 or section[2] & 3 != 3 or section[5] != 0x41 or section[4] + section[5] > len(data):
        raise ValueError("fixture metadata must be 65 allocated, writable file bytes")
    address, offset = section[3] + 1, section[4] + 1
    headers = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize) for i in range(phcount)]
    if not any(p[0] == 1 and p[3] <= address and address + 0x40 <= p[3] + p[6]
               and p[2] <= offset and offset + 0x40 <= p[2] + p[5]
               and address - p[3] == offset - p[2] for p in headers):
        raise ValueError("fixture metadata is not contained in a file-backed PT_LOAD")
    slots = [i for i, p in enumerate(headers) if p[0] == 0x61000001]
    if len(slots) > 1:
        raise ValueError("duplicate fixture process headers")
    if not slots:
        slots = [i for i, p in enumerate(headers) if p[0] == 4]
    if not slots:
        raise ValueError("fixture requires a PT_NOTE slot (link with --build-id)")
    slot = phoff + slots[-1] * phsize
    outputs = {}
    for mode in ("positive", "missing", "truncated", "unmapped"):
        result = bytearray(data)
        size = 0x3f if mode == "truncated" else 0x40
        location = 0xffffffffffff0000 if mode == "unmapped" else address
        struct.pack_into("<IIQQQQQQ", result, slot, 0 if mode == "missing" else 0x61000001,
                         6, offset, location, location, size, size, 1)
        outputs[mode] = result
    return outputs


def main():
    path = Path(sys.argv[1])
    outputs = fixtures(path.read_bytes())
    for mode, data in outputs.items():
        target = path if mode == "positive" else Path(str(path) + "." + mode)
        if target != path:
            shutil.copyfile(path, target)
            shutil.copymode(path, target)
        target.write_bytes(data)


if __name__ == "__main__":
    main()
