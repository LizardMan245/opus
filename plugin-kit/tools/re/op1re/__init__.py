import os

__version__ = '0.1.0'

ENV_VAR = 'OP1_LDR'
L1_LO, L1_HI = 0xff800000, 0xffb00000

class NoLdrPath(SystemExit):
    pass

def ldr_path(explicit=None):

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

    parser.add_argument('--ldr', metavar='PATH', default=None,
                        help=f'LDR file (default: ${ENV_VAR})')
    return parser
