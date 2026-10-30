"""Opus 2's plugin build: a plugin folder -> one .opmod file.

    python plugin_tool.py build DIR [--out FILE.opmod]     (the kit's command)

DIR holds manifest.txt and the plugin's C and assembly sources (*.c, *.s, *.S at its top level); a DIR with
no sources builds a code-free .opmod (image size 0, flags without the code bit: its manifest is all it brings,
a theme's `theme` lines, settings; 2026-10-21).  The
sources are compiled as the core's are (build.CFLAGS) against the SDK: opus_plugin.h (the
core's names) and op1fw.h in plugin form (the firmware's functions by name, no addresses: fwheader.py).
Every name the plugin uses and does not define is an import.  A called import gets a stub in the image
(`P1.L = lo; P1.H = hi; JUMP (P1)`, written here, its two halves relocations to the import), so the
plugin's calls reach it by a relative branch and the image may land anywhere: the core's far homes lie
beyond a call's ±16 MB.  The image is linked twice at two bases
(opmod.link_two_bases: the relocation table checked byte for byte), and the file written:

  header (0x60 bytes), the manifest (the author's lines and the built ones), the image (code and data,
  as linked at the first base), imports (u32: each name's offset in the strings), relocations (12 bytes:
  site | kind << 28 | import << 31, target, value), exports (8 bytes: name offset, offset in the image),
  places (24 bytes: kind | expected length << 8, the firmware's address, the target's offset in the
  image, the place's name offset, the 8 expected bytes), the strings; the CRC-32 of the whole file with
  its own field 0.

A place is a manifest line as the .hooks files have it:
`place call|jump|ldimm|word|patch ADDRESS SYMBOL expect HEX [as NAME] [switched]`; the core checks the expected
bytes and installs it at load (a call or jump: a 24-bit branch to SYMBOL; ldimm: the pair Rx.L/Rx.H loading
SYMBOL's address; word: the 32-bit word at ADDRESS; patch, 2026-10-21: as many bytes as expected, 2 to 8, from
SYMBOL's data in the image, such as tape undo's NOPs). A switched place (2026-10-25, core 2.1: a call, a word or a
patch of 2 or 4 bytes) is checked and held at load but left stock; the plugin turns it on and off with
opus_place_set (tape undo's loop jump behind a setting). The kind byte's bit 0x80 marks it.

read_opmod() checks a file the way the core's loader does (every offset, size, relocation and import
inside the file, the CRC) and is what the tests and `op1mod plugin --show` read with.
"""
import os
import re
import struct
import time
import subprocess
import zlib

import opmod
from build import CFLAGS, tool, toolchain

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SDK = next((d for d in (os.path.join(REPO, 'mods', 'opus2', 'sdk'), os.path.join(REPO, 'sdk')) if os.path.isdir(d)),
           os.path.join(REPO, 'mods', 'opus2', 'sdk'))   # the repository's, or the authors' kit's
MAGIC = b'OPMD'
FORMAT = 1
HEADER = 0x60
PLACE_KINDS = {'call': 0, 'jump': 1, 'ldimm': 2, 'word': 3, 'patch': 4}
PLACE_NAMES = {v: k for k, v in PLACE_KINDS.items()}
PLACE_SWITCHED = 0x80                      # in the kind byte: switched by its plugin (opus_place_set), not installed at load
FLAG_CODE, FLAG_PACK, FLAG_LIBRARY = 1, 2, 4
KINDS = {'word': 0, 'lo': 1, 'hi': 2, 'call': 3}
KIND_NAMES = {v: k for k, v in KINDS.items()}
STUB_PREFIX = 'opimp_'                     # the import a stub loads: C name opimp_NAME, the plugin calls NAME
CALL_RELOCS = ('R_BFIN_PCREL24', 'R_BFIN_PCREL24_JUMP_L', 'R_BFIN_PCREL24_CALL_X')
ID_RE = re.compile(r'^[a-z0-9_]+(\.[a-z0-9_]+)+$')
VERSION_RE = re.compile(r'^\d+\.\d+(\.\d+)?$')
REPEATED = ('needs', 'breaks', 'setting', 'place', 'replace', 'pack', 'uses', 'removes', 'adds', 'checked', 'theme')
THEMES_MAX = 4                             # loader.c THEMES_MAX
NEEDS_MAX = 4                              # loader.c NEEDS_MAX
LINE_MAX = 159                             # loader.c reads a manifest line into char line[160]
THEME_RE = re.compile(r'^(\S.{0,30}?) +([0-9A-Fa-f]{6}) +([0-9A-Fa-f]{6}) +([0-9A-Fa-f]{6}) +([0-9A-Fa-f]{6})$')
REQUIRED = ('opmod', 'id', 'name', 'author', 'version', 'core')
BUILT = ('deep', 'ram', 'imports', 'cpu')  # written by the build; refused in the author's manifest
MAX_IMAGE = 1 << 20


class PluginError(Exception):
    pass


# ---------------------------------------------------------------- the manifest

COMMENT_RE = re.compile(r'(?:^|(?<=[ \t]))#')


def uncomment(raw):
    """A manifest line without its comment: # at the start or after a space or tab (loader.c the same; 2026-10-21:
    a choice like c# is a word), stripped."""
    return COMMENT_RE.split(raw, 1)[0].strip()


def read_manifest(text):
    """-> {key: value} (the repeated keys: lists); PluginError on a line the contract does not allow."""
    out = {}
    for n, raw in enumerate(text.splitlines(), 1):
        line = uncomment(raw)
        if not line:
            continue
        if uncomment(raw[:LINE_MAX]) != line:  # the core reads 159 characters a line (loader.c's line[160])
            raise PluginError(f'manifest line {n}: longer than the {LINE_MAX} characters the core reads')
        key, _, value = line.partition(' ')
        value = value.strip()
        if not re.match(r'^[a-z]+$', key) or not value:
            raise PluginError(f'manifest line {n}: "key value" expected, got {raw!r}')
        if key in REPEATED:
            out.setdefault(key, []).append(value)
        elif key in out:
            raise PluginError(f'manifest line {n}: {key} given twice')
        else:
            out[key] = value
    for key in REQUIRED:
        if key not in out:
            raise PluginError(f'manifest: no {key} line')
    if out['opmod'] != str(FORMAT):
        raise PluginError(f'manifest: opmod {out["opmod"]}, this build writes {FORMAT}')
    if not ID_RE.match(out['id']) or len(out['id']) > 47:
        raise PluginError(f'manifest: id {out["id"]!r}: author.plugin, a-z 0-9 _ and dots, up to 47 characters')
    if not VERSION_RE.match(out['version']) or not VERSION_RE.match(out['core']):
        raise PluginError('manifest: version and core are numbers like 1.0.0 and 2.0')
    for key in BUILT:
        if key in out and not (key == 'cpu' and out[key] == 'user'):   # 2026-10-25: a manager's own word
            raise PluginError(f'manifest: {key} is written by the build')
    if len(out.get('needs', [])) > NEEDS_MAX:     # the core reads 4 and would pass over the rest
        raise PluginError(f'manifest: {len(out["needs"])} needs lines, the core reads {NEEDS_MAX}')
    for need in out.get('needs', []):
        w = need.split()
        if not (len(w) == 2 and ID_RE.match(w[0]) and VERSION_RE.match(w[1])):
            raise PluginError(f'manifest: needs {need!r}: a library\'s id and its lowest version')
    uses = [u for u in out.get('uses', []) if u != 'none']
    if len(uses) > USES_MAX:
        raise PluginError(f'manifest: {len(uses)} uses lines, the core keeps {USES_MAX}')
    for use in uses:
        check_use(use)
    settings = out.get('setting', [])
    if 'none' in settings:                    # the core refuses it ("a setting line"): no setting is no line
        raise PluginError('manifest: setting none: leave the setting lines out instead')
    if len(settings) > SETTINGS_MAX:
        raise PluginError(f'manifest: {len(settings)} setting lines, the core keeps {SETTINGS_MAX}')
    for setting in settings:
        check_setting(setting)
    themes = out.get('theme', [])
    if len(themes) > THEMES_MAX:
        raise PluginError(f'manifest: {len(themes)} theme lines, the core keeps {THEMES_MAX}')
    for theme in themes:
        if not THEME_RE.match(theme):
            raise PluginError(f'manifest: theme {theme!r}: a name (up to 31 characters) and four colours as RRGGBB '
                              '(no #: it starts a comment), blue, green, white, orange')
    for checked in out.get('checked', []):
        check_checked(checked)
    return out


CHECKED_RE = re.compile(r'^(emulator|device) (\d{4}-\d{2}-\d{2})(?: (.+))?$')


def check_checked(line):
    """`checked emulator|device YYYY-MM-DD [what: a test, a build, a note]` (from op1-sdk's catalog): where
    and when the author saw the plugin work, shown by --show and --check; the core passes it over."""
    import datetime
    m = CHECKED_RE.match(line)
    try:
        ok = bool(m) and datetime.date.fromisoformat(m.group(2)) is not None
    except ValueError:
        ok = False
    if not ok:
        raise PluginError(f'manifest: checked {line!r}: "emulator" or "device", a date YYYY-MM-DD, then what was run')
    if m.group(3) and len(m.group(3)) > 60:
        raise PluginError(f'manifest: checked {line!r}: what was run in 60 characters at most')


def checked_of(man):
    """-> the latest `checked` line's text of each kind: {"emulator": "2026-10-16 test_bitcrush", ...}."""
    out = {}
    for line in man.get('checked', []):
        kind, _, rest = line.partition(' ')
        if rest[:10] >= out.get(kind, '')[:10]:
            out[kind] = rest
    return out


SETTINGS_MAX = 12


def check_setting(setting):
    """`NAME LOW HIGH DEFAULT "label"` or `NAME A,B,C DEFAULT "label"`, as the core reads it (loader.c)."""
    m = re.match(r'^([^"]*)"([^"]*)"$', setting)
    w = m.group(1).split() if m else []
    form = f'manifest: setting {setting!r}: NAME LOW HIGH DEFAULT "label" or NAME A,B,C DEFAULT "label"'
    if not m or len(w) not in (3, 4) or not re.match(r'^[a-z0-9_]{1,15}$', w[0]):
        raise PluginError(form)
    if len(m.group(2)) > 23:
        raise PluginError(f'manifest: setting {setting!r}: its label in 23 characters at most')
    if len(w) == 4:
        try:
            lo, hi, default = (int(x) for x in w[1:])
        except ValueError:
            raise PluginError(form) from None
        if not -32768 <= lo <= default <= hi <= 32767:
            raise PluginError(f'manifest: setting {setting!r}: LOW <= DEFAULT <= HIGH, within a short')
    else:
        choices = w[1].split(',')         # the OP-1's font draws # and /: c#, 6/9 (2026-10-21)
        if len(w[1]) > 95 or not all(re.match(r'^[a-z0-9_#/+-]+$', c) for c in choices):
            raise PluginError(f'manifest: setting {setting!r}: choices a,b,c (a-z 0-9 _ # / + -), 95 characters at '
                              'most')
        if w[2] not in choices:
            raise PluginError(f'manifest: setting {setting!r}: the default {w[2]!r} is not one of the choices')


# The controls a `uses` line can name (the core's loader.c INPUTS: the OS's key codes, the encoders' turns, the
# keyboard's note keys) and the screens.
USES_MAX = 12
USE_INPUTS = ('tempo', 'synth', 'drum', 'tape', 'mixer', 't1', 't2', 't3', 't4', 'lift', 'drop', 'split',
              's1', 's2', 's3', 's4', 's5', 's6', 's7', 's8', 'seq', 'rec', 'play', 'stop', 'left', 'right', 'mic',
              'com', 'blue_push', 'green_push', 'white_push', 'orange_push', 'blue_turn', 'green_turn',
              'white_turn', 'orange_turn', 'keyboard')
USE_SCREENS = ('synth', 'drum', 'tape', 'mixer')
USE_PAGES = ('t1', 't2', 't3', 't4')      # after the input: the page (2026-10-23)


def check_use(use):
    """`INPUT [shift | noshift] [fn | nofn] [with KEY] [synth | drum | tape | mixer] [t1 | t2 | t3 | t4] [modal] "what it
    does"`, as the core reads it (fn: the core's FN held, the help key; 2026-10-17; with: a key held, 2026-10-21; a
    page, 2026-10-23)."""
    m = re.match(r'^([^"]*)"([^"]*)"$', use)
    words = m.group(1).split() if m else []
    if not m or not words or not m.group(2).strip():
        raise PluginError(f'manifest: uses {use!r}: INPUT [shift|noshift] [fn|nofn] [with KEY] [SCREEN] [PAGE] [modal] '
                          '"what it does"')
    if words[0] in ('fn', 'help'):
        raise PluginError(f"manifest: uses {use!r}: fn is the core's: a uses line's modifier (INPUT fn ...)")
    if words[0] not in USE_INPUTS:
        raise PluginError(f'manifest: uses {use!r}: {words[0]!r} is no input ({", ".join(USE_INPUTS)})')
    rest = list(words[1:])
    if 'with' in rest:                     # `with KEY`: only while that key is held (2026-10-21, tape + left)
        k = rest.index('with')
        held = rest[k + 1] if k + 1 < len(rest) else ''
        if held not in USE_INPUTS or held.endswith('_turn') or held == 'keyboard' or held == words[0]:
            raise PluginError(f'manifest: uses {use!r}: with KEY (a key held: not a turn, the keyboard or the input)')
        del rest[k:k + 2]
    for w in rest:
        if w not in ('shift', 'noshift', 'fn', 'nofn', 'modal') + USE_SCREENS + USE_PAGES:
            raise PluginError(f'manifest: uses {use!r}: {w!r} (shift, noshift, fn, nofn, modal, a screen: '
                              f'{", ".join(USE_SCREENS)}, or a page: {", ".join(USE_PAGES)})')
    if len(m.group(2)) > 27:
        raise PluginError(f'manifest: uses {use!r}: what it does in 27 characters at most')


# ---------------------------------------------------------------- the build

def _run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        raise PluginError(f'{os.path.basename(cmd[0])} failed:\n{p.stdout}{p.stderr}')
    return p.stdout


def compile_sources(tc, src_dir, out_dir):
    """-> the objects of DIR's sources, compiled against the SDK and op1fw.h in plugin form."""
    import fwheader
    if os.path.isfile(fwheader.FUNCTIONS):  # made from the function table; the kit has none: its sdk/op1fw.h is found
        with open(os.path.join(out_dir, 'op1fw.h'), 'w', newline='\n', encoding='utf-8') as f:
            f.write(fwheader.generate(plugin=True))
    sources = sorted(f for f in os.listdir(src_dir) if os.path.splitext(f)[1] in ('.c', '.s', '.S'))
    if not sources:
        raise PluginError(f'{src_dir}: no sources')
    objs = []
    for f in sources:
        obj = os.path.join(out_dir, f + '.o')
        _run([tool(tc, 'gcc')] + CFLAGS + ['-fno-common', '-I', out_dir, '-I', SDK, '-I', src_dir, '-c', '-o', obj,
                                           os.path.join(src_dir, f)])
        objs.append(obj)
    return objs


def called_imports(tc, objs):
    """-> the undefined symbols some instruction calls or jumps to (they get a stub), not libgcc's (division and
    the other helpers: linked into the image)."""
    libgcc = _run([tool(tc, 'gcc'), '-mcpu=bf524-any', '-print-libgcc-file-name']).strip()
    lib = {line.split()[-1] for line in _run([tool(tc, 'nm'), libgcc]).splitlines()
           if len(line.split()) == 3 and line.split()[1] in 'TtDdRrBb'}
    defined, undefined, called = set(), set(), set()
    for o in objs:
        for line in _run([tool(tc, 'nm'), o]).splitlines():
            kind, name = line.split()[-2:]
            (undefined if kind == 'U' else defined).add(name)
        for line in _run([tool(tc, 'objdump'), '-r', o]).splitlines():
            w = line.split()
            if len(w) >= 3 and w[1] in CALL_RELOCS:
                called.add(w[2].split('+')[0].split('-')[0])
    return sorted((undefined - defined - lib) & called)


def write_stubs(names, path):
    """An assembly file: for each called import NAME (its ELF name _NAME) a stub _NAME loading the import
    _opimp_NAME into P1 and jumping there.  P1 is the caller's to lose at a call (GCC passes in
    R0-R2 and keeps P3-P5)."""
    out = ['/* Generated by tools/op1mod/plugin.py: the stubs for the called imports. */', '        .text']
    for elf in names:
        c = elf[1:] if elf.startswith('_') else elf
        imp = f'_{STUB_PREFIX}{c}'
        out += [f'        .global {elf}', f'        .type {elf}, STT_FUNC', f'        .align 2', f'{elf}:',
                f'        P1.L = {imp};', f'        P1.H = {imp};', '        JUMP (P1);']
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(out) + '\n')


def read_places(lines):
    """The manifest's place lines -> [(kind, address, symbol, expected bytes, name, switched)]."""
    out = []
    for line in lines:
        w = line.split()
        name = None
        switched = len(w) in (6, 8) and w[-1] == 'switched'
        if switched:
            w = w[:-1]
        if len(w) == 7 and w[5] == 'as':
            name = w[6]
            w = w[:5]
        if len(w) != 5 or w[0] not in PLACE_KINDS or w[3] != 'expect':
            raise PluginError(f'place {line!r}: "place call|jump|ldimm|word|patch ADDRESS SYMBOL expect HEX [as NAME] '
                              f'[switched]"')
        try:
            addr, expect = int(w[1], 16), bytes.fromhex(w[4])
        except ValueError:
            raise PluginError(f'place {line!r}: the address and the expected bytes are hex')
        want = {'call': (4,), 'jump': (4, 6, 8), 'ldimm': (8,), 'word': (4,), 'patch': (2, 4, 6, 8)}[w[0]]
        if len(expect) not in want or addr % 2:
            raise PluginError(f'place {line!r}: {w[0]} expects {" or ".join(map(str, want))} bytes at an even address')
        name = name or f'{w[0]}_{addr:08x}'
        if switched and (w[0] in ('jump', 'ldimm') or len(expect) > 4):   # loader.c: one instruction or word
            raise PluginError(f'place {line!r}: only a call, a word or a patch of 2 or 4 bytes can be switched')
        if switched and len(name) > 23:
            raise PluginError(f'place {line!r}: a switched place\'s name is 23 characters at most')
        out.append((w[0], addr, w[2], expect, name, switched))
    return out


ORIG_RE = re.compile(r'^_orig_([0-9a-f]{8})$')
NOT_MOVABLE = re.compile(r'\b(JUMP|CALL|RTS|RTI|RTX|RTN|RTE|LSETUP|LOOP|IDLE|RAISE|EXCPT)\b')


def replay(addr, expect):
    """The instructions a jump place covers, repeated in its way back (orig_ADDR): -> their texts. Refused
    (PluginError) unless they are whole instructions that can run elsewhere: no target relative to the PC (a branch,
    a call, a loop's setup), no other change of flow, nothing naming P0 (the way back jumps on with it, free at a
    function's entry: GCC passes in R0-R2)."""
    _re_path()
    from op1re.bfdis import Dis, insn_length
    d, at, texts = Dis(), 0, []
    while at < len(expect):
        iw0 = struct.unpack_from('<H', expect, at)[0]
        n = insn_length(iw0)
        iw1 = struct.unpack_from('<H', expect, at + 2)[0] if at + 4 <= len(expect) else 0
        if at + n > len(expect):
            raise PluginError(f'place at 0x{addr:08x}: its {len(expect)} bytes end inside an instruction')
        r = d.decode(iw0, iw1, addr + at) if n <= 4 else None
        if not r:
            raise PluginError(f'place at 0x{addr:08x}: {expect[at:at + n].hex()} cannot be repeated elsewhere '
                              f'(not decoded); write its way back by hand')
        text, _, _, target = r
        if target is not None or NOT_MOVABLE.search(text) or re.search(r'\bP0\b', text):
            raise PluginError(f'place at 0x{addr:08x}: "{text}" cannot be repeated elsewhere (a PC-relative target, '
                              f'a change of flow or P0); write its way back by hand')
        texts.append(text)
        at += n
    return texts


def ways_back(tc, objs, places):
    """-> [(symbol, address, expected bytes, texts)]: the orig_ADDR names the plugin uses (undefined in its objects),
    each the way back of its jump place at ADDR."""
    defined, undefined = set(), set()
    for o in objs:
        for line in _run([tool(tc, 'nm'), o]).splitlines():
            kind, name = line.split()[-2:]
            (undefined if kind == 'U' else defined).add(name)
    jumps = {addr: expect for kind, addr, _, expect, _, _ in places if kind == 'jump'}
    out = []
    for name in sorted(undefined - defined):
        m = ORIG_RE.match(name)
        if not m:
            continue
        addr = int(m.group(1), 16)
        if addr not in jumps:
            raise PluginError(f'{name[1:]}: the manifest has no jump place at 0x{addr:08x}')
        out.append((name, addr, jumps[addr], replay(addr, jumps[addr])))
    return out


def write_ways(ways, path):
    """An assembly file: each orig_ADDR repeats the bytes its place covers, then jumps on with P0 to ADDR + their
    length (an absolute jump: the plugin may land beyond a branch's reach). As the core's effects_entry.S."""
    out = ['/* Generated by tools/op1mod/plugin.py: the jump places\' ways back. */', '        .text']
    for name, addr, expect, texts in ways:
        on = addr + len(expect)
        halves = ', '.join(f'0x{struct.unpack_from("<H", expect, k)[0]:04x}' for k in range(0, len(expect), 2))
        out += [f'        .global {name}', f'        .type {name}, STT_FUNC', '        .align 2',
                f'{name}:                         /* 0x{addr:08X}: {"; ".join(texts)} */', f'        .hword {halves}',
                f'        P0.L = 0x{on & 0xFFFF:04x};', f'        P0.H = 0x{on >> 16:04x};', '        JUMP (P0);']
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(out) + '\n')


def exports_of(tc, elf, base, wanted, local=False):
    """-> [(name, offset in the image)] for the manifest's `exports` names (with local, static symbols too:
    a place's target), from the link at `base`."""
    have = {}
    for line in _run([tool(tc, 'nm'), elf]).splitlines():
        w = line.split()
        if len(w) == 3 and (w[1] in 'TDBR' or (local and w[1] in 'tdbr')):
            have.setdefault(w[2].lstrip('_'), int(w[0], 16) - base)
    missing = [n for n in wanted if n not in have]
    if missing:
        raise PluginError('exports not defined by the plugin: ' + ', '.join(missing))
    return [(n, have[n]) for n in wanted]


CPU_FILE = 'cpu.txt'


def read_cpu(src_dir):
    """The plugin's measured CPU figure (its worst audio block, cycles) from DIR/cpu.txt, or None (unmeasured)."""
    path = os.path.join(src_dir, CPU_FILE)
    if not os.path.exists(path):
        return None
    for line in open(path, encoding='utf-8'):
        w = line.split('#', 1)[0].split()
        if w[:1] == ['cycles'] and len(w) == 2 and w[1].isdigit() and int(w[1]) > 0:
            return int(w[1])
    raise PluginError(f'{path}: no "cycles N" line')


def cpu_from(src_dir, report_path, name='cpu'):
    """DIR/cpu.txt from a test's report (an emulator test's `expect report cpu CYCLE ADDR`, the plugin's worst block): ->
    the cycles written, the largest of its ok reports (a test whose starts each report their worst block, 2026-10-27:
    outatime's power at full in a second start). The build turns it into the manifest's cpu line (measured, not by
    hand)."""
    found = None
    for line in open(report_path, encoding='utf-8'):
        m = re.search(r'\breport ' + re.escape(name) + r' (\d+)\s*$', line)
        if m and line.startswith('ok:'):
            found = max(found or 0, int(m.group(1)))
    if not found:
        raise PluginError(f'{report_path}: no "report {name} N" line (the test needs `expect report {name} CYCLE ADDR`)')
    timing = ' (memory timing)' if 'memory-timing' in os.path.abspath(report_path) else ''
    lines = ["# The plugin's CPU figure: its worst audio block in cycles, measured in the emulator (op1mod plugin",
             "# --cpu-from); the build writes it as the manifest's cpu line, the budget the core holds it to.",
             f'cycles {found}',
             f'measured {time.strftime("%Y-%m-%d")} {os.path.basename(report_path)}{timing}']
    with open(os.path.join(src_dir, CPU_FILE), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return found


def build(src_dir, out_path, tc=None, work=None):
    """-> a summary dict; writes out_path (.opmod)."""
    tc = toolchain(tc)
    with open(os.path.join(src_dir, 'manifest.txt'), encoding='utf-8') as f:
        author_text = f.read()
    man = read_manifest(author_text)
    if not any(os.path.splitext(f)[1] in ('.c', '.s', '.S') for f in os.listdir(src_dir)):
        return build_code_free(out_path, man, author_text)
    work = work or os.path.splitext(out_path)[0] + '.build'
    os.makedirs(work, exist_ok=True)
    objs = compile_sources(tc, src_dir, work)
    ways = ways_back(tc, objs, read_places(man.get('place', [])))
    if ways:
        ways_s = os.path.join(work, '_ways.s')
        write_ways(ways, ways_s)
        _run([tool(tc, 'as'), '-mcpu=bf524', '-o', ways_s + '.o', ways_s])
        objs = objs + [ways_s + '.o']
    stubbed = called_imports(tc, objs)
    if stubbed:
        stubs_s = os.path.join(work, '_stubs.s')
        write_stubs(stubbed, stubs_s)
        stubs_o = stubs_s + '.o'
        _run([tool(tc, 'as'), '-mcpu=bf524', '-o', stubs_o, stubs_s])
        objs = objs + [stubs_o]
    try:
        imports, relocs, summary = opmod.link_two_bases(tc, objs, work, 'plugin')
    except opmod.OpmodError as e:
        raise PluginError(str(e))
    imports = [n[len(STUB_PREFIX):] if n.startswith(STUB_PREFIX) else n for n in imports]
    relocs = [(site, kind, (name[len(STUB_PREFIX):] if name and name.startswith(STUB_PREFIX) else name), value)
              for site, kind, name, value in relocs]
    elf0 = os.path.join(work, 'plugin_0.elf')
    base, image, zero, _ = opmod.relocations(elf0)
    if len(image) > MAX_IMAGE or zero > MAX_IMAGE:
        raise PluginError(f'image {len(image)} bytes, zero fill {zero}: over {MAX_IMAGE}')
    entry = dict(exports_of(tc, elf0, base, ['opl_init']))['opl_init']
    wanted = man['exports'].split() if 'exports' in man else []
    exports = exports_of(tc, elf0, base, wanted) if wanted else []
    place_lines = read_places(man.get('place', []))
    targets = dict(exports_of(tc, elf0, base, sorted({p[2] for p in place_lines}), local=True)) if place_lines else {}
    places = [(kind, addr, targets[sym], expect, name, sw) for kind, addr, sym, expect, name, sw in place_lines]
    deep = sorted(n for n in imports if n.startswith('fw_'))
    built = [f'deep      {" ".join(deep) if deep else "none"}',
             f'ram       image {len(image)} zero {zero}',
             f'imports   {len(imports)}',
             f'cpu       {"user" if man.get("cpu") == "user" else read_cpu(src_dir) or "unmeasured"}']
    manifest = (author_text.rstrip('\n') + AUTHOR_END + '\n'.join(built) + '\n').encode('utf-8')
    data = write_opmod(manifest, image, zero, entry, imports, relocs, exports, library=bool(wanted), places=places)
    with open(out_path, 'wb') as f:
        f.write(data)
    return {'id': man['id'], 'version': man['version'], 'bytes': len(data), 'image': len(image), 'zero': zero,
            'imports': imports, 'stubs': [s.lstrip('_') for s in stubbed], 'exports': [n for n, _ in exports],
            'places': [p[4] for p in places], 'ways': [w[0][1:] for w in ways], 'link': summary}


def build_code_free(out_path, man, author_text):
    """A DIR without sources -> a code-free .opmod (§3: image size 0; no imports, places or exports)."""
    for key in ('place', 'exports'):
        if key in man:
            raise PluginError(f'manifest: {key} lines need code, and this plugin has no sources')
    built = ['deep      none', 'ram       image 0 zero 0', 'imports   0', 'cpu       0']
    manifest = (author_text.rstrip('\n') + AUTHOR_END + '\n'.join(built) + '\n').encode('utf-8')
    data = write_opmod(manifest, b'', 0, 0, [], [], code=False)
    with open(out_path, 'wb') as f:
        f.write(data)
    return {'id': man['id'], 'version': man['version'], 'bytes': len(data), 'image': 0, 'zero': 0,
            'imports': [], 'stubs': [], 'exports': [], 'places': [], 'ways': [], 'link': 'code-free: nothing linked'}


# ---------------------------------------------------------------- the file

def write_opmod(manifest, image, zero, entry, imports, relocs, exports=(), library=False, places=(), code=True):
    """-> the .opmod bytes (the layout in the module's docstring)."""
    strings, offsets = bytearray(), {}

    def s(name):
        if name not in offsets:
            offsets[name] = len(strings)
            strings.extend(name.encode('ascii') + b'\0')
        return offsets[name]
    imp_index = {n: i for i, n in enumerate(imports)}
    imp_tab = b''.join(struct.pack('<I', s(n)) for n in imports)
    rel_tab = b''
    for site, kind, name, value in relocs:
        word = site | KINDS[kind] << 28 | (1 << 31 if name else 0)
        rel_tab += struct.pack('<IIi', word, imp_index[name] if name else 0, value)
    exp_tab = b''.join(struct.pack('<II', s(n), off) for n, off in exports)
    pl_tab = b''.join(struct.pack('<IIII', PLACE_KINDS[kind] | (PLACE_SWITCHED if sw else 0) | len(expect) << 8, addr,
                                  target, s(name)) +
                      expect + b'\0' * (8 - len(expect)) for kind, addr, target, expect, name, sw in places)

    def pad(b):
        return bytes(b) + b'\0' * (-len(b) % 4)
    parts = [pad(manifest), pad(image), imp_tab, rel_tab, exp_tab, pl_tab, pad(strings)]
    offs, at = [], HEADER
    for p in parts:
        offs.append(at)
        at += len(p)
    flags = (FLAG_CODE if code else 0) | (FLAG_LIBRARY if library else 0)
    head = struct.pack('<4sHHII', MAGIC, FORMAT, HEADER, at, 0)
    head += struct.pack('<II', offs[0], len(manifest))
    head += struct.pack('<II', offs[1], len(image))
    head += struct.pack('<II', zero, entry)
    head += struct.pack('<II', offs[2], len(imports))
    head += struct.pack('<II', offs[3], len(relocs))
    head += struct.pack('<II', 0, 0)                          # resources: none yet
    head += struct.pack('<II', offs[6], len(strings))
    head += struct.pack('<II', flags, 0)
    head += struct.pack('<II', offs[4], len(exports))
    head += struct.pack('<II', offs[5], len(places))
    assert len(head) == HEADER
    data = bytearray(head + b''.join(parts))
    struct.pack_into('<I', data, 0x0C, zlib.crc32(bytes(data)) & 0xFFFFFFFF)
    return bytes(data)


def read_opmod(data):
    """The core loader's checks, in Python -> {manifest, image, zero, entry, imports, relocs, exports, flags};
    PluginError naming the first thing wrong."""
    if len(data) < HEADER or data[:4] != MAGIC:
        raise PluginError('not an .opmod')
    fmt, hsize, size, crc = struct.unpack_from('<HHII', data, 4)
    if fmt != FORMAT or hsize < HEADER:
        raise PluginError(f'format {fmt}, header {hsize}')
    if size != len(data):
        raise PluginError(f'file size {len(data)}, the header says {size}')
    blank = bytearray(data)
    struct.pack_into('<I', blank, 0x0C, 0)
    if zlib.crc32(bytes(blank)) & 0xFFFFFFFF != crc:
        raise PluginError('CRC mismatch: the file is damaged')
    w = struct.unpack_from('<20I', data, 0x10)
    man_off, man_len, img_off, img_len, zero, entry, imp_off, imp_n, rel_off, rel_n, res_off, res_n, \
        str_off, str_len, flags, _, exp_off, exp_n, pl_off, pl_n = w[:20]

    def inside(off, n, what):
        if off > size or n > size - off:
            raise PluginError(f'{what} outside the file')
    inside(man_off, man_len, 'the manifest')
    inside(img_off, img_len, 'the image')
    inside(imp_off, 4 * imp_n, 'the imports')
    inside(rel_off, 12 * rel_n, 'the relocations')
    inside(res_off, 16 * res_n, 'the resources')
    inside(str_off, str_len, 'the strings')
    inside(exp_off, 8 * exp_n, 'the exports')
    inside(pl_off, 24 * pl_n, 'the places')
    if not img_len and (flags & FLAG_CODE or zero or entry or imp_n or rel_n or exp_n or pl_n):
        raise PluginError('a code-free file (image size 0) with code\'s fields set')
    if img_len > MAX_IMAGE or zero > MAX_IMAGE or (img_len and (img_len < 4 or entry > img_len - 4 or entry & 1)):
        raise PluginError('image, zero fill or entry out of range')
    strings = data[str_off:str_off + str_len]

    def name(off):
        if off >= len(strings) or b'\0' not in strings[off:]:
            raise PluginError('a name outside the strings')
        return strings[off:strings.index(b'\0', off)].decode('ascii')
    imports = [name(struct.unpack_from('<I', data, imp_off + 4 * i)[0]) for i in range(imp_n)]
    relocs = []
    for i in range(rel_n):
        word, target, value = struct.unpack_from('<IIi', data, rel_off + 12 * i)
        site, kind, imported = word & 0x0FFFFFFF, (word >> 28) & 3, bool(word >> 31)
        width = 4 if kind == 0 else 2
        if site + width > img_len or (kind == 3 and site < 2):
            raise PluginError(f'relocation {i} outside the image')
        if imported and target >= imp_n:
            raise PluginError(f'relocation {i}: no import {target}')
        if not imported and not 0 <= value <= img_len + zero:
            raise PluginError(f'relocation {i} points outside the image')
        relocs.append((site, KIND_NAMES[kind], imports[target] if imported else None, value))
    exports = []
    for i in range(exp_n):
        noff, off = struct.unpack_from('<II', data, exp_off + 8 * i)
        if off >= img_len + zero:
            raise PluginError(f'export {i} outside the image')
        exports.append((name(noff), off))
    places = []
    for i in range(pl_n):
        kl, addr, target, noff = struct.unpack_from('<IIII', data, pl_off + 24 * i)
        kind, sw, n = kl & 0x7F, bool(kl & PLACE_SWITCHED), kl >> 8
        if kind not in PLACE_NAMES or not 2 <= n <= 8 or target >= img_len + zero or addr % 2:
            raise PluginError(f'place {i}: damaged')
        if PLACE_NAMES[kind] == 'patch' and (n % 2 or target + n > img_len):   # loader.c: its bytes, set ones
            raise PluginError(f'place {i}: damaged (a patch\'s bytes outside the image)')
        if sw and (PLACE_NAMES[kind] in ('jump', 'ldimm') or n > 4):
            raise PluginError(f'place {i}: damaged (switched)')
        places.append((PLACE_NAMES[kind], addr, target, data[pl_off + 24 * i + 16:pl_off + 24 * i + 16 + n], name(noff),
                       sw))
    manifest = data[man_off:man_off + man_len].decode('utf-8')
    return {'manifest': read_manifest_built(manifest), 'text': manifest, 'image': data[img_off:img_off + img_len],
            'zero': zero, 'entry': entry, 'imports': imports, 'relocs': relocs, 'exports': exports, 'flags': flags,
            'places': places, 'strings': str_len}


AUTHOR_END = '\n# built by op1mod plugin\n'


def read_manifest_built(text):
    """A built manifest: the author's lines and the build's (which read_manifest refuses from an author)."""
    out = {}
    for raw in text.splitlines():
        line = uncomment(raw)
        if line:
            key, _, value = line.partition(' ')
            if key in REPEATED:
                out.setdefault(key, []).append(value.strip())
            else:
                out[key] = value.strip()
    return out


# ---------------------------------------------------------------- a set of files, as a start would load them

CORE = os.path.join(REPO, 'mods', 'opus2')
LDR = os.path.join(REPO, 'firmware', 'OP1_vdk.ldr')
FAR_POOL = 0x00400000                      # loader.c POOL_LO..POOL_HI, shared with opus_alloc
FX_PLACE_RE = re.compile(r'\{\s*(0x[0-9A-Fa-f]+),\s*P_(CALL|JUMP|LDIMM|WORD),\s*(\d+),\s*\(u32\)\w+,\s*\{([^}]*)\}\s*\}')
PAGE_PLACE_RE = re.compile(r'\{\s*(0x[0-9A-Fa-f]+),\s*(\d+),\s*\(u32\)fxhost_\w+,\s*\{([^}]*)\}\s*\}')   # fxhost.c: jumps


def core_facts(core=CORE):
    """What the core's sources allow a start: its version (loader.c CORE_MAJOR, CORE_MINOR), its names (CORE_NAMES),
    the plugins at once (opus.h OPUS_PLUGINS), the effect service's places (effects.c PLACES), the instance service's
    page places (fxhost.c PLACES, 2026-10-25) and the firmware bytes its own hooks change (every .hooks file)."""
    import hooks

    def text(name):
        with open(os.path.join(core, name), encoding='utf-8') as f:
            return f.read()
    loader, opus_h, effects, fxhost = text('loader.c'), text('opus.h'), text('effects.c'), text('fxhost.c')
    table = loader[loader.index('CORE_NAMES[] = {'):]
    names = set(re.findall(r'NAME\((\w+)\)', table[:table.index('};')]))
    fx = [(k.lower(), int(a, 16), bytes(int(b, 16) for b in want.split(',')))
          for a, k, n, want in FX_PLACE_RE.findall(effects)]
    pages = [('jump', int(a, 16), bytes(int(b, 16) for b in want.split(',')))
             for a, n, want in PAGE_PLACE_RE.findall(fxhost)]
    held = {}
    for owner, path in hooks.hook_files(core):
        for _, kind, addr, what, expect, _ in hooks.lines(path):
            new = bytes.fromhex(what) if kind == 'patch' else None
            for k in range(len(expect)):
                if new is None or new[k] != expect[k]:
                    held[addr + k] = f'the core ({owner}.hooks)'
    return {'core': (int(re.search(r'#define CORE_MAJOR\s+(\d+)', loader).group(1)),
                     int(re.search(r'#define CORE_MINOR\s+(\d+)', loader).group(1))),
            'names': names, 'plugins': int(re.search(r'#define OPUS_PLUGINS\s+(\d+)', opus_h).group(1)),
            'fx': fx, 'pages': pages, 'held': held}


def written(kind, addr, n=4):
    """The bytes a place's install changes (loader.c opus_place_write): a branch's or a word's 4, an address load's
    two immediates."""
    return ([addr + 2, addr + 3, addr + 6, addr + 7] if kind == 'ldimm' else
            [addr + k for k in range(n if kind == 'patch' else 4)])


def _version(text, parts=3):
    v = [int(x) for x in text.split('.')]
    return tuple(v + [0] * (parts - len(v)))


def _uses(lines):
    """`uses` lines -> [(input, shift 0/1/2, screen 0-4, modal, fn 0/1/2, with: a held key or '')] as loader.c's
    use_line reads them."""
    out = []
    for use in lines:
        if use == 'none':
            continue
        words = use.split('"', 1)[0].split()
        held = words[words.index('with') + 1] if 'with' in words[1:-1] else ''
        if held:
            k = words.index('with')
            words = words[:k] + words[k + 2:]
        shift = 1 if 'shift' in words else 2 if 'noshift' in words else 0
        screen = next((USE_SCREENS.index(w) + 1 for w in words[1:] if w in USE_SCREENS), 0)
        fn = 1 if 'fn' in words[1:] else 2 if 'nofn' in words else 0
        page = next((USE_PAGES.index(w) + 1 for w in words[1:] if w in USE_PAGES), 0)
        out.append((words[0], shift, screen, 'modal' in words, fn, held, page))
    return out


def _re_path():
    """tools/re on the path (op1re: the LDR's memory)."""
    import sys
    here = os.path.join(REPO, 'tools', 're')
    if here not in sys.path:
        sys.path.insert(0, here)


DAMAGE = (('not an .opmod', 'not an opmod file'), ('format', 'a newer file format'), ('file size', 'damaged: its size'),
          ('CRC', 'damaged: its crc'), ('image, zero', 'damaged: its image'), ('a name outside', 'damaged: its names'),
          ('relocation', 'damaged: a relocation'), ('export', 'damaged: an export'), ('place', 'damaged: a place'))


def damage_words(message):
    """read_opmod's refusal in the core's words (loader.c check); a manifest's in the build's."""
    for start, words in DAMAGE:
        if message.startswith(start):
            return words
    return 'damaged: a table outside the file' if 'outside the file' in message else message


def check_set(paths, ldr=LDR, core=CORE):
    """Would these .opmod files load together? The core's start in Python (loader.c load_all, load, uses_clash,
    places_check; effects.c start): the files in their names' order (the first OPUS_PLUGINS), each read and checked,
    its core version, one id twice (the newer loads), libraries before the plugins that need them, a control a
    plugin loaded before takes, the imports, the room, each place against the firmware as the core and the plugins
    loaded before left it, the effect service's places when a plugin imports opus_effect_register or opus_fx_manager,
    and the instance service's page places with the first manager.
    -> [{file, id, version, state: loads | refused | not listed, why, notes}] in the mods list's order. Not known
    before a start: each init's result, opus_alloc's room, the skip marks and the safe start."""
    _re_path()
    import fwheader
    from op1re import load as load_ldr
    from op1re.image import flatten
    facts = core_facts(core)
    deep = {n for n, _ in fwheader.names()}
    img = load_ldr(ldr)

    def stock(addr, n):
        return bytes(flatten(img, addr, addr + n)[0])
    rows = []
    for p in sorted(paths, key=lambda p: os.path.basename(p).encode()):
        f = {'file': os.path.basename(p), 'id': None, 'version': None, 'state': None, 'why': None, 'notes': []}
        rows.append(f)
        if len(rows) > facts['plugins']:
            f['state'], f['why'] = 'not listed', f'over {facts["plugins"]} files'
            continue
        try:
            with open(p, 'rb') as h:
                info = read_opmod(h.read())
            man = read_manifest(info['text'].split(AUTHOR_END)[0])
        except OSError:
            f['state'], f['why'] = 'refused', 'cannot read its file'
            continue
        except PluginError as e:
            f['state'], f['why'] = 'refused', damage_words(str(e))
            continue
        f.update(id=man['id'], version=man['version'], name=man['name'], info=info, ver=_version(man['version']),
                 needs=[(w.split()[0], _version(w.split()[1], 2)) for w in man.get('needs', [])],
                 uses=_uses(man.get('uses', [])), checked=checked_of(man))
        core_ver = _version(man['core'], 2)
        if core_ver[0] != facts['core'][0] or core_ver[1] > facts['core'][1]:
            f['state'], f['why'] = 'refused', f'needs core {core_ver[0]}.{core_ver[1]}'
    live = [f for f in rows if f['state'] is None]
    for f in live:                         # one id twice: the newer version loads
        for g in live:
            if f is not g and f['state'] is None and g['state'] is None and f['id'] == g['id'] and \
                    (f['ver'] < g['ver'] or (f['ver'] == g['ver'] and rows.index(f) > rows.index(g))):
                f['state'], f['why'] = 'refused', 'an older copy'
    held = dict(facts['held'])             # a firmware byte -> who changed it
    exports, used = set(), [0, 0, 0]       # the libraries' names; the room taken, the effect service, the instance
                                           # service's pages (each 1 on, -1 not)

    def clash(f):
        for g in rows:
            if g is f or g['state'] != 'loads':
                continue
            for x in f['uses']:
                for y in g['uses']:
                    if x[3] or y[3] or x[0] != y[0] or (x[5] and y[5] and x[5] != y[5]) or (x[1] and y[1] and x[1] != y[1]) or \
                            (x[4] and y[4] and x[4] != y[4]) or (x[2] and y[2] and x[2] != y[2]) or \
                            (x[6] and y[6] and x[6] != y[6]):
                        continue
                    return (f'{"fn " if x[4] == 1 else ""}{"shift " if x[1] == 1 else ""}{x[0]} is taken by '
                            f'{g["name"]}').replace('_', ' ')
        return None

    def load(f):
        info = f['info']
        for n in info['imports']:
            if n not in exports and n not in facts['names'] and n not in deep:
                return f'needs {n}'
        need = len(info['image']) + info['zero'] + (info['strings'] if info['exports'] else 0)
        if used[0] + need > FAR_POOL:
            return 'not loaded: no room'
        for kind, addr, _, expect, name, _ in info['places']:
            mine = [a for a in range(addr, addr + len(expect)) if a in held]
            if mine:
                f['notes'].append(f'place {name}: 0x{mine[0]:08x} is held by {held[mine[0]]}')
                return f'place {name} is taken'
            there = stock(addr, len(expect))
            if there != expect:
                f['notes'].append(f'place {name}: the firmware holds {there.hex()} there, not {expect.hex()}')
                return f'place {name} is taken'
            if kind == 'ldimm':
                w0, w1 = struct.unpack_from('<H', expect)[0], struct.unpack_from('<H', expect, 4)[0]
                if (w0 & 0xFFC0) != 0xE100 or (w1 & 0xFFC0) != 0xE140 or (w0 & 0x3F) != (w1 & 0x3F):
                    return f'place {name} is no address load'
        used[0] += need
        if ({'opus_effect_register', 'opus_fx_manager'} & set(info['imports'])) and not used[1]:   # its init starts the effect service
            for kind, addr, want in facts['fx']:
                at = [a for a in range(addr, addr + len(want)) if a in held]
                if at or stock(addr, len(want)) != want:
                    f['notes'].append(f'the effect service would give -4: the effect place 0x{addr:08x} is taken'
                                      + (f' by {held[at[0]]}' if at else ''))
                    used[1] = -1
                    break
            else:
                used[1] = 1
                for kind, addr, _ in facts['fx']:
                    for a in written(kind, addr):
                        held[a] = 'the effect service'
        if 'opus_fx_manager' in info['imports'] and used[1] > 0 and not used[2]:   # the first manager: the pages
            for kind, addr, want in facts['pages']:
                at = [a for a in range(addr, addr + len(want)) if a in held]
                if at or stock(addr, len(want)) != want:
                    f['notes'].append(f'the instance service would give -4: its page place 0x{addr:08x} is taken'
                                      + (f' by {held[at[0]]}' if at else ''))
                    used[2] = -1
                    break
            else:
                used[2] = 1
                for kind, addr, want in facts['pages']:
                    for a in written(kind, addr, len(want)):
                        held[a] = 'the instance service'
        for kind, addr, _, expect, name, _ in info['places']:   # installed after its init (switched: held while stock)
            over = [a for a in written(kind, addr, len(expect)) if held.get(a) == 'the effect service']
            if over:
                f['notes'].append(f"place {name} writes over the effect service's place at 0x{over[0]:08x}")
            for a in written(kind, addr, len(expect)):
                held[a] = f'{f["id"]} ({name})'
        exports.update(n for n, _ in info['exports'])
        return None

    progress = True
    while progress:                        # libraries before the plugins that need them
        progress = False
        for f in rows:
            if f['state'] is not None:
                continue
            ready = True
            for nid, nver in f['needs']:
                ok = pending = False
                for g in rows:
                    if g['id'] == nid:
                        if g['state'] is None:
                            pending = True
                        elif g['state'] == 'loads' and g['ver'][:2] >= nver:
                            ok = True
                if not ok:
                    ready = False
                    if not pending:
                        f['state'], f['why'] = 'refused', f'needs {nid} {nver[0]}.{nver[1]}'
                        progress = True
                    break
            if ready and f['state'] is None:
                why = clash(f) or load(f)
                f['state'], f['why'] = ('refused', why) if why else ('loads', None)
                progress = True
    for f in rows:                         # a cycle of needs
        if f['state'] is None:
            f['state'], f['why'] = 'refused', 'needs a plugin that needs it'
    for f in rows:
        f.pop('info', None)
        if f['why']:
            f['why'] = f['why'].lower()    # as the mods list shows it (loader.c row_set)
    return rows


# ---------------------------------------------------------------- a new plugin's folder

NEW_MANIFEST = """# @ID@: what it does, in a line.
opmod     1
id        @ID@
name      @NAME@
author    @AUTHOR@
version   0.1.0
core      2.0
setting   level 0 16 8 "level"
uses      green_turn modal "its level, on its panel"
"""

NEW_SOURCE = """/* @ID@: a new Opus 2 plugin (written by `plugin new`; the core's names in opus_plugin.h, the firmware's in
 * op1fw.h). It shows the pieces most plugins use: a setting the user changes on the mods list's settings
 * page (blue push), its own panel on the mods list's right, the green turn while its row is selected, and a line in
 * the boot log. See README.md beside it for building, checking, testing and putting it on the OP-1. */
#include "opus_plugin.h"
#include "op1fw.h"

static struct opl_self *me;
static int level;
static char number[8];

/* Its panel (the display thread): drawn when the mods list shows its row, and twice a second. */
static void draw(void)
{
    opus_panel_text(me, 0, 172, 30, 9, OPUS_COLOR_DIM, "level");
    fw_snprintf(number, sizeof number, "%i", level);
    opus_panel_text(me, 1, 172, 42, 18, OPUS_COLOR_E2, number);
    opus_panel_bar(172, 72, 300, 80, (unsigned)level, 16, OPUS_COLOR_E2);
}

/* The green turn while its row is selected (encoder 1; steps, + clockwise): 1 to have the panel drawn again. */
static int knob(int encoder, int steps)
{
    if (encoder != 1)
        return 0;
    level += steps;
    level = level < 0 ? 0 : level > 16 ? 16 : level;
    return 1;
}

/* The settings page changed its setting (the KeyHandler thread). */
static void setting(const char *name, int value)
{
    (void)name;
    level = value;
}

int opl_init(struct opl_self *self)
{
    me = self;
    level = opus_setting(self, "level");
    opus_on_setting(self, setting);
    opus_on_panel(self, draw);
    opus_on_panel_knob(self, knob);
    opus_log(self, "started");
    return 0;
}
"""

NEW_TEST = NEW_README = None              # the repository's own, below (the authors' kit has KIT_README)

KIT_README = """# @ID@

A new Opus 2 plugin, written by `python plugin_tool.py new @ID@`. Commands from the kit's folder:

- **Build:** `python plugin_tool.py build @REL@` (writes build/@FILE@.opmod)
- **Read it back as the core would:** `python plugin_tool.py show build/@FILE@.opmod`
- **On the OP-1:** connect it in disk mode (shift + COM, then T3), copy the .opmod into the disk's mods folder,
  eject, and switch the OP-1 off and on. Open the mods list (hold synth, drum, tape and mixer, then T2): its row
  shows "loaded" or why not; blue push its settings, green push its controls, orange push its details.
- **When it works,** record where: `checked device DATE what you ran` in manifest.txt (`show` prints it).

What it can call: docs/api.md; its manifest's lines: docs/manifest.md; the rules every plugin keeps: docs/rules.md.
"""


def new(plugin_id, where=None, kit=False):
    """`op1mod plugin new ID`: a working plugin folder (manifest.txt, a source, test.txt, README.md) at `where`
    (default the repository's plugins folder, ID) -> the folder. Refused when the id is not author.plugin or the folder exists.
    kit: the authors' kit's (plugin_tool.py): its README, no emulator test (the kit has no emulator yet)."""
    if not ID_RE.match(plugin_id) or len(plugin_id) > 47:
        raise PluginError(f'{plugin_id!r}: an id is author.plugin, a-z 0-9 _ and dots, up to 47 characters')
    where = where or os.path.join(REPO, 'mods', 'opus2', 'plugins', plugin_id)
    if os.path.exists(where):
        raise PluginError(f'{where} exists')
    author, last = plugin_id.split('.')[0], plugin_id.split('.')[-1]
    try:
        rel = os.path.relpath(where, REPO).replace(os.sep, '/')
    except ValueError:                     # another drive: the test's put takes the whole path
        rel = os.path.abspath(where).replace(os.sep, '/')
    words = {'@ID@': plugin_id, '@NAME@': last.replace('_', ' ').capitalize()[:31], '@AUTHOR@': author,
             '@FILE@': last, '@REL@': rel, '@ABS@': os.path.join(os.path.abspath(where), 'test.txt').replace(os.sep, '/')}
    os.makedirs(where)
    files = ((('manifest.txt', NEW_MANIFEST), (f'{last}.c', NEW_SOURCE), ('README.md', KIT_README)) if kit else
             (('manifest.txt', NEW_MANIFEST), (f'{last}.c', NEW_SOURCE), ('test.txt', NEW_TEST),
              ('README.md', NEW_README)))
    for name, text in files:
        for k, v in words.items():
            text = text.replace(k, v)
        with open(os.path.join(where, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    return where
