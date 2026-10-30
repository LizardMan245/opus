"""outatime's tables (ota_tables.h). The sound as it was shaped on the device (a constant time-travel morph, not a
jump effect; a strong overdrive; a level that stays even when the settings change; time drift and ignition): three
oscillators a voice, the flux capacitor's arms; YEAR their era's wave; SPEED the time drift, the arms at different
points in time travelling around the year; POWER a strong overdrive; TRAIL the ignition, each note bright then burning
down through a resonant filter. The Blackfin has no floating point, so every curve is a table, and every stage that
changes the level has its loudness matched here, by simulation. A knob's table has 257 entries over the knob's
0-32767 (index v >> 7, the next one for the fraction).

    python -I make_tables.py        (it writes ota_tables.h beside itself)
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
VSR = 88200.0                              # the voices' rate
BSR = 44100.0                              # the block's
VBLOCK = 128                               # a voice's block
WAVES = 9                                  # the era waves, 1955 .. 2015 (7.5 years apart; a voice crossfades)
WLEN = 256                                 # their length (fast memory: 9 x 256 x 2 = 4,608 bytes)
FPO = 64                                   # the filter's index: steps an octave
F0 = 100.0                                 # its index 0, Hz
FLEN = 449                                 # 100 Hz .. 12.8 kHz


def q(x, one):
    return int(round(x * one))


def table(name, ctype, values, per_line=8):
    lines = [f'static const {ctype} {name}[{len(values)}] = {{']
    for k in range(0, len(values), per_line):
        lines.append('    ' + ', '.join(str(v) for v in values[k:k + per_line]) + ',')
    lines.append('};')
    return '\n'.join(lines)


def rms(xs):
    return math.sqrt(sum(x * x for x in xs) / len(xs))


def era_wave(t):                           # wave t of WAVES-1: a triangle -> a saw (first half) -> a folded sine
    half = (WAVES - 1) / 2
    late, w = t > half, (t - half) / half if t > half else t / half

    def fold(x):                           # folded back at +-1: a triangle of period 4 over x
        m = (x + 1) % 4
        return m - 1 if m < 2 else 3 - m
    row = []
    for i in range(WLEN):
        saw = 2 * i / WLEN - 1
        if not late:
            a, b = 2 * abs(saw) - 1, saw
        else:
            a, b = saw, fold(math.sin(2 * math.pi * i / WLEN) * (1 + 7 * w))
        row.append(a + (b - a) * w)
    return row


def svf_f(i):                              # the Chamberlin filter's f at index i, at 88.2 kHz
    return 2 * math.sin(math.pi * min(F0 * 2 ** (i / FPO), 14000) / VSR)


def svf_gain(i, damp=1.41):                # a 1985 saw at C4 through it, its rms out over in
    f, low, band, xs, ys = svf_f(i), 0.0, 0.0, [], []
    ph, step = 0.0, 262.0 / VSR
    for n in range(8192):
        ph = (ph + step) % 1
        x = 2 * ph - 1
        low += f * band
        high = x - low - damp * band
        band += f * high
        if n > 2048:
            xs.append(x)
            ys.append(low)
    return rms(ys) / rms(xs)


def main():
    out = ['/* outatime\'s tables: made by make_tables.py (do not edit). */', '#ifndef OTA_TABLES_H', '#define OTA_TABLES_H']
    # the era waves, each at the same rms (a saw's: 1/sqrt(3)), no peak past full scale
    rows = []
    for t in range(WAVES):
        w = era_wave(t)
        k = min((1 / math.sqrt(3)) / rms(w), 0.999 / max(abs(x) for x in w))
        rows.append('    { ' + ', '.join(str(q(x * k, 16384)) for x in w) + ' },')   # half scale: a difference fits 16 bits
    out.append(f'/* YEAR: {WAVES} waves of one cycle in {WLEN} steps, 1955 (0) to 2015 ({WAVES - 1}): a triangle to a saw, then '
               'the saw to a sine folded up to 8 times; each at a saw\'s rms (the year changes the sound, not its '
               'level). A voice\'s arm crossfades the two nearest its year. */')
    out.append(f'#define OTA_WAVES {WAVES}\n#define OTA_WLEN {WLEN}\nstatic const short WAVES_BANK[{WAVES}][{WLEN}] = {{\n'
               + '\n'.join(rows) + '\n};')
    # SPEED: the time drift, its depth (the arms' years apart, up to +-30 years: half the knob's range) and rate
    out.append('/* SPEED (0-88 mph): the time drift\'s rate, 0.06 x 2^(5 s) Hz (to 1.9 Hz), as a phase step a voice block. '
               '*/')
    out.append(table('DRIFT_STEP', 'unsigned', [q(0.06 * 2 ** (5 * i / 256) * VBLOCK / VSR, 2 ** 32) for i in range(257)]))
    out.append('/* SPEED: the drift\'s depth, s^1.5 x 30 years, as a share of the year knob (Q15: 16384 is 30 years). */')
    out.append(table('DRIFT_DEPTH', 'unsigned short', [q((i / 256) ** 1.5 * 0.5, 32768) for i in range(257)]))
    # FLUX (shift + white): the ensemble, its rate and depth
    out.append('/* FLUX (shift + white, a 0-99): the ensemble\'s three-phase pitch modulation: its rate 0.4 + 2.6 a Hz as a '
               'phase step a voice block, its depth 22 a cents as a share of the step (Q24). */')
    out.append(table('FLUX_STEP', 'unsigned', [q((0.4 + 2.6 * i / 256) * VBLOCK / VSR, 2 ** 32) for i in range(257)]))
    out.append(table('FLUX_DEPTH', 'unsigned', [q(2 ** (22 * (i / 256) / 1200) - 1, 2 ** 24) for i in range(257)]))
    # the filter: Chamberlin f over the index (Q14), and the level a saw keeps through it, made up (Q12)
    out.append(f'/* The voice\'s filter (Chamberlin, 88.2 kHz): f at index i, {FPO} an octave from {F0:.0f} Hz (to 14 kHz), '
               'Q14. */')
    out.append(table('SVF_F', 'unsigned short', [q(svf_f(i), 16384) for i in range(FLEN)]))
    comp = [min(4.0, 1 / svf_gain(i)) for i in range(0, FLEN + 7, 8)]
    out.append('/* Its level made up: a saw keeps the same rms whatever the cutoff (index / 8), Q12. */')
    out.append(table('SVF_COMP', 'unsigned short', [q(c, 4096) for c in comp]))
    # TRAIL: the ignition's burn (its decay a voice block) over the knob
    out.append('/* TRAIL (t 0-1): the ignition burns down with 40 ms + 1.4 t^2 s, its decay a voice block (Q15). */')
    out.append(table('BURN', 'unsigned short', [q(math.exp(-(VBLOCK / VSR) / (0.04 + 1.4 * (i / 256) ** 2)), 32768)
                                                for i in range(257)]))
    # POWER: the overdrive's gain 1 + 39 p^2 (Q8); its loudness is matched live in ota_block, so no power and full
    # power sound as loud
    out.append('/* POWER (p 0-1): the overdrive\'s gain 1 + 39 p^2 (Q8); its level is matched live (ota_block). */')
    gains = [1 + 39 * (i / 256) ** 2 for i in range(257)]
    out.append(table('DRIVE_GAIN', 'unsigned short', [q(g, 256) for g in gains]))
    out.append('/* tanh(x) for x 0-8 in 512 steps (x = i / 64), Q15; past 8: 1. */')
    out.append(table('TANH', 'unsigned short', [min(32767, q(math.tanh(i / 64), 32768)) for i in range(513)]))
    out.append('/* sin over a cycle in 1024 steps, Q15. */')
    out.append(table('SINE', 'short', [min(32767, q(math.sin(2 * math.pi * i / 1024), 32768)) for i in range(1024)]))
    with open(os.path.join(HERE, 'ota_tables.h'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out[:3]) + '\n\n' + '\n\n'.join(out[3:]) + '\n\n#endif\n')


if __name__ == '__main__':
    main()
