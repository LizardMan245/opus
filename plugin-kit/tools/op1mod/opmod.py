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
"""

import struct

KIND = {0x12: 'word', 0x06: 'lo', 0x07: 'hi', 0x0A: 'call', 0x09: 'call', 0x0D: 'call', 0x0E: 'call'}
PCREL = {0x01, 0x03, 0x04, 0x08, 0x09, 0x0A, 0x0D, 0x0E, 0x13}   # relative: nothing when inside the image
SHN_ABS = 0xFFF1


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
        name, typ, _, addr, off, size, link, info, _, entsize = struct.unpack_from('<10I', data, shoff + i * shentsize)
        secs.append({'name': name, 'type': typ, 'addr': addr, 'offset': off, 'size': size, 'link': link,
                     'info': info, 'entsize': entsize})
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


def relocations(elf_path, section='.image'):
    """-> (base, image bytes, zero fill, [(site, kind, import name or None, value)]): value is the offset
    in the image an own address points at (addend included), or an import's addend."""
    with open(elf_path, 'rb') as f:
        data = f.read()
    secs, syms = _elf(data)
    byname = {s['name']: s for s in secs}
    img = byname.get(section)
    if img is None:
        raise OpmodError(f'{elf_path}: no {section} section')
    base, size = img['addr'], img['size']
    image = data[img['offset']:img['offset'] + size]
    zsec = byname.get('.zero')
    # the zero fill runs from the image's end to the zero section's end: an aligned buffer in it (a file system
    # buffer at an odd address hung the OS) leaves the linker's padding before it, which the loader zeroes too
    end = zsec['addr'] + zsec['size'] if zsec and zsec['size'] else base + size
    zero = end - (base + size)
    out = []
    for s in secs:
        if s['type'] != 4 or secs[s['info']] is not img:     # SHT_RELA for the image
            continue
        for k in range(s['size'] // 12):
            r_offset, r_info, addend = struct.unpack_from('<IIi', data, s['offset'] + k * 12)
            typ, symi = r_info & 0xFF, r_info >> 8
            name, value, shndx = syms[symi]
            target = value + addend
            imported = shndx == SHN_ABS and not base <= target < end
            site = r_offset - base
            if not 0 <= site < size:
                raise OpmodError(f'relocation at 0x{r_offset:08x} outside the image')
            if typ in PCREL and not imported:
                continue                                     # inside the image: moves with it
            kind = KIND.get(typ)
            if kind is None:
                raise OpmodError(f'relocation type 0x{typ:02x} at image+0x{site:x} ({name}): cannot be moved')
            if kind == 'call' and not imported:
                continue
            if imported:
                out.append((site, kind, name.lstrip('_'), addend))
            elif base <= target <= end:
                out.append((site, kind, None, target - base))
            else:
                raise OpmodError(f'image+0x{site:x} points at 0x{target:08x}, outside the image ({name}): '
                                 'an address the build did not give as an import')
    return base, image, zero, sorted(out)


def relocate(image, base, relocs, imports):
    """The loader: the image as it must stand at `base`, every import at imports[name]."""
    b = bytearray(image)
    for site, kind, name, value in relocs:
        full = (imports[name] if name else base) + value
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
    moved = relocate(img_a, base_b, rel_a, imports)
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
        key = kind + (' import' if name else '')
        kinds[key] = kinds.get(key, 0) + 1
    return rel_a, (f'{len(img_a)} bytes, {zero_a} zero fill, {len(rel_a)} relocations ('
                   + ', '.join(f'{n} {k}' for k, n in sorted(kinds.items())) + ')')


LINK_SCRIPT = """SECTIONS
{
    . = 0x%08X;
    .image : { *(.text .text.*) *(.rodata .rodata.*) *(.data .data.*) }
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
            f.write(LINK_SCRIPT % base)
        elf = os.path.join(out_dir, f'{tag}_{n}.elf')
        syms = [f'--defsym={s}=0x{import_address(n, i):08X}' for i, s in enumerate(imports)]
        run([tool(tc, 'ld'), '-q', '--orphan-handling=error', '-T', script, '-o', elf] + syms + list(objs) + [libgcc])
        elfs.append(elf)
    second = {s.lstrip('_'): import_address(1, i) for i, s in enumerate(imports)}
    relocs, summary = check_two_bases(elfs[0], elfs[1], second)
    return [s.lstrip('_') for s in imports], relocs, summary
