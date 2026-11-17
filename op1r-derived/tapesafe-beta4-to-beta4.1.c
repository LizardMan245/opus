/* The core's tape writing (tape_write), as shipped in Opus 2 beta 4 and 4.1 (the core); replaced in beta 4.2.
 *
 * Derived from op1r by pcppcp (https://github.com/pcppcp/op1r; its sources: https://github.com/pcppcp/op1r-src):
 * synth_sdk/tape/safewrite.c (tape_safe_write).
 * Licensed under the GNU General Public License, version 3 (GPL-3.0), as op1r is:
 * https://www.gnu.org/licenses/gpl-3.0.html
 *
 * Only the parts derived from op1r are here, with the types and definitions needed to read them; the
 * comments of the shipped source are left out. */

typedef unsigned int u32;

#define PIECE           4096u

static u32 copy[PIECE / 4];

int tape_write(int fd, const void *buf, unsigned n);

int tape_write(int fd, const void *buf, unsigned n)
{
    mod_tape_write(fd, buf, n);
    const unsigned char *src = (const unsigned char *)buf;
    unsigned done = 0;
    while (done < n) {
        const unsigned k = n - done < PIECE ? n - done : PIECE;
        if (!((u32)(src + done) & 3)) {
            const u32 *s = (const u32 *)(src + done);
            for (unsigned i = 0; i < (k + 3) / 4; ++i)
                copy[i] = s[i];
        } else
            for (unsigned i = 0; i < k; ++i)
                ((unsigned char *)copy)[i] = src[done + i];
        const int r = fw_yaffs_write(fd, copy, k);
        if (r < 0)
            return done ? (int)done : r;
        done += (unsigned)r;
        if ((unsigned)r < k)
            break;
    }
    return (int)done;
}
