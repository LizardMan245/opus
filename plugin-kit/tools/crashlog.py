"""crashlog -- an Opus 2 crash log, read.

    python tools/crashlog.py N.txt [--plugin ID=build/NAME.build/plugin_0.elf ...]

After a crash, the OP-1's next start writes a crash log; disk mode shows it in the crash folder on the USB disk
(/crash/N.txt, the last eight). Its first lines are readable as they are; after them comes the whole crash record in
hexadecimal, which this reads: what stopped the OP-1, how long after the start, where the processor was, the frame
chain and the stack's words that look like return addresses, and the plugins that were loaded. An address inside a
plugin shows as its id and offset; with --plugin, your own build's linked file names the function too (the plugin
kit's build keeps it as build/NAME.build/plugin_0.elf). Code built by GCC keeps no frame chain, so the chain can end
early: the stack's words then show where the calls came from. Standard library only.
"""
import argparse
import struct
import sys
import zlib

MAGIC = 0x5243504F                         # "OPCR"
VERSION = 1
FRAMES, PLUGINS, STACK = 32, 30, 256
FIELDS = ('count', 'state', 'how', 'uptime_ms', 'cycles_lo', 'cycles_hi', 'kind', 'seqstat', 'retx', 'reti', 'rets',
          'ipend', 'ilat', 'imask', 'dcplb_status', 'dcplb_addr', 'icplb_status', 'icplb_addr', 'sp', 'fp', 'thread',
          'screen', 'pool_used', 'menu', 'nframes')
TALLY = ('passes', 'failed', 'cleared', 'ms')
SIZE_1 = 16 + 4 * len(FIELDS) + 4 * FRAMES + 4 + PLUGINS * 60 + 32 + 256 + 4 * STACK
SIZES = (SIZE_1, SIZE_1 + 12, SIZE_1 + 16)
KINDS = {1: "an assert (the OS's fatal message)", 2: 'an exception', 3: 'a freeze (no audio block for 2 s)'}
HOWS = {0: 'nothing yet', 1: 'stop', 2: 'the power switch', 3: "the screen's guard (the screen stopped)",
        4: 'the screen (no key chip answered for 10 s)'}
EXCAUSE = {0x21: 'undefined instruction', 0x22: 'illegal instruction combination',
           0x23: 'data access CPLB protection violation', 0x24: 'data access misaligned',
           0x25: 'unrecoverable event', 0x26: 'data access CPLB miss', 0x27: 'data access multiple CPLB hits',
           0x28: 'emulation watchpoint', 0x2A: 'instruction fetch misaligned', 0x2B: 'instruction fetch CPLB protection',
           0x2C: 'instruction fetch CPLB miss', 0x2D: 'instruction fetch multiple CPLB hits',
           0x2E: 'illegal use of a supervisor resource'}
LOG_MARK = b'-- the crash record, for crashlog.py --'   # the log's line before the record
OS_CODE = (0x0191209C, 0x01AA4A4A)         # the OP-1 firmware's own code
CORE = (0x01E00000, 0x01F00000)            # Opus's core (plugins with patches can load there too)
L1_CODE = (0xFFA00000, 0xFFA14000)
PLUGIN_ELF_BASE = 0x01E80000               # where the kit's build links build/NAME.build/plugin_0.elf


class RecordError(Exception):
    pass


def record_bytes(data):
    """A crash log's record (the hexadecimal after its mark line), or the bytes as they are (a raw record)."""
    at = data.find(LOG_MARK)
    if at < 0:
        return data
    return bytes.fromhex(data[at + len(LOG_MARK):].decode('ascii', 'replace').replace('\r', '').replace('\n', ''))


def _cstr(b):
    return b.split(b'\0')[0].decode('latin-1')


def parse(data):
    """The record's bytes -> a dict; RecordError when it is not a whole, sound record."""
    if len(data) < 16:
        raise RecordError('no crash record in it')
    magic, version, size, crc = struct.unpack_from('<4I', data)
    if magic != MAGIC:
        raise RecordError('no crash record in it')
    if version != VERSION or size not in SIZES:
        raise RecordError(f'a record of version {version}, {size} bytes: this reads version {VERSION}')
    if len(data) < size:
        raise RecordError(f'{len(data)} of its {size} bytes: cut short')
    if zlib.crc32(data[16:size]) & 0xFFFFFFFF != crc:
        raise RecordError('its CRC does not match: damaged')
    r = dict(zip(FIELDS, struct.unpack_from(f'<{len(FIELDS)}I', data, 16)))
    at = 16 + 4 * len(FIELDS)
    r['frames'] = list(struct.unpack_from(f'<{FRAMES}I', data, at))[:min(r['nframes'], FRAMES)]
    at += 4 * FRAMES
    n = struct.unpack_from('<I', data, at)[0]
    at += 4
    r['plugins'] = []
    for i in range(min(n, PLUGINS)):
        p = at + 60 * i
        crc_, base, psize = struct.unpack_from('<3I', data, p + 48)
        r['plugins'].append({'id': _cstr(data[p:p + 48]), 'crc': crc_, 'base': base, 'size': psize})
    at += 60 * PLUGINS
    r['build'] = _cstr(data[at:at + 32])
    r['text'] = _cstr(data[at + 32:at + 288])
    r['stack'] = list(struct.unpack_from(f'<{STACK}I', data, at + 288))
    n = (size - SIZE_1) // 4
    r.update(zip(TALLY[:n], struct.unpack_from(f'<{n}I', data, SIZE_1)))
    return r


def elf_functions(path):
    """An ELF's functions: [(address, size, name)], from its symbol table."""
    data = open(path, 'rb').read()
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<3H', data, 0x2E)
    secs = [struct.unpack_from('<10I', data, shoff + i * shentsize) for i in range(shnum)]

    def cstr(at):
        return data[at:data.index(b'\0', at)].decode('latin-1')
    funcs = []
    for s in secs:
        if s[1] == 2:                      # SHT_SYMTAB
            strtab = secs[s[6]][4]
            for k in range(s[5] // 16):
                name_at, value, size, info = struct.unpack_from('<3IB', data, s[4] + 16 * k)
                if info & 0xF == 2:        # STT_FUNC
                    name = cstr(strtab + name_at)
                    funcs.append((value, size, name[1:] if name.startswith('_') else name))
    return sorted(set(funcs))


def where(a, plugins, named):
    for p in plugins:
        if p['base'] <= a < p['base'] + p['size']:
            off = a - p['base']
            for s, size, name in named.get(p['id'], ()):
                if s <= off < s + size:
                    return f"plugin {p['id']}+0x{off:x}: {name}+0x{off - s:x}"
            return f"plugin {p['id']}+0x{off:x}"
    if CORE[0] <= a < CORE[1]:
        return "Opus's core"
    if L1_CODE[0] <= a < L1_CODE[1]:
        return "the OP-1 firmware's L1 code"
    if OS_CODE[0] <= a < OS_CODE[1]:
        return 'the OP-1 firmware'
    return ''


def code_like(a, plugins):
    return (OS_CODE[0] <= a < OS_CODE[1] or CORE[0] <= a < CORE[1] or L1_CODE[0] <= a < L1_CODE[1]
            or any(p['base'] <= a < p['base'] + p['size'] for p in plugins)) and not a & 1


def report(r, named=None):
    named = named or {}
    w = lambda a: where(a, r['plugins'], named)   # noqa: E731
    up = r['uptime_ms']
    out = [f"Opus crash record {r['count']} ({r['build']}): {KINDS.get(r['kind'], r['kind'])}, "
           f"{up // 3600000}:{up // 60000 % 60:02d}:{up // 1000 % 60:02d} after the start; restarted by "
           f"{HOWS.get(r['how'], r['how'])}", '']
    out += ['  ' + line for line in r['text'].split('\n')]
    out.append('')
    if r['kind'] == 2:
        cause = r['seqstat'] & 0x3F
        out.append(f"seqstat 0x{r['seqstat']:x}: EXCAUSE 0x{cause:x} {EXCAUSE.get(cause, '')}")
        out.append(f"retx    0x{r['retx']:08x}  {w(r['retx'])}   (the instruction that faulted)")
        out.append(f"dcplb   status 0x{r['dcplb_status']:08x} fault address 0x{r['dcplb_addr']:08x}")
        out.append(f"icplb   status 0x{r['icplb_status']:08x} fault address 0x{r['icplb_addr']:08x}")
    if r['kind'] == 3:
        out.append(f"retn    0x{r['retx']:08x}  {w(r['retx'])}   (where the processor was)")
    out.append(f"reti    0x{r['reti']:08x}  {w(r['reti'])}")
    out.append(f"sp 0x{r['sp']:08x} fp 0x{r['fp']:08x}; the screen 0x{r['screen']:x}; "
               f"Opus's menu page {r['menu'] & 0xFF}")
    out.append('')
    out.append('frames (return addresses up the frame chain):')
    for i, a in enumerate(r['frames']):
        out.append(f'  {i:2} 0x{a:08x}  ' + (w(a) if code_like(a, r['plugins']) else
                                             '(not code: the chain ends here)'))
    out.append('')
    out.append('the stack from sp: words that look like return addresses')
    for i, v in enumerate(r['stack']):
        if code_like(v, r['plugins']):
            out.append(f"  sp+0x{4 * i:03x} 0x{v:08x}  {w(v)}")
    out.append('')
    out.append('plugins loaded:' if r['plugins'] else 'plugins loaded: none')
    for p in r['plugins']:
        out.append(f"  {p['id']} at 0x{p['base']:08x}, {p['size']} bytes, file crc 0x{p['crc']:08x}")
    return '\n'.join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log', help="a crash log from the OP-1's USB disk (/crash/N.txt)")
    ap.add_argument('--plugin', action='append', default=[], metavar='ID=ELF',
                    help="your plugin's id and its build's linked file, build/NAME.build/plugin_0.elf: its "
                         "functions named (more than one allowed)")
    a = ap.parse_args(argv)
    named = {}
    for spec in a.plugin:
        pid, _, path = spec.partition('=')
        if not path:
            ap.error(f'--plugin {spec}: ID=ELF')
        named[pid] = [(s - PLUGIN_ELF_BASE, size, name) for s, size, name in elf_functions(path)]
    try:
        r = parse(record_bytes(open(a.log, 'rb').read()))
    except RecordError as e:
        print(f'{a.log}: {e}')
        return 1
    print(report(r, named))
    return 0


if __name__ == '__main__':
    sys.exit(main())
