from pathlib import Path
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import time

source = Path(sys.argv[1]).resolve()
evidence = Path(sys.argv[2]).resolve()
evidence.mkdir(parents=True, exist_ok=True)
build = source.parent / 'math-build'
arms = [
    (10314, '1673ec6b712c70e1973fad68b614f457ebba16b2', ['log1p']),
    (10315, '916ef2d7fb63ba22dde66cf1202a18561f1913d5', ['atanh']),
    (10468, '5cc53a56c94d2b874a33019adcc26e11bb5a9343', ['nextafterf', 'nextafter']),
]

def git(*args):
    return subprocess.check_output(['git', '-C', str(source), *args], text=True).strip()

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def function(text, name):
    match = re.search(r'^(?:float|double) APS5_VABI ' + name + r'_nid_postfix\([^\n]*\) \{', text, re.M)
    assert match, name
    start = match.start()
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return start, end

receipt = {'platform': platform.platform(), 'machine': platform.machine(), 'compiler': subprocess.check_output(['g++', '--version'], text=True), 'started': time.time(), 'terminal': False, 'arms': {}}
(evidence / 'environment.json').write_text(json.dumps(receipt, indent=2) + '\n')
subprocess.run(['git', '-C', str(source), 'fetch', '--no-tags', 'origin', *[a[1] for a in arms]], check=True)
all_passed = True
for number, head, names in arms:
    folder = evidence / str(number)
    folder.mkdir(exist_ok=True)
    state = {'head': head, 'started': time.time(), 'terminal': False, 'commands': []}
    receipt['arms'][str(number)] = state
    original = None
    math = source / 'core/libs/prx/libc/src/Math.cpp'
    fixture = source / 'core/libs/tests/GuestMath.cpp'

    def command(label, args, timeout=1800):
        state['commands'].append({'label': label, 'argv': args})
        with (folder / (label + '.log')).open('w', encoding='utf-8') as log:
            result = subprocess.run(args, cwd=source, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        return result.returncode

    compile_command = ['cmake', '--build', str(build), '--parallel', '4', '--target', 'libc', 'guest_math_tests', 'guest_math_exact_tests', 'guest_dinkumware_internals_tests']
    def tests(label, pattern, repeat=1):
        return command(label, ['ctest', '--test-dir', str(build), '-R', pattern, '-V', '--no-tests=error', '--timeout', '60', '--repeat', 'until-fail:' + str(repeat), '--output-junit', str(folder / (label + '.xml'))], 600)

    try:
        subprocess.run(['git', '-C', str(source), 'checkout', '--detach', head], check=True)
        assert git('rev-parse', 'HEAD') == head
        assert command('submodules', ['git', 'submodule', 'update', '--init', '--recursive', '--depth', '1', '--jobs', '4']) == 0
        assert command('configure', ['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON', '-DCMAKE_C_COMPILER=gcc', '-DCMAKE_CXX_COMPILER=g++', '-DANYPS5_ENABLE_SPIRV_TOOLS=OFF']) == 0
        assert command('positive-build', compile_command) == 0
        assert tests('positive-tests', '^(guest_math|guest_math_exact|guest_dinkumware_internals|public_headers)$', 3) == 0
        original = math.read_bytes()
        state['positive_math_sha256'] = digest(math)
        state['fixture_sha256'] = digest(fixture)
        library = build / 'core/libs/libs/unpatched/libc.prx'
        state['positive_library_sha256'] = digest(library)
        text = original.decode()
        reference = git('show', head + '~2:core/libs/prx/libc/src/Math.cpp') if number == 10468 else None
        for name in names:
            start, end = function(text, name)
            if reference is None:
                replacement = 'double APS5_VABI ' + name + '_nid_postfix(double x) { return std::' + name + '(x); }'
            else:
                a, b = function(reference, name)
                replacement = reference[a:b]
            text = text[:start] + replacement + text[end:]
        math.write_bytes(text.encode())
        (folder / 'negative.patch').write_text(git('diff', '--', 'core/libs/prx/libc/src/Math.cpp'))
        assert digest(fixture) == state['fixture_sha256']
        assert command('negative-build', compile_command) == 0
        code = tests('negative-math', '^guest_math$')
        log = (folder / 'negative-math.log').read_text(errors='replace')
        state.update(negative_exit=code, negative_aborted=bool(re.search(r'Subprocess aborted|Exit code 0xc0000409|Exit code 0x40000015', log, re.I)))
        assert code != 0 and state['negative_aborted'], 'Expected a math assertion abort, not an arbitrary failure'
        assert tests('negative-dinkumware', '^guest_dinkumware_internals$') == 0
        state['negative_passed'] = True
    except BaseException as error:
        state['error'] = repr(error)
    finally:
        if original is not None:
            math.write_bytes(original)
            try:
                assert command('restored-build', compile_command) == 0
                assert tests('restored-tests', '^(guest_math|guest_math_exact|guest_dinkumware_internals)$', 3) == 0
                state['restored_math_sha256'] = digest(math)
                state['restored_fixture_sha256'] = digest(fixture)
                state['restored_library_sha256'] = digest(library)
                assert state['restored_math_sha256'] == state['positive_math_sha256']
                assert state['restored_fixture_sha256'] == state['fixture_sha256']
                assert not git('status', '--porcelain', '--untracked-files=no')
                state['restored_passed'] = True
            except BaseException as error:
                state['restore_error'] = repr(error)
        state.update(terminal=True, finished=time.time(), passed=bool(state.get('negative_passed') and state.get('restored_passed')))
        all_passed = all_passed and state['passed']
        (folder / 'result.json').write_text(json.dumps(state, indent=2) + '\n')
        print(number, head, 'PASS' if state['passed'] else 'FAIL', flush=True)
receipt.update(terminal=True, finished=time.time(), passed=all_passed, scope='CPU library tests on a GitHub-hosted Windows runner; no local Windows agent, title or GPU execution.')
(evidence / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
raise SystemExit(0 if all_passed else 1)
