"""op1re -- reverse-engineering helpers for the OP-1 #246 Blackfin LDR.

Pure standard library. The LDR path is never hardcoded: pass it explicitly
or set the OP1_LDR environment variable.

    from op1re import load
    img = load()                  # uses $OP1_LDR
    img = load('path/to/OP1_vdk.ldr')

Modules
    ldr      LDR block parser, sparse memory Image, memory-map helpers
    bfdis    Blackfin disassembler (port of binutils bfin-dis.c, GPL)
    bfconst  instruction field offsets/masks (from binutils bfin.h, GPL)
    image    flat memory export (holes zero-filled)
    strings  NUL-terminated strings with memory addresses
    consts   32-bit constants materialised by LDIMMhalf .L/.H pairs
    tumap    translation-unit map from `file.cpp:line` assert strings
    sweep    linear-sweep decode statistics

NOTE ON L1 ADDRESSES (0xff800000..0xffb00000). The L1 blocks in the LDR are
CALLBACK,INDIRECT: the boot ROM stages them, and te-boot loads its own L1
image to the same addresses first. The bytes in the file at an L1 address are
therefore not guaranteed to be what runs there at any given moment. These
tools read the file bytes and say so; treat L1 listings as "what the LDR
carries", not "what the CPU executes".
"""
import os

__version__ = '0.1.0'

ENV_VAR = 'OP1_LDR'
L1_LO, L1_HI = 0xff800000, 0xffb00000


class NoLdrPath(SystemExit):
    pass


def ldr_path(explicit=None):
    """Resolve the LDR path: explicit argument, else $OP1_LDR, else error."""
    p = explicit or os.environ.get(ENV_VAR)
    if not p:
        raise NoLdrPath(f'no LDR given: pass a path or set {ENV_VAR}')
    if not os.path.isfile(p):
        raise NoLdrPath(f'LDR not found: {p}')
    return p


def load(path=None):
    from .ldr import Image
    return Image(ldr_path(path))


def in_l1(lo, hi=None):
    hi = lo + 1 if hi is None else hi
    return lo < L1_HI and hi > L1_LO


def add_ldr_arg(parser):
    """Add the common --ldr option to an argparse parser."""
    parser.add_argument('--ldr', metavar='PATH', default=None,
                        help=f'LDR file (default: ${ENV_VAR})')
    return parser
