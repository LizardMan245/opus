/* outatime's arms in Blackfin assembly (the same sound for less CPU): one arm's pass over a voice
 * block, bit for bit as ota_dsp.c's C loops (checked: their output's hash the same).
 *
 * The C: p += s; q = a + (p >> 24); v = (q[0] + (((short)(q[256] - q[0]) * w) >> 15)) >> 1; the first arm stores v,
 * the others add it to the sum (a short). Exactly the same here as v = (q[0] (32768 - w) + q[256] w) >> 16: the
 * neighbouring era waves never differ by more than 22,057 (make_tables.py's bank), so the (short) cast changes
 * nothing, and q[0] 32768 has no bits below 15; the shift of 15 then 1 is one of 16 (floor of a floor). The two
 * products are MAC1's mixed mode (signed by unsigned: 32768 - w reaches 32768), the sum exact in the accumulator, and
 * its top half taken truncated (T: bits 31-16, the floor; |sum| < 2^31). GCC's loop spent a move, a scaled add and an
 * offset load (a 32-bit instruction) on the two reads and a 32-bit multiply's shifts on the blend, 13-14 instructions a
 * sample with the address used at once; here the next sample's address is made while this one's products run.
 *
 * Called from C: R0-R2 the first three arguments, the rest at [SP + 12] on; R4-R7, P3-P5 kept; the loop registers
 * are the caller's to lose (GCC keeps no hardware loop across a call); L0 and L1 are 0 (the ABI's). */

        .text

/* unsigned ota_arm_bf(short *sum, const short *a, unsigned p, unsigned s, unsigned uw, int add): 128 samples;
 * uw: 32768 - w in the high half, w in the low; add 0: sum = v, else sum += v. Returns the arm's phase after them. */
        .global _ota_arm_bf
        .type _ota_arm_bf, STT_FUNC
_ota_arm_bf:
        [--SP] = (R7:4, P5:3);
        I0 = R0;                            /* sum, read */
        I1 = R0;                            /* sum, written */
        P0 = R1;                            /* a */
        R4 = R2;                            /* p */
        R5 = [SP + 40];                     /* s (12 + 7 saved words) */
        R6 = [SP + 44];                     /* uw */
        R3 = [SP + 48];                     /* add */
        R0 = 512 (X);
        P5 = R0;                            /* the next era's wave: 256 samples on */
        R0 = 128 (X);
        P2 = R0;
        R4 = R4 + R5;                       /* the first sample's place */
        R0 = R4 >> 24;
        P1 = R0;
        P1 = P0 + (P1 << 1);
        P3 = P1 + P5;
        CC = R3 == 0;
        IF CC JUMP .Lset;

        LSETUP (.La0, .La1) LC0 = P2;
.La0:   R4 = R4 + R5 (NS) || R1 = W[P1] (X) || R3.L = W[I0++];   /* the next phase; q0; the sum so far */
        R0 = R4 >> 24 || R2 = W[P3] (X);    /* the next index; q1 */
        P1 = R0;
        A1 = R1.L * R6.H (M, IS);           /* q0 (32768 - w) */
        A1 += R2.L * R6.L (M, IS);          /* + q1 w */
        P1 = P0 + (P1 << 1);
        R7.H = A1 (T);                      /* v: the sum >> 16 */
        P3 = P1 + P5;
        R7.H = R7.H + R3.L (NS);
.La1:   W[I1++] = R7.H;
        JUMP .Lout;

.Lset:  LSETUP (.Ls0, .Ls1) LC0 = P2;
.Ls0:   R4 = R4 + R5 (NS) || R1 = W[P1] (X);
        R0 = R4 >> 24 || R2 = W[P3] (X);
        P1 = R0;
        A1 = R1.L * R6.H (M, IS);
        A1 += R2.L * R6.L (M, IS);
        P1 = P0 + (P1 << 1);
        R7.H = A1 (T);
        P3 = P1 + P5;
.Ls1:   W[I1++] = R7.H;

.Lout:  R0 = R4 - R5;                       /* the loop stepped once past its last sample */
        (R7:4, P5:3) = [SP++];
        RTS;
        .size _ota_arm_bf, .-_ota_arm_bf
