/* The op1r bridge's stroke font, helpers and sequencer store, as shipped in Opus 2 beta 3, 3.1 and 3.2 (opus.bridge 0.9.0) and beta 4 (opus.bridge 0.9.1); replaced in beta 4.2 (opus.bridge 0.11.0).
 *
 * Derived from op1r by pcppcp (https://github.com/pcppcp/op1r; its sources: https://github.com/pcppcp/op1r-src):
 * synth_sdk/screens/stroke_font.h, synth_sdk/seq/seq_text.h, synth_sdk/seq/euclid.c, synth_sdk/op1_plugin.h.
 * Licensed under the GNU General Public License, version 3 (GPL-3.0), as op1r is:
 * https://www.gnu.org/licenses/gpl-3.0.html
 *
 * Only the parts derived from op1r are here, with the types and definitions needed to read them; the
 * comments of the shipped source are left out. */

typedef unsigned int u32;

typedef unsigned char u8;

#define RASTER_LINE     ((void (*)(void *, const int *, const int *, int, const u8 *))0x01A24A4C)

static void sq_line(void *r, int x0, int y0, int x1, int y1, int w, const u8 *c)
{
    const int a[2] = { x0, y0 }, b[2] = { x1, y1 };
    RASTER_LINE(r, a, b, w, c);
}

static const struct { char c; u8 narrow; const char *s; } FONT[] = {
    { 'A', 0, "06020220204242460444" }, { 'B', 0, "0006003030414142423303333344444545363606" },
    { 'C', 0, "400000060646" }, { 'D', 0, "000600303041414545363606" }, { 'E', 0, "4000000606460333" },
    { 'F', 0, "400000060333" }, { 'G', 0, "40000006064646434323" }, { 'H', 0, "000640460343" },
    { 'I', 0, "202610301636" }, { 'J', 0, "404646060604" }, { 'K', 0, "000640030346" }, { 'L', 0, "00060646" },
    { 'M', 0, "0600002323404046" }, { 'N', 0, "060000464640" }, { 'O', 0, "0040404646060600" },
    { 'P', 0, "0600004040434303" }, { 'Q', 0, "00404046460606002446" }, { 'R', 0, "06000040404343031346" },
    { 'S', 0, "40000003034343464606" }, { 'T', 0, "00402026" }, { 'U', 0, "000606464640" }, { 'V', 0, "00262640" },
    { 'W', 0, "0016162323363640" }, { 'X', 0, "00464006" }, { 'Y', 0, "002340232326" }, { 'Z', 0, "004040060646" },
    { '0', 0, "00404046460606004006" }, { '1', 1, "11202026" }, { '2', 0, "00404043430303060646" },
    { '3', 0, "0040404646061343" }, { '4', 0, "000303434046" }, { '5', 0, "40000003034343464606" },
    { '6', 0, "40000006064646434303" }, { '7', 0, "00404026" }, { '8', 0, "00404046460606000343" },
    { '9', 0, "43030300004040464606" }, { '#', 0, "1016303602420444" }, { '/', 0, "4006" },
    { '+', 0, "21250343" }, { '-', 0, "0343" }, { '%', 0, "400600113546" }, { '.', 1, "0506" },
    { ':', 1, "01020405" },
};

#define ADVANCE         6

#define ADVANCE_NARROW  3

static int sq_text(void *r, int x, int y, int s, const char *t, int w, const u8 *c)
{
    for (; *t; ++t) {
        char ch = *t;
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');
        int adv = ADVANCE;
        for (unsigned k = 0; k < sizeof FONT / sizeof FONT[0]; ++k) {
            if (FONT[k].c != ch)
                continue;
            for (const char *p = FONT[k].s; p[0] && p[1] && p[2] && p[3]; p += 4)
                sq_line(r, x + (p[0] - '0') * s, y + (p[1] - '0') * s, x + (p[2] - '0') * s, y + (p[3] - '0') * s, w,
                        c);
            if (FONT[k].narrow)
                adv = ADVANCE_NARROW;
            break;
        }
        x += adv * s;
    }
    return x;
}

static char *sq_cat(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

static char *sq_num(char *p, u32 v, int digits)
{
    char d[10];
    int n = 0;
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 10);
    while (n < digits && n < 10)
        d[n++] = '0';
    while (n)
        *p++ = d[--n];
    *p = 0;
    return p;
}

static void sq_node_rotate(void *node, int cx, int cy, int s15, int c15)
{
    const int c = c15 >> 5, s = s15 >> 5;
    const int m[9] = { c, s, 0, -s, c, 0, cx - ((cx * c - cy * s) >> 10), cy - ((cx * s + cy * c) >> 10), 1024 };
    fw_node_set_matrix(node, m);
}

static void sq_dot(void *r, int x, int y, int w, const u8 *c)
{
    const int h = w / 2, t = w * 7 / 10;
    sq_line(r, x - h, y, x + h, y, t, c);
    sq_line(r, x, y - h, x, y + h, t, c);
}

static int sq_join(char *out, int n, const int *v, int k)
{
    int o = 0;
    for (int i = 0; i < k; ++i) {
        char d[4];
        int m = 0, x = v[i];
        do
            d[m++] = (char)('0' + x % 10);
        while ((x /= 10) && m < 4);
        if (o + m + 2 >= n)
            break;
        if (i)
            out[o++] = ',';
        while (m)
            out[o++] = d[--m];
    }
    out[o] = 0;
    return o;
}

static int sq_split(const char *s, int len, int *v, int max)
{
    int k = 0, x = -1;
    for (int i = 0; i <= len; ++i) {
        const char ch = i < len ? s[i] : ',';
        if (ch >= '0' && ch <= '9') {
            x = (x < 0 ? 0 : x * 10) + (ch - '0');
            if (x > 9999)
                return -1;
        } else if (ch == ',' && x >= 0 && k < max) {
            v[k++] = x;
            x = -1;
        } else
            return -1;
    }
    return k;
}

struct sq_store {
    u8 dirty, saving, pad[2];
    const u32 *vt;
    u32 more[2];
    const char *name;
    int (*ser)(int pos, char *out, int n);
    int (*parse)(int pos, const char *s, int len);
    char *sql;
    short cap, maxlen;
    u8 ready;
};

#define DB_USER         ((void *(*)(void))0x0196DD44)

#define DB_GET          ((void (*)(void *, void *, const char *))0x0196FBD0)

#define STR_FREE        ((void (*)(void *))0x01A5E310)

#define BG_REGISTER     ((void (*)(void *))0x0193F010)

#define BG_DIRTY        ((void (*)(void *))0x0193EF3C)

struct os_string {
    u32 alloc;
    union {
        char buf[16];
        char *ptr;
    } u;
    u32 size, cap, pad;
};

static void store_table(struct sq_store *st, void *db)
{
    fw_snprintf(st->sql, (unsigned)st->cap, "CREATE TABLE IF NOT EXISTS %s (is_synth INTEGER PRIMARY KEY, data TEXT NOT NULL)",
                st->name);
    fw_db_exec(db, st->sql);
}

static void store_nothing(void *self)
{
    (void)self;
}

static void store_save(void *self)
{
    struct sq_store *const st = (struct sq_store *)self;
    void *const db = DB_USER();
    store_table(st, db);
    for (int pos = 0; pos < 2; ++pos) {
        int o = fw_snprintf(st->sql, (unsigned)st->cap, "INSERT OR REPLACE INTO %s VALUES (%c,'", st->name, pos ? '0' : '1');
        if (o < 0 || o >= st->cap - 8)
            return;
        o += st->ser(pos, st->sql + o, st->cap - o - 4);
        st->sql[o++] = '\'';
        st->sql[o++] = ')';
        st->sql[o] = 0;
        fw_db_exec(db, st->sql);
    }
}

static const u32 STORE_VT[6] = { 0, 0, 0, (u32)store_nothing, 0, (u32)store_save };

static void sq_store_dirty(struct sq_store *st)
{
    if (st->ready)
        BG_DIRTY(st);
}

static void sq_store_load(struct sq_store *st)
{
    if (st->ready)
        return;
    void *const db = DB_USER();
    store_table(st, db);
    for (int pos = 0; pos < 2; ++pos) {
        struct os_string s;
        for (unsigned i = 0; i < sizeof s / 4; ++i)
            ((u32 *)&s)[i] = 0;
        s.cap = 15;
        fw_snprintf(st->sql, (unsigned)st->cap, "SELECT data FROM %s WHERE is_synth=%c", st->name, pos ? '0' : '1');
        DB_GET(&s, db, st->sql);
        const char *const p = s.cap > 15 ? s.u.ptr : s.u.buf;
        if (s.size && s.size < (u32)st->maxlen)
            st->parse(pos, p, (int)s.size);
        if (s.cap > 15)
            STR_FREE(s.u.ptr);
    }
    BG_REGISTER(st);
    st->vt = STORE_VT;

    st->ready = 1;
}
