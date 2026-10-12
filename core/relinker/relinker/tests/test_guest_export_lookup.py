from pathlib import Path
import os
import ctypes
import platform
import struct
import subprocess
import sys
import tempfile

from test_guest_intel_trampolines import PLAIN_SITE, guest_fixture, main_fixture
from test_guest_module_directories import needed_libraries
from test_guest_symbol_names import dynamic_tags, file_offset, imported_symbols, module_symbols
from test_windows_import_modules import executable


def guest_symbols(symbols, dependency=None):
    image = guest_fixture(PLAIN_SITE)
    image[0x400:0x406] = b'\xb8\x2a\x00\x00\x00\xc3'
    strings = bytearray(b'\0')
    table = bytearray(24)
    for name, info, visibility, section in symbols:
        offset = len(strings)
        strings.extend(name.encode() + b'\0')
        table.extend(struct.pack('<IBBHQQ', offset, info, visibility, section,
                                 0x1000 if section else 0, 6 if section else 0))
    dependency_offset = len(strings)
    if dependency:
        strings.extend(dependency.encode() + b'\0')
    string_offset = 0x800
    symbol_offset = (string_offset + len(strings) + 7) & ~7
    hash_offset = symbol_offset + len(table)
    end = hash_offset + 8 + 4 * (1 + len(table) // 24)
    image.extend(bytes(max(0, end - len(image))))
    image[string_offset:string_offset + len(strings)] = strings
    image[symbol_offset:symbol_offset + len(table)] = table
    image[hash_offset:end] = bytes(end - hash_offset)
    struct.pack_into('<II', image, hash_offset, 1, len(table) // 24)
    address = lambda offset: offset + 0x1a00
    tags = [(5, address(string_offset)), (10, len(strings)),
            (6, address(symbol_offset)), (11, 24), (4, address(hash_offset))]
    if dependency:
        tags.append((1, dependency_offset))
    tags.append((0, 0))
    image[0x600:0x800] = bytes(0x200)
    for index, tag in enumerate(tags):
        struct.pack_into('<qQ', image, 0x600 + index * 16, *tag)
    struct.pack_into('<QQ', image, 120 + 32, end - 0x600, end - 0x600)
    struct.pack_into('<QQ', image, 176 + 32, len(tags) * 16, len(tags) * 16)
    return image


def large_modules(count):
    names = [f'entry{index:06d}' for index in range(count)]
    return {
        'provider.prx': guest_symbols((name, 0x12, 0, 1) for name in names),
        'consumer.prx': guest_symbols(((name, 0x12, 0, 0) for name in reversed(names)),
                                      'provider.prx'),
    }


def aliases(reverse=False):
    names = ['entry#A#A', 'entry#B#A']
    if reverse:
        names.reverse()
    image = guest_symbols([(name, 0x12, 3, 1) for name in names])
    tags = dynamic_tags(image)
    slots = len(image)
    image.extend(bytes(16))
    relocations = len(image)
    for index in (1, 2):
        image.extend(struct.pack('<QQq', slots + 0x1a00 + (index - 1) * 8,
                                 (index << 32) | 1, 0))
    entries = [(tag, value) for tag, value in tags.items()]
    entries += [(7, relocations + 0x1a00), (8, 48), (9, 24), (0, 0)]
    for index, entry in enumerate(entries):
        struct.pack_into('<qQ', image, 0x600 + index * 16, *entry)
    struct.pack_into('<QQ', image, 120 + 32, len(image) - 0x600, len(image) - 0x600)
    struct.pack_into('<QQ', image, 176 + 32, len(entries) * 16, len(entries) * 16)
    aligned = bytearray(0x2000 + len(image) - 0x600)
    aligned[:64 + 3 * 56] = image[:64 + 3 * 56]
    aligned[0x1000:0x1100] = image[0x400:0x500]
    aligned[0x2000:] = image[0x600:]
    for header, offset in ((64, 0x1000), (120, 0x2000), (176, 0x2000)):
        struct.pack_into('<Q', aligned, header + 8, offset)
    for header in (64, 120):
        struct.pack_into('<Q', aligned, header + 48, 0x1000)
    return aligned, slots + 0x1a00


def check_aliases(relinker, work):
    cases = 0
    for reverse in (False, True):
        image, slots = aliases(reverse)
        result, output = convert(relinker, work / f'aliases-{reverse}', False, {
            'provider.prx': image,
            'consumer.prx': guest_symbols([('entry', 0x12, 0, 0)], 'provider.prx'),
        })
        assert result.returncode == 0, (result.stdout, result.stderr)
        provider = output.parent / 'app0/sce_module/provider.prx.guest.prx'
        generated = provider.read_bytes()
        tags = dynamic_tags(generated)
        assert module_symbols(generated) == ['entry#guest', 'entry#guest']
        symbols = file_offset(generated, tags[6])
        assert generated[symbols + 28:symbols + 48] == generated[symbols + 52:symbols + 72]
        relocations = file_offset(generated, tags[7])
        assert [struct.unpack_from('<QQq', generated, relocations + i * 24)
                for i in range(2)] == [(slots, (1 << 32) | 1, 0), (slots + 8, (2 << 32) | 1, 0)]
        result = subprocess.run([str(relinker), '--macos', str(output.parent / 'input.elf'),
                                 str(output.parent / 'output.macho')], capture_output=True,
                                text=True, timeout=30)
        assert result.returncode != 0 and 'Duplicate or empty export' in result.stderr, (result.stdout, result.stderr)
        if sys.platform == 'linux' and platform.machine().lower() in ('x86_64', 'amd64'):
            library = ctypes.CDLL(str(provider))
            function = getattr(library, 'entry#guest')
            function.restype = ctypes.c_int
            assert function() == 42
            address = ctypes.cast(function, ctypes.c_void_p).value
            assert [ctypes.c_uint64.from_address(address - 0x1000 + slots + i * 8).value
                    for i in range(2)] == [address, address]
        cases += 1
    image, _ = aliases()
    symbols = file_offset(image, dynamic_tags(image)[6])
    for field, layout, value in ((4, '<B', 0x22), (4, '<B', 0x11), (5, '<B', 0),
                                  (6, '<H', 2), (8, '<Q', 0x1010), (16, '<Q', 7)):
        conflicting = bytearray(image)
        struct.pack_into(layout, conflicting, symbols + 48 + field, value)
        result, _ = convert(relinker, work / f'conflict-{field}-{value}', False, {'provider.prx': conflicting})
        assert result.returncode != 0 and 'Duplicate or empty export' in result.stderr, (result.stdout, result.stderr)
        cases += 1
    for info in (0x11, 0x10):
        nonfunction = bytearray(image)
        for index in (1, 2):
            struct.pack_into('<B', nonfunction, symbols + index * 24 + 4, info)
        result, _ = convert(relinker, work / f'nonfunction-{info}', False, {'provider.prx': nonfunction})
        assert result.returncode != 0 and 'Duplicate or empty export' in result.stderr, (result.stdout, result.stderr)
        cases += 1
    return cases


def convert(relinker, case, windows, modules, source=None):
    directory = case / 'sce_module'
    directory.mkdir(parents=True)
    for name, image in modules.items():
        (directory / name).write_bytes(image)
    input_path = case / 'input.elf'
    input_path.write_bytes(main_fixture() if source is None else source)
    output = case / ('output.exe' if windows else 'output.elf')
    result = subprocess.run([str(relinker), *(['--windows'] if windows else []),
                             str(input_path), str(output)], capture_output=True,
                            text=True, timeout=30)
    return result, output


def main():
    relinker = Path(sys.argv[1]).resolve()
    cases = 0
    with tempfile.TemporaryDirectory(prefix='anyps5-export-lookup-') as directory:
        work = Path(directory)
        cases += check_aliases(relinker, work)
        for windows in (False, True):
            modules = large_modules(4096)
            result, output = convert(relinker, work / f'large-{windows}', windows, modules,
                                     executable('provider.prx', symbol='entry004095'))
            assert result.returncode == 0, (result.stdout, result.stderr)
            provider = output.parent / 'app0/sce_module/provider.prx.guest.prx'
            consumer = output.parent / 'app0/sce_module/consumer.prx.guest.prx'
            assert provider.is_file() and consumer.is_file()
            if not windows:
                names = {f'entry{index:06d}#guest' for index in range(4096)}
                assert set(module_symbols(provider.read_bytes())) == names
                assert set(module_symbols(consumer.read_bytes())) == names
                assert needed_libraries(consumer.read_bytes()) == ['$ORIGIN/provider.prx.guest.prx']
                assert imported_symbols(output.read_bytes()) == {'entry004095#guest'}
            elif os.name == 'nt':
                run = subprocess.run([str(output)], capture_output=True, text=True, timeout=30)
                assert run.returncode == 42, (run.returncode, run.stdout, run.stderr)
            cases += 1
            for label, exports, imports, error in (
                    ('object', [('entry', 0x11, 0, 1)], [('entry', 0x11, 0, 0)], None),
                    ('unspecified', [('entry', 0x12, 0, 1)], [('entry', 0x10, 0, 0)], None),
                    ('mismatch', [('entry', 0x11, 0, 1)], [('entry', 0x12, 0, 0)],
                     'Guest import/export type mismatch: entry'),
                    ('filtered-first', [('entry', 0x12, 2, 1), ('entry', 0x11, 0, 1)],
                     [('entry', 0x11, 0, 0)], None),
                    ('filtered-mismatch', [('entry', 0x12, 2, 1), ('entry', 0x11, 0, 1)],
                     [('entry', 0x12, 0, 0)], 'Guest import/export type mismatch: entry'),
                    ('internal-first', [('entry', 0x12, 1, 1), ('entry', 0x11, 0, 1)],
                     [('entry', 0x11, 0, 0)], None),
                    ('local-first', [('entry', 0x02, 0, 1), ('entry', 0x11, 0, 1)],
                     [('entry', 0x11, 0, 0)], None),
                    ('duplicate-first', [('entry#A#A', 0x12, 0, 1), ('entry#B#A', 0x12, 0, 1)],
                     [('entry', 0x12, 0, 0)], None),
                    ('duplicate-mismatch', [('entry#A#A', 0x12, 0, 1), ('entry#B#A', 0x12, 0, 1)],
                     [('entry', 0x11, 0, 0)], 'Guest import/export type mismatch: entry'),
                    ('weak', [('entry', 0x22, 0, 1)], [('entry', 0x12, 0, 0)], None),
                    ('protected', [('entry', 0x12, 3, 1)], [('entry', 0x12, 0, 0)], None)):
                result, _ = convert(relinker, work / f'{label}-{windows}', windows, {
                    'provider.prx': guest_symbols(exports),
                    'consumer.prx': guest_symbols(imports, 'provider.prx'),
                })
                if error:
                    assert result.returncode != 0 and error in result.stderr, (result.stdout, result.stderr)
                else:
                    assert result.returncode == 0, (result.stdout, result.stderr)
                cases += 1
    print(f'{cases} guest export lookup cases passed')


if __name__ == '__main__':
    main()
