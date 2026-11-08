"""Opus 2's plugin image: the relocations from a `ld -q` link and the loader's way of applying them
(stage B, 2026-10-15).

A plugin is linked once with `ld -q` (--emit-relocs) at a base inside the pool, its imports given as
absolute symbols (--defsym NAME=ADDR), into one output section holding code and initialised data
(`.image`) and one of zeros (`.zero`).  The relocations kept in the ELF say where the image holds an
address; this module turns them into the contract's table and applies it as the core's loader will.
The table is checked by a second link at another base (`check_two_bases`): relocating the first image
to the second base must give the second image byte for byte (two methods).

The kinds (bfin binutils 2.38; the r_offset of every kind is the bytes it changes, in the image):

  word  R_BFIN_BYTE4_DATA (0x12)  a 32-bit word: tables of pointers, vtables, a struct's pointer field
  lo    R_BFIN_LUIMM16 (0x06)     the 16-bit immediate of `Rx.L = imm` (the instruction starts 2 before)
  hi    R_BFIN_HUIMM16 (0x07)     the 16-bit immediate of `Rx.H = imm`
  call  R_BFIN_PCREL24 (0x0A) and the other 24-bit branches: CALL or JUMP.L to an import; r_offset is
        the instruction's second half-word, the offset's high 8 bits are the low byte of the first

Branches inside the image are relative and need nothing; a branch from the image to an import moves
with the image, so it is the one relative kind the loader writes.  Anything else that points into
the image or at an import (a 16-bit sign-extended immediate holding an address, a 2-byte data word) is
refused: the image could not be moved.

The zero fill apart (2026-11-02, dev 105): an address whose symbol lies in `.zero` is marked
(the name ZERO, bit 30 in the file), so the loader may put the zero fill somewhere else than right after the image
(a plugin near the OS's code for its places, its big buffer in the far pool). Its value stays the offset as if the
zero fill followed the image (the image's size, the alignment's padding, the offset in `.zero`), so a loader that
keeps them together needs nothing new. The mark follows the symbol's section, never the address it points at: the
end of an array that is the image's last (`p < arr + N`) is the zero fill's first byte. The second link puts `.zero`
ZERO_GAP further than the image moves, so a mark missing or wrong fails the byte-for-byte check.
"""

import struct

KIND = {0x12: 'word', 0x06: 'lo', 0x07: 'hi', 0x0A: 'call', 0x09: 'call', 0x0D: 'call', 0x0E: 'call'}
PCREL = {0x01, 0x03, 0x04, 0x08, 0x09, 0x0A, 0x0D, 0x0E, 0x13}   # relative: nothing when inside the image
SHN_ABS = 0xFFF1
ZERO = ''                  # a relocation's name when its target lies in the zero fill: falsy, so a reader that only
                           # knows imports and own addresses takes it as an own address (right while they are together)
ZERO_GAP = 0x00351000      # the second link's .zero beyond where it would follow the image: both halves differ


class OpmodError(Exception):
    pass


def _elf(data):
    """-> (sections [{name, type, addr, offset, size, link, info, entsize}], symbols [(name, value, shndx)])."""
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        raise OpmodError('not a 32-bit little-endian ELF')
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x2E)
    secs = []
    for i in range(shnum):
        name, typ, _, addr, off, size, link, info, align, entsize = struct.unpack_from('<10I', data,
                                                                                       shoff + i * shentsize)
        secs.append({'name': name, 'type': typ, 'addr': addr, 'offset': off, 'size': size, 'link': link,
                     'info': info, 'align': align, 'entsize': entsize})
    strtab = secs[shstrndx]

    def cstr(sec, off):
        start = sec['offset'] + off
        return data[start:data.index(b'\0', start)].decode()
    for s in secs:
        s['name'] = cstr(strtab, s['name'])
    syms = []
    for s in secs:
        if s['type'] == 2:                                   # SHT_SYMTAB
            names = secs[s['link']]
            for k in range(s['size'] // 16):
                name, value, _, _, _, shndx = struct.unpack_from('<IIIBBH', data, s['offset'] + k * 16)
                syms.append((cstr(names, name), value, shndx))
    return secs, syms


def _zero_layout(byname, base, size):
    """-> (zero, pad, the .zero section or None): the zero fill's bytes as the file gives them (the alignment's padding
    after the image, then .zero; an aligned buffer at an odd address hung the OS, so the loader zeroes the
    padding too), counted as if .zero followed the image at an aligned base, whichever link this is."""
    zsec = byname.get('.zero')
    if not zsec or not zsec['size']:
        return 0, 0, None
    align = max(zsec['align'], 1)
    pad = -size % align
    return pad + zsec['size'], pad, zsec


def zero_at(elf_path):
    """-> where the zero fill's offset 0 (the image's size in the file's terms) lies in this link: the address a
    loader keeping them apart gives relocate() as zero_at."""
    with open(elf_path, 'rb') as f:
        data = f.read()
    secs, _ = _elf(data)
    byname = {s['name']: s for s in secs}
    img = byname['.image']
    _, pad, zsec = _zero_layout(byname, img['addr'], img['size'])
    return zsec['addr'] - pad if zsec else img['addr'] + img['size']


def relocations(elf_path, section='.image'):
    """-> (base, image bytes, zero fill, [(site, kind, name, value)]): name an import's, ZERO for an address in the
    zero fill or None for one in the image; value the offset an own address points at (addend included; in the zero
    fill as if it followed the image), or an import's addend."""
    with open(elf_path, 'rb') as f:
        data = f.read()
    secs, syms = _elf(data)
    byname = {s['name']: s for s in secs}
    img = byname.get(section)
    if img is None:
        raise OpmodError(f'{elf_path}: no {section} section')
    base, size = img['addr'], img['size']
    image = data[img['offset']:img['offset'] + size]
    zero, pad, zsec = _zero_layout(byname, base, size)
    zi = secs.index(zsec) if zsec else -1
    zlo = zsec['addr'] - pad if zsec else base + size      # the zero fill's start in this link
    end = base + size + zero                               # the file's end of image and zero fill together
    out = []
    for s in secs:
        if s['type'] != 4 or secs[s['info']] is not img:     # SHT_RELA for the image
            continue
        for k in range(s['size'] // 12):
            r_offset, r_info, addend = struct.unpack_from('<IIi', data, s['offset'] + k * 12)
            typ, symi = r_info & 0xFF, r_info >> 8
            name, value, shndx = syms[symi]
            target = value + addend
            in_zero = shndx == zi
            imported = shndx == SHN_ABS and not (base <= target < base + size or zlo <= target < zlo + zero)
            site = r_offset - base
            if not 0 <= site < size:
                raise OpmodError(f'relocation at 0x{r_offset:08x} outside the image')
            if in_zero and (typ in PCREL or KIND.get(typ) == 'call'):
                raise OpmodError(f'image+0x{site:x}: a relative reference into the zero fill ({name}): '
                                 'it could not lie apart from the image')
            if typ in PCREL and not imported:
                continue                                     # inside the image: moves with it
            kind = KIND.get(typ)
            if kind is None:
                raise OpmodError(f'relocation type 0x{typ:02x} at image+0x{site:x} ({name}): cannot be moved')
            if kind == 'call' and not imported:
                continue
            own = size + (target - zlo) if in_zero else target - base   # the file's offset (together)
            if imported:
                out.append((site, kind, name.lstrip('_'), addend))
            elif 0 <= own <= size + zero:
                out.append((site, kind, ZERO if in_zero else None, own))
            else:
                raise OpmodError(f'image+0x{site:x} points at 0x{target:08x}, outside the image ({name}): '
                                 'an address the build did not give as an import')
    if zsec and zsec['addr'] - (base + size) not in (pad, pad + ZERO_GAP):   # the first link's, the second's
        raise OpmodError(f'{elf_path}: .zero at 0x{zsec["addr"]:08x}, not where its alignment '
                         f'({zsec["align"]}) puts it after the image (over 64 bytes: not yet)')
    return base, image, zero, sorted(out, key=lambda r: (r[0], r[1], r[2] or '', r[3]))


def relocate(image, base, relocs, imports, zero_at=None):
    """The loader: the image as it must stand at `base`, every import at imports[name]; the zero fill at zero_at
    (its offset 0, the image's size in the file's terms), or right after the image when None."""
    b = bytearray(image)
    zdelta = (zero_at - len(image)) if zero_at is not None else base
    for site, kind, name, value in relocs:
        full = (imports[name] if name else zdelta if name == ZERO else base) + value
        if kind == 'word':
            struct.pack_into('<I', b, site, full & 0xFFFFFFFF)
        elif kind == 'lo':
            struct.pack_into('<H', b, site, full & 0xFFFF)
        elif kind == 'hi':
            struct.pack_into('<H', b, site, (full >> 16) & 0xFFFF)
        else:                                                # call: the instruction starts at site - 2
            insn = base + site - 2
            off = (full - insn) // 2
            if (full - insn) % 2 or not -(1 << 23) <= off < (1 << 23):
                raise OpmodError(f'image+0x{site:x}: 0x{full:08x} beyond a branch\'s reach from 0x{insn:08x}')
            off &= 0xFFFFFF
            b[site - 2] = off >> 16
            struct.pack_into('<H', b, site, off & 0xFFFF)
    return bytes(b)


def check_two_bases(elf_a, elf_b, imports):
    """Two links at two bases (imports: the second link's import addresses, which may differ from the
    first's): the same table, and the first image relocated to the second base equal to
    the second image.  -> (relocs, a summary line); OpmodError on any difference."""
    base_a, img_a, zero_a, rel_a = relocations(elf_a)
    base_b, img_b, zero_b, rel_b = relocations(elf_b)
    if base_a == base_b:
        raise OpmodError('both links at one base')
    if (len(img_a), zero_a, rel_a) != (len(img_b), zero_b, rel_b):
        raise OpmodError('the two links differ in size or relocations')
    moved = relocate(img_a, base_b, rel_a, imports, zero_at(elf_b))   # the second's zero fill apart (ZERO_GAP)
    if moved != img_b:
        first = next(i for i in range(len(img_b)) if moved[i] != img_b[i])
        raise OpmodError(f'relocated image differs from the second link at image+0x{first:x}')
    covered = set()
    for site, kind, _, _ in rel_a:
        covered.update(range(site - 2, site + 2) if kind == 'call' else range(site, site + (4 if kind == 'word' else 2)))
    stray = [i for i in range(len(img_a)) if img_a[i] != img_b[i] and i not in covered]
    if stray:
        raise OpmodError(f'the two links differ outside every relocation, first at image+0x{stray[0]:x}')
    kinds = {}
    for _, kind, name, _ in rel_a:
        key = kind + (' import' if name else ' zero' if name == ZERO else '')
        kinds[key] = kinds.get(key, 0) + 1
    return rel_a, (f'{len(img_a)} bytes, {zero_a} zero fill, {len(rel_a)} relocations ('
                   + ', '.join(f'{n} {k}' for k, n in sorted(kinds.items())) + ')')


LINK_SCRIPT = """SECTIONS
{
    . = 0x%08X;
    .image : { *(.text .text.*) *(.rodata .rodata.*) *(.data .data.*) }
    . = . + 0x%08X;
    .zero (NOLOAD) : { *(.bss .bss.*) *(COMMON) *(.far) }
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note.*) *(.debug*) }
}
"""
# Two bases and two sets of import addresses that differ in both halves, or a missing low-half
# relocation would go unseen (the bases first tried, 0x01E80000 and 0x01EC0000, hid one).
BASES = (0x01E80000, 0x01EC1240)


def import_address(n, i):
    """The address link n (0 or 1) gives import i."""
    return 0x01E10000 + (n + 1) * 0x10000 + n * 0x1234 + 4 * i


def link_two_bases(tc, objs, out_dir, tag):
    """Link a part's objects as a plugin at two bases, every symbol they leave undefined (and libgcc
    does not define) an import at two different addresses; check_two_bases on the pair.
    -> (imports, relocs, summary); OpmodError when a section cannot go into a plugin (L1 code) or the
    two links disagree."""
    import os
    import subprocess
    from build import tool

    def run(cmd):
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode:
            raise OpmodError(p.stderr.strip().splitlines()[-1] if p.stderr.strip() else f'{cmd[0]} failed')
        return p.stdout
    libgcc = run([tool(tc, 'gcc'), '-mcpu=bf524-any', '-print-libgcc-file-name']).strip()
    defined, undefined, l1 = set(), set(), []
    for o in objs:
        for line in run([tool(tc, 'nm'), o]).splitlines():
            kind, name = line.split()[-2:]
            (undefined if kind == 'U' else defined).add(name)
        l1 += [f'{os.path.basename(o)} {s}' for s in run([tool(tc, 'objdump'), '-h', o]).split() if s.startswith('.l1')]
    if l1:
        raise OpmodError('L1 sections cannot go into a plugin yet: ' + ', '.join(l1))
    lib = {line.split()[-1] for line in run([tool(tc, 'nm'), libgcc]).splitlines()
           if len(line.split()) == 3 and line.split()[1] in 'TtDdRrBb'}
    imports = sorted(undefined - defined - lib)
    elfs = []
    for n, base in enumerate(BASES):
        script = os.path.join(out_dir, f'{tag}_{n}.ld')
        with open(script, 'w') as f:
            f.write(LINK_SCRIPT % (base, ZERO_GAP if n else 0))   # the second: .zero apart (the marks checked)
        elf = os.path.join(out_dir, f'{tag}_{n}.elf')
        syms = [f'--defsym={s}=0x{import_address(n, i):08X}' for i, s in enumerate(imports)]
        run([tool(tc, 'ld'), '-q', '--orphan-handling=error', '-T', script, '-o', elf] + syms + list(objs) + [libgcc])
        elfs.append(elf)
    second = {s.lstrip('_'): import_address(1, i) for i, s in enumerate(imports)}
    relocs, summary = check_two_bases(elfs[0], elfs[1], second)
    return [s.lstrip('_') for s in imports], relocs, summary
