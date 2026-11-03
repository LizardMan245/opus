/* castle's saw, one control step (16 samples), in Blackfin assembly: Lode's saw_eval (castle_dsp.c, saw_run) sample
 * for sample, the same numbers as the C (castle_dsp.c builds the C on the computer and checks both agree).
 *
 * What the Blackfin does here that the C compiler would not:
 *   - a hardware loop (LSETUP): no counter, compare or branch per sample;
 *   - the two products of the first operator's output (by its feedback 0.4 x fbfac and by the shaper 0.2 x factor) in
 *     one instruction: both multipliers at once (the dual 16 x 16 multiply);
 *   - each sine lookup's interpolation, rise x fraction >> 15, as one fractional multiply that truncates (T) straight
 *     into a half register, and the add as one 16-bit add: two instructions for four;
 *   - the fraction (bits 7 to 21 of the phase) cut out with one EXTRACT;
 *   - a load issued beside an arithmetic instruction in the same cycle (multi-issue);
 *   - every value of the loop in a register: no stack traffic.
 * The two lookups of a sample are interleaved: the next sample's first operator (it needs only this sample's first
 * output) is looked up beside this sample's second, so neither waits on the other.
 *
 * void ca_saw_set(struct ca_saw *), ca_saw_add, ca_saw_mix: out[k] = b, out[k] += b, out[k] += b x dm >> 15.
 * struct ca_saw (castle_dsp.c): q (phase with the saw's offset), f0, f1 (feedbacks), c1 (fsa | sh << 16),
 * c2 (0x070F, EXTRACT's place and length | fsb << 16), dm, st (17 steps, the last 0), out, tab (the sine pairs).
 * The BF524's anomalies the compiler works round: no conditional branch before a load here (05000245), the routine is
 * far longer than 5 cycles (RETS, 05000371), no P register stored beside a shift (05000074). */

#define Q   R0
#define F0  R1
#define F1  R2

/* The output: b in R3.L, R7 the next sample's first pair (its rise high), R6 its fraction. Each ends with the next a in
 * R7.L, the value the loop starts from. */
/* out[k] = b */
#define OUT_SET \
    R3 = R3.L (X); \
    R6.L = R7.H * R6.L (T) || [I2++] = R3 || NOP; \
2:  R7.L = R7.L + R6.L (NS);
/* out[k] += b */
#define OUT_ADD \
    R6.L = R7.H * R6.L (T); \
    R7.L = R7.L + R6.L (NS) || R6 = [I2] || NOP;   /* the add reads R6 before the load replaces it */ \
    R3 = R3.L (X); \
    R6 = R6 + R3; \
2:  [I2++] = R6;
/* out[k] += b x dm >> 15 (dm under 32,768: a truncating fractional multiply) */
#define OUT_MIX \
    R6.L = R7.H * R6.L (T); \
    R7.L = R7.L + R6.L (NS); \
    R6 = M3; \
    R6.L = R3.L * R6.L (T) || R3 = [I2] || NOP; \
    R6 = R6.L (X); \
    R3 = R3 + R6; \
2:  [I2++] = R3;

#define SAW(name, OUT) \
    .text; .align 4; .global name; .type name, STT_FUNC; \
name: \
    [--SP] = (R7:4); \
    P2 = R0; \
    Q = [P2]; \
    F0 = [P2 + 4]; \
    F1 = [P2 + 8]; \
    R4 = [P2 + 12]; \
    R5 = [P2 + 16]; \
    R6 = [P2 + 20]; \
    M3 = R6; \
    P1 = [P2 + 24]; \
    I1 = P1; \
    P1 = [P2 + 28]; \
    I2 = P1; \
    P0 = [P2 + 32]; \
    I3 = P2; \
    /* the first sample's first operator: a = sine(q + f0) */ \
    R6 = [I1++]; \
    Q = Q + R6; \
    R6 = Q + F0; \
    R3 = R6 >> 22; \
    P2 = R3; \
    R6 = EXTRACT(R6, R5.L) (Z); \
    P1 = 16; \
    P2 = P0 + (P2 << 2); \
    R7 = [P2]; \
    R6.L = R7.H * R6.L (T); \
    R7.L = R7.L + R6.L (NS); \
    LSETUP (1f, 2f) LC0 = P1; \
1:  R7 = R7.L * R4.H, R6 = R7.L * R4.L (IS);   /* a x sh, a x fsa */ \
    F0 = F0 >>> 1; \
    F0 = F0 + R6;                              /* the next f0 */ \
    R7 = R7 >>> 2; \
    R7 <<= 4;                                  /* the shaper's push, (a x sh >> 2) << 4 */ \
    R7 = R7 + F1; \
    R7 = R7 + Q (NS) || R6 = [I1++] || NOP;    /* the second operator's phase; the next step */ \
    Q = Q + R6; \
    R6 = Q + F0;                               /* the next sample's first operator's phase */ \
    R3 = R7 >> 22; \
    P1 = R3; \
    R3 = R6 >> 22; \
    P2 = R3; \
    R7 = EXTRACT(R7, R5.L) (Z); \
    P1 = P0 + (P1 << 2); \
    P2 = P0 + (P2 << 2); \
    R6 = EXTRACT(R6, R5.L) (Z) || R3 = [P1] || NOP;   /* the second operator's pair */ \
    R7.L = R3.H * R7.L (T); \
    R3.L = R3.L + R7.L (NS);                   /* b */ \
    R7 = R3.L * R5.H (IS); \
    F1 = F1 >>> 1; \
    F1 = F1 + R7 (NS) || R7 = [P2] || NOP;     /* the next f1; the next first operator's pair */ \
    OUT \
    P2 = I3; \
    [P2] = Q; \
    [P2 + 4] = F0; \
    [P2 + 8] = F1; \
    (R7:4) = [SP++]; \
    RTS; \
    .size name, .-name

SAW(_ca_saw_set, OUT_SET)
SAW(_ca_saw_add, OUT_ADD)
SAW(_ca_saw_mix, OUT_MIX)
