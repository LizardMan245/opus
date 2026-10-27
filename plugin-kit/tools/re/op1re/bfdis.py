"""Blackfin disassembler -- a Python port of GNU binutils opcodes/bfin-dis.c.

ORIGIN AND LICENCE
    The decoder structure, field extraction and mnemonic rendering below are
    a line-for-line port of `opcodes/bfin-dis.c` from GNU binutils 2.38
    (Copyright (C) Free Software Foundation, Inc., GPL-3.0-or-later). Field
    offsets/masks come from bfconst.py, which was auto-extracted from
    `include/opcode/bfin.h` of the same release. This file is therefore a
    derived work under the GNU General Public License, version 3 or later.
    See tools/re/README.md.

    Ported from the legacy op1_246_mod_v2/re/bfdis.py with these changes:
      * package-relative import of bfconst
      * `disasm()` takes any object exposing `read(mem, size)` and `segs`
        (see op1re.ldr.Image); reads are clamped to the loaded segment so a
        decode near a segment tail never sees the next block's header.

The decoders below mirror the C control flow one-for-one. The dsp32*
parallel-DSP groups are recognised only well enough to get the instruction
length right (and to render the common dsp32alu add/sub/min/max/abs forms);
the rest render as <dsp32...>.

Instruction length depends only on iw0:
    32-bit iff (iw0 & 0xc000) == 0xc000 and (iw0 & 0xff00) != 0xf800
    64-bit (multi-issue) iff 32-bit and BIT_MULTI_INS (0x0800) set and not
    an 0xE8xx linkage/loop op
"""
from .bfconst import *  # noqa: F401,F403


# ---------------------------------------------------------------- registers
DREGS      = [f'R{i}' for i in range(8)]
DREGS_LO   = [f'R{i}.L' for i in range(8)]
DREGS_HI   = [f'R{i}.H' for i in range(8)]
DREGS_BYTE = [f'R{i}.B' for i in range(8)]
PREGS      = ['P0', 'P1', 'P2', 'P3', 'P4', 'P5', 'SP', 'FP']
PREGS_LO   = ['P0.L', 'P1.L', 'P2.L', 'P3.L', 'P4.L', 'P5.L', 'SP.L', 'FP.L']
PREGS_HI   = ['P0.H', 'P1.H', 'P2.H', 'P3.H', 'P4.H', 'P5.H', 'SP.H', 'FP.H']
IREGS      = ['I0', 'I1', 'I2', 'I3']
MREGS      = ['M0', 'M1', 'M2', 'M3']
BREGS      = ['B0', 'B1', 'B2', 'B3']
LREGS      = ['L0', 'L1', 'L2', 'L3']
BAD        = '<illegal-reg>'

DPREGS  = DREGS + PREGS
GREGS   = DREGS + PREGS
REGS    = DREGS + PREGS + IREGS + MREGS + BREGS + LREGS
REGS_LO = (DREGS_LO + PREGS_LO
           + [f'I{i}.L' for i in range(4)] + [f'M{i}.L' for i in range(4)]
           + [f'B{i}.L' for i in range(4)] + [f'L{i}.L' for i in range(4)])
REGS_HI = (DREGS_HI + PREGS_HI
           + [f'I{i}.H' for i in range(4)] + [f'M{i}.H' for i in range(4)]
           + [f'B{i}.H' for i in range(4)] + [f'L{i}.H' for i in range(4)])
ALLREGS = (DREGS + PREGS + IREGS + MREGS + BREGS + LREGS
           + ['A0.X', 'A0.W', 'A1.X', 'A1.W', BAD, BAD, 'ASTAT', 'RETS']
           + [BAD] * 8
           + ['LC0', 'LT0', 'LB0', 'LC1', 'LT1', 'LB1', 'CYCLES', 'CYCLES2']
           + ['USP', 'SEQSTAT', 'SYSCFG', 'RETI', 'RETX', 'RETN', 'RETE', 'EMUDAT'])
STATBITS = ['AZ', 'AN', 'AC0_COPY', 'V_COPY', BAD, BAD, 'AQ', BAD,
            'RND_MOD', BAD, BAD, BAD, 'AC0', 'AC1', BAD, BAD,
            'AV0', 'AV0S', 'AV1', 'AV1S', BAD, BAD, BAD, BAD,
            'V', 'VS', BAD, BAD, BAD, BAD, BAD, BAD]
COUNTERS = ['LC0', 'LC1']


def dregs(x):      return DREGS[x & 7]
def dregs_lo(x):   return DREGS_LO[x & 7]
def dregs_hi(x):   return DREGS_HI[x & 7]
def dregs_byte(x): return DREGS_BYTE[x & 7]
def pregs(x):      return PREGS[x & 7]
def iregs(x):      return IREGS[x & 3]
def mregs(x):      return MREGS[x & 3]
def dpregs(x):     return DPREGS[x & 15]
def gregs(x, i):   return GREGS[((i << 3) | x) & 15]
def regs(x, i):    return REGS[((i << 3) | x) & 31]
def regs_lo(x, i): return REGS_LO[((i << 3) | x) & 31]
def regs_hi(x, i): return REGS_HI[((i << 3) | x) & 31]
def statbits(x):   return STATBITS[x & 31]
def counters(x):   return COUNTERS[x & 1]


def allregs(x, i):
    j = (i << 3) | x
    return ALLREGS[j] if j < len(ALLREGS) else BAD


def IS_DREG(g, r): return g == 0 and r < 8
def IS_PREG(g, r): return g == 1 and r < 8
def IS_RESERVEDREG(g, r): return r > 7 or (g == 4 and r in (4, 5)) or g == 5
def allreg(r, g):  return not IS_RESERVEDREG(g, r)
def mostreg(r, g): return not (IS_DREG(g, r) or IS_PREG(g, r) or IS_RESERVEDREG(g, r))


# ------------------------------------------------------------ constant fmts
# name -> (nbits, reloc, issigned, pcrel, scale, offset, negative, decimal)
CF = {
    'pcrel4':    (4, 1, 0, 1, 1, 0, 0, 0),
    'imm3':      (3, 0, 1, 0, 0, 0, 0, 0),
    'uimm3':     (3, 0, 0, 0, 0, 0, 0, 0),
    'uimm4':     (4, 0, 0, 0, 0, 0, 0, 0),
    'uimm4s4':   (4, 0, 0, 0, 2, 0, 0, 0),
    'uimm4s2':   (4, 0, 0, 0, 1, 0, 0, 0),
    'negimm5s4': (5, 0, 1, 0, 2, 0, 1, 0),
    'imm5d':     (5, 0, 1, 0, 0, 0, 0, 1),
    'uimm5':     (5, 0, 0, 0, 0, 0, 0, 0),
    'imm7':      (7, 0, 1, 0, 0, 0, 0, 0),
    'imm7d':     (7, 0, 1, 0, 0, 0, 0, 1),
    'lppcrel10': (10, 1, 0, 1, 1, 0, 0, 0),
    'pcrel10':   (10, 1, 1, 1, 1, 0, 0, 0),
    'pcrel12':   (12, 1, 1, 1, 1, 0, 0, 0),
    'imm16':     (16, 0, 1, 0, 0, 0, 0, 0),
    'imm16d':    (16, 0, 1, 0, 0, 0, 0, 1),
    'uimm16':    (16, 0, 0, 0, 0, 0, 0, 0),
    'imm16s2':   (16, 0, 1, 0, 1, 0, 0, 0),
    'imm16s4':   (16, 0, 1, 0, 2, 0, 0, 0),
    'uimm16s4':  (16, 0, 0, 0, 2, 0, 0, 0),
    'pcrel24':   (24, 1, 1, 1, 1, 0, 0, 0),
}


def _signext(v, n):
    sb = 1 << (n - 1)
    return ((v & ((sb << 1) - 1)) ^ sb) - sb


def cval(fmt, x, pc=0):
    """Numeric value of a constant field (binutils fmtconst / fmtconst_val)."""
    nbits, reloc, issigned, pcrel, scale, offset, negative, decimal = CF[fmt]
    if reloc:
        if pcrel:
            x = _signext(x, nbits)
        ea = (x + offset) << scale
        if pcrel:
            ea += pc
        return ea & 0xFFFFFFFF
    if negative:
        x = _signext(x | (1 << nbits), nbits + 1)
    elif issigned:
        x = _signext(x, nbits)
    return (x + offset) << scale


def cs(fmt, x, pc=0):
    """Printable form of a constant field."""
    v = cval(fmt, x, pc)
    nbits, reloc, issigned, pcrel, scale, offset, negative, decimal = CF[fmt]
    if reloc:
        return f'0x{v:x}'
    if decimal:
        return str(v)
    if (issigned or negative) and v < 0:
        return f'-0x{-v:x}'
    return f'0x{v:x}'


# ------------------------------------------------------------------ decoder
class Insn:
    __slots__ = ('addr', 'length', 'text', 'kind', 'target', 'iw0', 'iw1', 'comment')

    def __init__(self, addr, length, text, kind=None, target=None,
                 iw0=0, iw1=0, comment=''):
        self.addr, self.length, self.text = addr, length, text
        self.kind, self.target = kind, target   # call/callr/jump/jumpr/cjump/ret
        self.iw0, self.iw1, self.comment = iw0, iw1, comment

    def __repr__(self):
        c = ('   ; ' + self.comment) if self.comment else ''
        return f'0x{self.addr:08x}  {self.text}{c}'


class Dis:
    """Stateful decoder.

    Tracks register values across instructions so that split
    'Rn.L = lo ; Rn.H = hi' pairs report the composed 32-bit constant, the
    same trick binutils' own disassembler uses.
    """

    def __init__(self):
        self.rv = {}
        self.comment = ''

    def reset(self):
        self.rv.clear()

    def _set(self, name, val):
        if name != BAD:
            self.rv[name] = val & 0xFFFFFFFF

    # ------------------------------------------------------------------
    def decode(self, iw0, iw1, pc):
        """-> (text, length, kind, target) or None."""
        self.comment = ''
        for fn in self._TABLE:
            r = fn(self, iw0, iw1, pc)
            if r:
                return r
        return None

    # ---------------------------------------------------------- 16-bit ops
    def _ProgCtrl(self, iw0, iw1, pc):
        if (iw0 & 0xFF00) != 0x0000:
            return None
        poprnd = (iw0 >> ProgCtrl_poprnd_bits) & ProgCtrl_poprnd_mask
        prgfunc = (iw0 >> ProgCtrl_prgfunc_bits) & ProgCtrl_prgfunc_mask
        t = k = None
        if prgfunc == 0 and poprnd == 0:
            t = 'NOP'
        elif prgfunc == 1 and poprnd < 5:
            t = ['RTS', 'RTI', 'RTX', 'RTN', 'RTE'][poprnd]
            k = 'ret'
        elif prgfunc == 2 and poprnd == 0:
            t = 'IDLE'
        elif prgfunc == 2 and poprnd == 3:
            t = 'CSYNC'
        elif prgfunc == 2 and poprnd == 4:
            t = 'SSYNC'
        elif prgfunc == 2 and poprnd == 5:
            t = 'EMUEXCPT'
        elif prgfunc == 3 and IS_DREG(0, poprnd):
            t = 'CLI ' + dregs(poprnd)
        elif prgfunc == 4 and IS_DREG(0, poprnd):
            t = 'STI ' + dregs(poprnd)
        elif prgfunc == 5 and IS_PREG(1, poprnd):
            t, k = f'JUMP ({pregs(poprnd)})', 'jumpr'
        elif prgfunc == 6 and IS_PREG(1, poprnd):
            t, k = f'CALL ({pregs(poprnd)})', 'callr'
        elif prgfunc == 7 and IS_PREG(1, poprnd):
            t, k = f'CALL (PC + {pregs(poprnd)})', 'callr'
        elif prgfunc == 8 and IS_PREG(1, poprnd):
            t, k = f'JUMP (PC + {pregs(poprnd)})', 'jumpr'
        elif prgfunc == 9:
            t = 'RAISE ' + cs('uimm4', poprnd)
        elif prgfunc == 10:
            t = 'EXCPT ' + cs('uimm4', poprnd)
        elif prgfunc == 11 and IS_PREG(1, poprnd) and poprnd <= 5:
            t = f'TESTSET ({pregs(poprnd)})'
        if t is None:
            return None
        # a register-indirect call/jump whose pointer we tracked
        tgt = None
        if k in ('callr', 'jumpr'):
            tgt = self.rv.get(pregs(poprnd))
        return (t, 2, k, tgt)

    def _CaCTRL(self, iw0, iw1, pc):
        if (iw0 & 0xFFC0) != 0x0240:
            return None
        a = (iw0 >> CaCTRL_a_bits) & CaCTRL_a_mask
        op = (iw0 >> CaCTRL_op_bits) & CaCTRL_op_mask
        reg = (iw0 >> CaCTRL_reg_bits) & CaCTRL_reg_mask
        names = ['PREFETCH', 'FLUSHINV', 'FLUSH', 'IFLUSH']
        if a not in (0, 1) or op > 3:
            return None
        inc = '++' if a else ''
        return (f'{names[op]}[{pregs(reg)}{inc}]', 2, None, None)

    def _PushPopReg(self, iw0, iw1, pc):
        if (iw0 & 0xFF80) != 0x0100:
            return None
        W = (iw0 >> PushPopReg_W_bits) & PushPopReg_W_mask
        grp = (iw0 >> PushPopReg_grp_bits) & PushPopReg_grp_mask
        reg = (iw0 >> PushPopReg_reg_bits) & PushPopReg_reg_mask
        if W == 0 and mostreg(reg, grp):
            return (f'{allregs(reg, grp)} = [SP++]', 2, None, None)
        if W == 1 and allreg(reg, grp) and not (grp == 1 and reg == 6):
            return (f'[--SP] = {allregs(reg, grp)}', 2, None, None)
        return None

    def _PushPopMultiple(self, iw0, iw1, pc):
        if (iw0 & 0xFE00) != 0x0400:
            return None
        p = (iw0 >> PushPopMultiple_p_bits) & PushPopMultiple_p_mask
        d = (iw0 >> PushPopMultiple_d_bits) & PushPopMultiple_d_mask
        W = (iw0 >> PushPopMultiple_W_bits) & PushPopMultiple_W_mask
        dr = (iw0 >> PushPopMultiple_dr_bits) & PushPopMultiple_dr_mask
        pr = (iw0 >> PushPopMultiple_pr_bits) & PushPopMultiple_pr_mask
        if pr > 5:
            return None
        D, P = cs('imm5d', dr), cs('imm5d', pr)
        if W == 1 and d == 1 and p == 1:
            t = f'[--SP] = (R7:{D}, P5:{P})'
        elif W == 1 and d == 1 and p == 0 and pr == 0:
            t = f'[--SP] = (R7:{D})'
        elif W == 1 and d == 0 and p == 1 and dr == 0:
            t = f'[--SP] = (P5:{P})'
        elif W == 0 and d == 1 and p == 1:
            t = f'(R7:{D}, P5:{P}) = [SP++]'
        elif W == 0 and d == 1 and p == 0 and pr == 0:
            t = f'(R7:{D}) = [SP++]'
        elif W == 0 and d == 0 and p == 1 and dr == 0:
            t = f'(P5:{P}) = [SP++]'
        else:
            return None
        return (t, 2, None, None)

    def _ccMV(self, iw0, iw1, pc):
        if (iw0 & 0xFE00) != 0x0600:
            return None
        s = (iw0 >> CCmv_s_bits) & CCmv_s_mask
        d = (iw0 >> CCmv_d_bits) & CCmv_d_mask
        T = (iw0 >> CCmv_T_bits) & CCmv_T_mask
        src = (iw0 >> CCmv_src_bits) & CCmv_src_mask
        dst = (iw0 >> CCmv_dst_bits) & CCmv_dst_mask
        cc = 'IF CC ' if T == 1 else 'IF !CC '
        return (f'{cc}{gregs(dst, d)} = {gregs(src, s)}', 2, None, None)

    def _CCflag(self, iw0, iw1, pc):
        if (iw0 & 0xF800) != 0x0800:
            return None
        x = (iw0 >> CCflag_x_bits) & CCflag_x_mask
        y = (iw0 >> CCflag_y_bits) & CCflag_y_mask
        I = (iw0 >> CCflag_I_bits) & CCflag_I_mask
        G = (iw0 >> CCflag_G_bits) & CCflag_G_mask
        opc = (iw0 >> CCflag_opc_bits) & CCflag_opc_mask
        if opc in (5, 6, 7) and I == 0 and G == 0 and x == 0 and y == 0:
            return ('CC = A0 ' + {5: '==', 6: '<', 7: '<='}[opc] + ' A1',
                    2, None, None)
        if opc > 4:
            return None
        rf = pregs if G == 1 else dregs
        op = ['==', '<', '<=', '<', '<='][opc]
        iu = ' (IU)' if opc in (3, 4) else ''
        if I == 1:
            rhs = cs('uimm3', y) if opc in (3, 4) else cs('imm3', y)
        else:
            rhs = rf(y)
        return (f'CC = {rf(x)} {op} {rhs}{iu}', 2, None, None)

    def _CC2dreg(self, iw0, iw1, pc):
        if (iw0 & 0xFFE0) != 0x0200:
            return None
        op = (iw0 >> CC2dreg_op_bits) & CC2dreg_op_mask
        reg = (iw0 >> CC2dreg_reg_bits) & CC2dreg_reg_mask
        if op == 0:
            t = f'{dregs(reg)} = CC'
        elif op == 1:
            t = f'CC = {dregs(reg)}'
        elif op == 3 and reg == 0:
            t = 'CC = !CC'
        else:
            return None
        return (t, 2, None, None)

    def _CC2stat(self, iw0, iw1, pc):
        if (iw0 & 0xFF00) != 0x0300:
            return None
        D = (iw0 >> CC2stat_D_bits) & CC2stat_D_mask
        op = (iw0 >> CC2stat_op_bits) & CC2stat_op_mask
        cbit = (iw0 >> CC2stat_cbit_bits) & CC2stat_cbit_mask
        bitname = statbits(cbit)
        if bitname == BAD:
            if cbit == 5:
                return None
            bitname = f'ASTAT[{cbit}]'
        o = ['', '|', '&', '^'][op]
        t = f'CC {o}= {bitname}' if D == 0 else f'{bitname} {o}= CC'
        return (t, 2, None, None)

    def _BRCC(self, iw0, iw1, pc):
        if (iw0 & 0xF000) != 0x1000:
            return None
        B = (iw0 >> BRCC_B_bits) & BRCC_B_mask
        T = (iw0 >> BRCC_T_bits) & BRCC_T_mask
        off = (iw0 >> BRCC_offset_bits) & BRCC_offset_mask
        tgt = cval('pcrel10', off, pc)
        bp = ' (BP)' if B == 1 else ''
        cc = 'IF CC' if T == 1 else 'IF !CC'
        return (f'{cc} JUMP 0x{tgt:x}{bp}', 2, 'cjump', tgt)

    def _UJUMP(self, iw0, iw1, pc):
        if (iw0 & 0xF000) != 0x2000:
            return None
        off = (iw0 >> UJump_offset_bits) & UJump_offset_mask
        tgt = cval('pcrel12', off, pc)
        return (f'JUMP.S 0x{tgt:x}', 2, 'jump', tgt)

    def _REGMV(self, iw0, iw1, pc):
        if (iw0 & 0xF000) != 0x3000:
            return None
        gs = (iw0 >> RegMv_gs_bits) & RegMv_gs_mask
        gd = (iw0 >> RegMv_gd_bits) & RegMv_gd_mask
        src = (iw0 >> RegMv_src_bits) & RegMv_src_mask
        dst = (iw0 >> RegMv_dst_bits) & RegMv_dst_mask
        if IS_RESERVEDREG(gs, src) or IS_RESERVEDREG(gd, dst):
            return None
        ok = (gs < 2 or gd < 2 or (gs == 4 and src < 4)
              or (gd == 4 and dst < 4 and gs < 4)
              or (gs == 7 and src == 7 and not (gd == 4 and dst < 4))
              or (gd == 7 and dst == 7)
              or (gs < 4 and gd < 4)
              or (gs == 7 and src == 0 and gd >= 4)
              or (gs == 7 and src == 0 and gd == 4 and dst < 4)
              or (gd == 7 and dst == 0 and gs == 4 and src < 4))
        if not ok:
            return None
        s, d = allregs(src, gs), allregs(dst, gd)
        if s in self.rv:
            self._set(d, self.rv[s])
        else:
            self.rv.pop(d, None)
        return (f'{d} = {s}', 2, None, None)

    def _ALU2op(self, iw0, iw1, pc):
        if (iw0 & 0xFC00) != 0x4000:
            return None
        src = (iw0 >> ALU2op_src_bits) & ALU2op_src_mask
        opc = (iw0 >> ALU2op_opc_bits) & ALU2op_opc_mask
        dst = (iw0 >> ALU2op_dst_bits) & ALU2op_dst_mask
        D, S = dregs(dst), dregs(src)
        m = {0: f'{D} >>>= {S}', 1: f'{D} >>= {S}', 2: f'{D} <<= {S}',
             3: f'{D} *= {S}', 4: f'{D} = ({D} + {S}) << 0x1',
             5: f'{D} = ({D} + {S}) << 0x2',
             8: f'DIVQ ({D}, {S})', 9: f'DIVS ({D}, {S})',
             10: f'{D} = {dregs_lo(src)} (X)', 11: f'{D} = {dregs_lo(src)} (Z)',
             12: f'{D} = {dregs_byte(src)} (X)', 13: f'{D} = {dregs_byte(src)} (Z)',
             14: f'{D} = -{S}', 15: f'{D} =~ {S}'}
        if opc not in m:
            return None
        self.rv.pop(D, None)
        return (m[opc], 2, None, None)

    def _PTR2op(self, iw0, iw1, pc):
        if (iw0 & 0xFE00) != 0x4400:
            return None
        src = (iw0 >> PTR2op_src_bits) & PTR2op_dst_mask
        opc = (iw0 >> PTR2op_opc_bits) & PTR2op_opc_mask
        dst = (iw0 >> PTR2op_dst_bits) & PTR2op_dst_mask
        D, S = pregs(dst), pregs(src)
        m = {0: f'{D} -= {S}', 1: f'{D} = {S} << 0x2', 3: f'{D} = {S} >> 0x2',
             4: f'{D} = {S} >> 0x1', 5: f'{D} += {S} (BREV)',
             6: f'{D} = ({D} + {S}) << 0x1', 7: f'{D} = ({D} + {S}) << 0x2'}
        if opc not in m:
            return None
        self.rv.pop(D, None)
        return (m[opc], 2, None, None)

    def _LOGI2op(self, iw0, iw1, pc):
        if (iw0 & 0xF800) != 0x4800:
            return None
        src = (iw0 >> LOGI2op_src_bits) & LOGI2op_src_mask
        opc = (iw0 >> LOGI2op_opc_bits) & LOGI2op_opc_mask
        dst = (iw0 >> LOGI2op_dst_bits) & LOGI2op_dst_mask
        D, U = dregs(dst), cs('uimm5', src)
        m = {0: f'CC = !BITTST ({D}, {U})', 1: f'CC = BITTST ({D}, {U})',
             2: f'BITSET ({D}, {U})', 3: f'BITTGL ({D}, {U})',
             4: f'BITCLR ({D}, {U})', 5: f'{D} >>>= {U}',
             6: f'{D} >>= {U}', 7: f'{D} <<= {U}'}
        if opc not in m:
            return None
        if opc >= 2:
            self.rv.pop(D, None)
        return (m[opc], 2, None, None)

    def _COMP3op(self, iw0, iw1, pc):
        if (iw0 & 0xF000) != 0x5000:
            return None
        opc = (iw0 >> COMP3op_opc_bits) & COMP3op_opc_mask
        dst = (iw0 >> COMP3op_dst_bits) & COMP3op_dst_mask
        src0 = (iw0 >> COMP3op_src0_bits) & COMP3op_src0_mask
        src1 = (iw0 >> COMP3op_src1_bits) & COMP3op_src1_mask
        if opc == 5 and src1 == src0:
            self.rv.pop(pregs(dst), None)
            return (f'{pregs(dst)} = {pregs(src0)} << 0x1', 2, None, None)
        d, a, b = dregs(dst), dregs(src0), dregs(src1)
        pd, pa, pb = pregs(dst), pregs(src0), pregs(src1)
        m = {0: (f'{d} = {a} + {b}', d), 1: (f'{d} = {a} - {b}', d),
             2: (f'{d} = {a} & {b}', d), 3: (f'{d} = {a} | {b}', d),
             4: (f'{d} = {a} ^ {b}', d), 5: (f'{pd} = {pa} + {pb}', pd),
             6: (f'{pd} = {pa} + ({pb} << 0x1)', pd),
             7: (f'{pd} = {pa} + ({pb} << 0x2)', pd)}
        if opc not in m:
            return None
        txt, clob = m[opc]
        self.rv.pop(clob, None)
        return (txt, 2, None, None)

    def _COMPI2op(self, iw0, iw1, pc, isP):
        op = (iw0 >> (COMPI2opP_op_bits if isP else COMPI2opD_op_bits)) & 1
        dst = (iw0 >> (COMPI2opP_dst_bits if isP else COMPI2opD_dst_bits)) & 7
        src = (iw0 >> (COMPI2opP_src_bits if isP else COMPI2opD_src_bits)) & 0x7F
        name = pregs(dst) if isP else dregs(dst)
        if op == 0:
            val = src | 0xFFFFFF80 if (src & 0x40) else src & 0x7F
            self._set(name, val)
            self.comment = f'{name}=0x{val & 0xFFFFFFFF:08x}'
            return (f'{name} = {cs("imm7", src)} (X)', 2, None, None)
        if op == 1:
            self.rv.pop(name, None)
            return (f'{name} += {cs("imm7", src)}', 2, None, None)
        return None

    def _COMPI2opD(self, iw0, iw1, pc):
        if (iw0 & 0xF800) != 0x6000:
            return None
        return self._COMPI2op(iw0, iw1, pc, False)

    def _COMPI2opP(self, iw0, iw1, pc):
        if (iw0 & 0xF800) != 0x6800:
            return None
        return self._COMPI2op(iw0, iw1, pc, True)

    def _LDSTpmod(self, iw0, iw1, pc):
        if (iw0 & 0xF000) != 0x8000:
            return None
        W = (iw0 >> LDSTpmod_W_bits) & LDSTpmod_W_mask
        aop = (iw0 >> LDSTpmod_aop_bits) & LDSTpmod_aop_mask
        idx = (iw0 >> LDSTpmod_idx_bits) & LDSTpmod_idx_mask
        ptr = (iw0 >> LDSTpmod_ptr_bits) & LDSTpmod_ptr_mask
        reg = (iw0 >> LDSTpmod_reg_bits) & LDSTpmod_reg_mask
        P, I = pregs(ptr), pregs(idx)
        if idx == ptr and aop in (1, 2) and W in (0, 1):
            half = dregs_lo(reg) if aop == 1 else dregs_hi(reg)
            if W == 0:
                self.rv.pop(dregs(reg), None)
                return (f'{half} = W[{P}]', 2, None, None)
            return (f'W[{P}] = {half}', 2, None, None)
        pm = f'{P} ++ {I}'
        table = {
            (0, 0): (f'{dregs(reg)} = [{pm}]', dregs(reg)),
            (1, 0): (f'{dregs_lo(reg)} = W[{pm}]', dregs(reg)),
            (2, 0): (f'{dregs_hi(reg)} = W[{pm}]', dregs(reg)),
            (3, 0): (f'{dregs(reg)} = W[{pm}] (Z)', dregs(reg)),
            (3, 1): (f'{dregs(reg)} = W[{pm}] (X)', dregs(reg)),
            (0, 1): (f'[{pm}] = {dregs(reg)}', None),
            (1, 1): (f'W[{pm}] = {dregs_lo(reg)}', None),
            (2, 1): (f'W[{pm}] = {dregs_hi(reg)}', None),
        }
        r = table.get((aop, W))
        if not r:
            return None
        if r[1]:
            self.rv.pop(r[1], None)
        return (r[0], 2, None, None)

    def _dagMODim(self, iw0, iw1, pc):
        if (iw0 & 0xFF60) != 0x9E60:
            return None
        i = (iw0 >> DagMODim_i_bits) & DagMODim_i_mask
        m = (iw0 >> DagMODim_m_bits) & DagMODim_m_mask
        br = (iw0 >> DagMODim_br_bits) & DagMODim_br_mask
        op = (iw0 >> DagMODim_op_bits) & DagMODim_op_mask
        if op == 0 and br == 1:
            t = f'{iregs(i)} += {mregs(m)} (BREV)'
        elif op == 0:
            t = f'{iregs(i)} += {mregs(m)}'
        elif op == 1 and br == 0:
            t = f'{iregs(i)} -= {mregs(m)}'
        else:
            return None
        return (t, 2, None, None)

    def _dagMODik(self, iw0, iw1, pc):
        if (iw0 & 0xFFF0) != 0x9F60:
            return None
        i = (iw0 >> DagMODik_i_bits) & DagMODik_i_mask
        op = (iw0 >> DagMODik_op_bits) & DagMODik_op_mask
        if op > 3:
            return None
        t = f'{iregs(i)} {"+=" if op in (0, 2) else "-="} 0x{2 if op < 2 else 4:x}'
        return (t, 2, None, None)

    def _dspLDST(self, iw0, iw1, pc):
        if (iw0 & 0xFC00) != 0x9C00:
            return None
        i = (iw0 >> DspLDST_i_bits) & DspLDST_i_mask
        m = (iw0 >> DspLDST_m_bits) & DspLDST_m_mask
        W = (iw0 >> DspLDST_W_bits) & DspLDST_W_mask
        aop = (iw0 >> DspLDST_aop_bits) & DspLDST_aop_mask
        reg = (iw0 >> DspLDST_reg_bits) & DspLDST_reg_mask
        I = iregs(i)
        suffix = {0: '++', 1: '--', 2: ''}
        if aop in (0, 1, 2) and m in (0, 1, 2):
            addr = f'{I}{suffix[aop]}'
            if m == 0:
                lhs, sz = dregs(reg), ''
            elif m == 1:
                lhs, sz = dregs_lo(reg), 'W'
            else:
                lhs, sz = dregs_hi(reg), 'W'
            if W == 0:
                self.rv.pop(dregs(reg), None)
                return (f'{lhs} = {sz}[{addr}]', 2, None, None)
            return (f'{sz}[{addr}] = {lhs}', 2, None, None)
        if aop == 3:
            if W == 0:
                self.rv.pop(dregs(reg), None)
                return (f'{dregs(reg)} = [{I} ++ {mregs(m)}]', 2, None, None)
            return (f'[{I} ++ {mregs(m)}] = {dregs(reg)}', 2, None, None)
        return None

    def _LDST(self, iw0, iw1, pc):
        if (iw0 & 0xF000) != 0x9000:
            return None
        Z = (iw0 >> LDST_Z_bits) & LDST_Z_mask
        W = (iw0 >> LDST_W_bits) & LDST_W_mask
        sz = (iw0 >> LDST_sz_bits) & LDST_sz_mask
        aop = (iw0 >> LDST_aop_bits) & LDST_aop_mask
        reg = (iw0 >> LDST_reg_bits) & LDST_reg_mask
        ptr = (iw0 >> LDST_ptr_bits) & LDST_ptr_mask
        if aop > 2 or sz > 2:
            return None
        P = pregs(ptr)
        addr = f'{P}++' if aop == 0 else (f'{P}--' if aop == 1 else P)
        pfx = {0: '', 1: 'W', 2: 'B'}[sz]
        if W == 0:
            if sz == 0 and Z == 1:
                if aop != 2 and reg == ptr:
                    return None
                self.rv.pop(pregs(reg), None)
                return (f'{pregs(reg)} = [{addr}]', 2, None, None)
            ext = '' if sz == 0 else (' (Z)' if Z == 0 else ' (X)')
            self.rv.pop(dregs(reg), None)
            return (f'{dregs(reg)} = {pfx}[{addr}]{ext}', 2, None, None)
        if sz == 0 and Z == 1:
            return (f'[{addr}] = {pregs(reg)}', 2, None, None)
        if Z != 0:
            return None
        return (f'{pfx}[{addr}] = {dregs(reg)}', 2, None, None)

    def _LDSTiiFP(self, iw0, iw1, pc):
        if (iw0 & 0xFC00) != 0xB800:
            return None
        reg = (iw0 >> LDSTiiFP_reg_bits) & LDSTiiFP_reg_mask
        off = (iw0 >> LDSTiiFP_offset_bits) & LDSTiiFP_offset_mask
        W = (iw0 >> LDSTiiFP_W_bits) & LDSTiiFP_W_mask
        o = cs('negimm5s4', off)
        if W == 0:
            self.rv.pop(dpregs(reg), None)
            return (f'{dpregs(reg)} = [FP {o}]', 2, None, None)
        return (f'[FP {o}] = {dpregs(reg)}', 2, None, None)

    def _LDSTii(self, iw0, iw1, pc):
        if (iw0 & 0xE000) != 0xA000:
            return None
        reg = (iw0 >> LDSTii_reg_bit) & LDSTii_reg_mask
        ptr = (iw0 >> LDSTii_ptr_bit) & LDSTii_ptr_mask
        off = (iw0 >> LDSTii_offset_bit) & LDSTii_offset_mask
        op = (iw0 >> LDSTii_op_bit) & LDSTii_op_mask
        W = (iw0 >> LDSTii_W_bit) & LDSTii_W_mask
        P = pregs(ptr)
        o4, o2 = cs('uimm4s4', off), cs('uimm4s2', off)
        if W == 0:
            if op == 0:
                self.rv.pop(dregs(reg), None)
                return (f'{dregs(reg)} = [{P} + {o4}]', 2, None, None)
            if op == 1:
                self.rv.pop(dregs(reg), None)
                return (f'{dregs(reg)} = W[{P} + {o2}] (Z)', 2, None, None)
            if op == 2:
                self.rv.pop(dregs(reg), None)
                return (f'{dregs(reg)} = W[{P} + {o2}] (X)', 2, None, None)
            self.rv.pop(pregs(reg), None)
            return (f'{pregs(reg)} = [{P} + {o4}]', 2, None, None)
        if op == 0:
            return (f'[{P} + {o4}] = {dregs(reg)}', 2, None, None)
        if op == 1:
            return (f'W[{P} + {o2}] = {dregs(reg)}', 2, None, None)
        if op == 3:
            return (f'[{P} + {o4}] = {pregs(reg)}', 2, None, None)
        return None

    # ---------------------------------------------------------- 32-bit ops
    def _LoopSetup(self, iw0, iw1, pc):
        if not ((iw0 & 0xFF80) == 0xE080 and (iw1 & 0x0C00) == 0x0000):
            return None
        c = (iw0 >> (LoopSetup_c_bits - 16)) & LoopSetup_c_mask
        reg = (iw1 >> LoopSetup_reg_bits) & LoopSetup_reg_mask
        rop = (iw0 >> (LoopSetup_rop_bits - 16)) & LoopSetup_rop_mask
        so = (iw0 >> (LoopSetup_soffset_bits - 16)) & LoopSetup_soffset_mask
        eo = (iw1 >> LoopSetup_eoffset_bits) & LoopSetup_eoffset_mask
        if reg > 7:
            return None
        head = (f'LSETUP(0x{cval("pcrel4", so, pc):x}, '
                f'0x{cval("lppcrel10", eo, pc):x}) {counters(c)}')
        if rop == 0:
            return (head, 4, None, None)
        if rop == 1:
            return (f'{head} = {pregs(reg)}', 4, None, None)
        if rop == 3:
            return (f'{head} = {pregs(reg)} >> 0x1', 4, None, None)
        return None

    def _LDIMMhalf(self, iw0, iw1, pc):
        if (iw0 & 0xFF00) != 0xE100:
            return None
        H = (iw0 >> (LDIMMhalf_H_bits - 16)) & LDIMMhalf_H_mask
        Z = (iw0 >> (LDIMMhalf_Z_bits - 16)) & LDIMMhalf_Z_mask
        S = (iw0 >> (LDIMMhalf_S_bits - 16)) & LDIMMhalf_S_mask
        reg = (iw0 >> (LDIMMhalf_reg_bits - 16)) & LDIMMhalf_reg_mask
        grp = (iw0 >> (LDIMMhalf_grp_bits - 16)) & LDIMMhalf_grp_mask
        hword = (iw1 >> LDIMMhalf_hword_bits) & LDIMMhalf_hword_mask
        name = allregs(reg, grp)
        old = self.rv.get(name, 0)
        if H == 0 and S == 1 and Z == 0:
            val = (hword | 0xFFFF0000) if (hword & 0x8000) else (hword & 0xFFFF)
        elif H == 0 and S == 0 and Z == 1:
            val = hword & 0xFFFF
        elif H == 0 and S == 0 and Z == 0:
            val = (old & 0xFFFF0000) | hword
        elif H == 1 and S == 0 and Z == 0:
            val = (old & 0xFFFF) | (hword << 16)
        else:
            return None
        self._set(name, val)
        if grp == 0 and H == 0 and S == 0 and Z == 0:
            t = f'{dregs_lo(reg)} = {cs("uimm16", hword)}'
        elif grp == 0 and H == 1 and S == 0 and Z == 0:
            t = f'{dregs_hi(reg)} = {cs("uimm16", hword)}'
        elif H == 0 and S == 1 and Z == 0:
            t = f'{regs(reg, grp)} = {cs("imm16", hword)} (X)'
        elif H == 0 and S == 0 and Z == 1:
            t = f'{regs(reg, grp)} = {cs("uimm16", hword)} (Z)'
        elif H == 0 and S == 0 and Z == 0:
            t = f'{regs_lo(reg, grp)} = {cs("uimm16", hword)}'
        elif H == 1 and S == 0 and Z == 0:
            t = f'{regs_hi(reg, grp)} = {cs("uimm16", hword)}'
        else:
            return None
        self.comment = f'{name}=0x{val & 0xFFFFFFFF:08x}'
        return (t, 4, None, None)

    def _CALLa(self, iw0, iw1, pc):
        if (iw0 & 0xFE00) != 0xE200:
            return None
        S = (iw0 >> (CALLa_S_bits - 16)) & CALLa_S_mask
        lsw = iw1 & 0xFFFF
        msw = iw0 & 0xFF
        tgt = cval('pcrel24', (msw << 16) | lsw, pc)
        if S == 1:
            return (f'CALL 0x{tgt:x}', 4, 'call', tgt)
        return (f'JUMP.L 0x{tgt:x}', 4, 'jump', tgt)

    def _LDSTidxI(self, iw0, iw1, pc):
        if (iw0 & 0xFC00) != 0xE400:
            return None
        Z = (iw0 >> (LDSTidxI_Z_bits - 16)) & LDSTidxI_Z_mask
        W = (iw0 >> (LDSTidxI_W_bits - 16)) & LDSTidxI_W_mask
        sz = (iw0 >> (LDSTidxI_sz_bits - 16)) & LDSTidxI_sz_mask
        reg = (iw0 >> (LDSTidxI_reg_bits - 16)) & LDSTidxI_reg_mask
        ptr = (iw0 >> (LDSTidxI_ptr_bits - 16)) & LDSTidxI_ptr_mask
        off = (iw1 >> LDSTidxI_offset_bits) & LDSTidxI_offset_mask
        P = pregs(ptr)
        o4, o2, o1 = cs('imm16s4', off), cs('imm16s2', off), cs('imm16', off)
        if W == 0:
            if sz == 0 and Z == 0:
                self.rv.pop(dregs(reg), None)
                return (f'{dregs(reg)} = [{P} + {o4}]', 4, None, None)
            if sz == 0 and Z == 1:
                self.rv.pop(pregs(reg), None)
                return (f'{pregs(reg)} = [{P} + {o4}]', 4, None, None)
            self.rv.pop(dregs(reg), None)
            if sz == 1:
                return (f'{dregs(reg)} = W[{P} + {o2}] '
                        f'({"Z" if Z == 0 else "X"})', 4, None, None)
            if sz == 2:
                return (f'{dregs(reg)} = B[{P} + {o1}] '
                        f'({"Z" if Z == 0 else "X"})', 4, None, None)
            return None
        if sz == 0 and Z == 0:
            return (f'[{P} + {o4}] = {dregs(reg)}', 4, None, None)
        if sz == 0 and Z == 1:
            return (f'[{P} + {o4}] = {pregs(reg)}', 4, None, None)
        if sz == 1 and Z == 0:
            return (f'W[{P} + {o2}] = {dregs(reg)}', 4, None, None)
        if sz == 2 and Z == 0:
            return (f'B[{P} + {o1}] = {dregs(reg)}', 4, None, None)
        return None

    def _linkage(self, iw0, iw1, pc):
        if (iw0 & 0xFFFE) != 0xE800:
            return None
        R = (iw0 >> (Linkage_R_bits - 16)) & Linkage_R_mask
        fs = (iw1 >> Linkage_framesize_bits) & Linkage_framesize_mask
        if R == 0:
            return (f'LINK {cs("uimm16s4", fs)}', 4, None, None)
        return ('UNLINK', 4, None, None)

    def _alu_text(self, iw0, iw1):
        """Render the common dsp32alu forms (add/sub/MIN/MAX/ABS).

        Field layout per binutils: aopcde/HL come from iw0, the rest from iw1.
        aop bit1 selects src0's half, bit0 selects src1's half; HL selects the
        destination half. amod1(s,x): (NS) when s=0,x=0 and (S) when s=1,x=0.
        """
        aopcde = iw0 & 0x1F
        HL = (iw0 >> 5) & 1
        src1 = iw1 & 7
        src0 = (iw1 >> 3) & 7
        dst1 = (iw1 >> 6) & 7
        dst0 = (iw1 >> 9) & 7
        x = (iw1 >> 12) & 1
        s = (iw1 >> 13) & 1
        aop = (iw1 >> 14) & 3
        mod = ' (S)' if (s == 1 and x == 0) else (' (NS)' if (s == 0 and x == 0) else '')
        half = lambda r, hi: (dregs_hi if hi else dregs_lo)(r)
        if aopcde in (2, 3) and aop < 4:
            op = '+' if aopcde == 2 else '-'
            return (f'{half(dst0, HL)} = {half(src0, (aop >> 1) & 1)} {op} '
                    f'{half(src1, aop & 1)}{mod}')
        if aopcde == 4:
            if aop == 0:
                return f'{dregs(dst0)} = {dregs(src0)} + {dregs(src1)}{mod}'
            if aop == 1:
                return f'{dregs(dst0)} = {dregs(src0)} - {dregs(src1)}{mod}'
            if aop == 2:
                return (f'{dregs(dst1)} = {dregs(src0)} + {dregs(src1)}, '
                        f'{dregs(dst0)} = {dregs(src0)} - {dregs(src1)}{mod}')
        if aopcde in (6, 7):
            v = ' (V)' if aopcde == 6 else ''
            if aop == 0:
                return f'{dregs(dst0)} = MAX ({dregs(src0)}, {dregs(src1)}){v}'
            if aop == 1:
                return f'{dregs(dst0)} = MIN ({dregs(src0)}, {dregs(src1)}){v}'
            if aop == 2:
                return f'{dregs(dst0)} = ABS {dregs(src0)}{v}'
        return None

    def _dsp32(self, iw0, iw1, pc):
        """Parallel-DSP groups. dsp32alu arithmetic is rendered; the rest are
        recognised for length only."""
        for mask, val, name in ((0xF600, 0xC000, 'dsp32mac'),
                                (0xF600, 0xC200, 'dsp32mult'),
                                (0xF7C0, 0xC400, 'dsp32alu'),
                                (0xF780, 0xC600, 'dsp32shift'),
                                (0xF780, 0xC680, 'dsp32shiftimm')):
            if (iw0 & mask) == val:
                # conservative: any Dreg may be clobbered
                self.rv.clear()
                if name == 'dsp32alu':
                    t = self._alu_text(iw0, iw1)
                    if t:
                        return (t, 4, None, None)
                return (f'<{name} {iw0:04x} {iw1:04x}>', 4, None, None)
        return None

    _TABLE = (_ProgCtrl, _CaCTRL, _PushPopReg, _PushPopMultiple, _ccMV, _CCflag,
              _CC2dreg, _CC2stat, _BRCC, _UJUMP, _REGMV, _ALU2op, _PTR2op,
              _LOGI2op, _COMP3op, _COMPI2opD, _COMPI2opP, _LDSTpmod, _dagMODim,
              _dagMODik, _dspLDST, _LDST, _LDSTiiFP, _LDSTii, _LoopSetup,
              _LDIMMhalf, _CALLa, _LDSTidxI, _linkage, _dsp32)


def insn_length(iw0):
    """Encoded length in bytes, from iw0 alone -- independent of whether the
    instruction is one we can render. A 32-bit instruction carrying
    BIT_MULTI_INS (0x0800) is a 64-bit multi-issue group: the 32-bit op plus
    two 16-bit slots issued in parallel. Getting this right on instructions we
    cannot decode is what keeps a linear sweep in sync."""
    if (iw0 & 0xC000) != 0xC000 or (iw0 & 0xFF00) == 0xF800:
        return 2
    if (iw0 & 0x0800) and (iw0 & 0xE800) != 0xE800:
        return 8
    return 4


def decode_at(img, pc, d):
    """Decode the one instruction at `pc` with the stateful decoder `d`.

    `img` needs only read(addr, n) -> bytes (may be short) or None.  Returns an
    Insn (text 'ILLEGAL (...)' and kind None when undecodable), or None when
    nothing is loaded at pc.  A 64-bit multi-issue group is one Insn of length 8.
    """
    import struct
    b = img.read(pc, 8)
    if not b or len(b) < 2:
        return None
    iw0 = struct.unpack_from('<H', b, 0)[0]
    n = insn_length(iw0)
    iw1 = struct.unpack_from('<H', b, 2)[0] if (n >= 4 and len(b) >= 4) else 0
    if iw0 == 0xC003 and iw1 == 0x1800:
        return Insn(pc, 4, 'MNOP', iw0=iw0, iw1=iw1)
    r = d.decode(iw0, iw1, pc)
    if r is None:
        return Insn(pc, n, f'ILLEGAL (0x{iw0:04x})', iw0=iw0, iw1=iw1)
    text, ln, kind, tgt = r
    cmt = d.comment
    # 64-bit multi-issue: a 32-bit DSP insn with BIT_MULTI_INS (0x0800)
    # set is followed by two 16-bit instructions issued in parallel.
    if (ln == 4 and (iw0 & 0xC000) == 0xC000 and (iw0 & 0x0800)
            and (iw0 & 0xE800) != 0xE800):
        b8 = img.read(pc, 8)
        if b8 and len(b8) == 8:
            parts = [text]
            for k in (4, 6):
                s0 = struct.unpack_from('<H', b8, k)[0]
                rr = d.decode(s0, 0, pc + k)
                parts.append(rr[0] if rr else f'ILLEGAL(0x{s0:04x})')
                if rr and d.comment:
                    cmt = cmt or d.comment
            text = ' || '.join(parts)
            ln = 8
    return Insn(pc, ln, text, kind, tgt, iw0, iw1, cmt)


def disasm(img, start, end, dis=None):
    """Linearly disassemble [start, end) from an ldr.Image. Yields Insn."""
    d = dis or Dis()
    d.reset()
    pc = start
    while pc < end:
        ins = decode_at(img, pc, d)
        if ins is None:
            # hole in the image (FILL/BSS): resume at the next loaded segment
            nxt = [a for a, o, n in img.segs if a > pc]
            if not nxt:
                return
            pc = min(nxt)
            d.reset()
            continue
        yield ins
        pc += ins.length
