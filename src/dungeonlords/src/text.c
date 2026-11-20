/* Translated game text.
   The DLL carries a table "English string hash -> Japanese (EUC-JP)" as resource DLJP/1.
   When the game opens one of its text files, we read the original from the game folder,
   rebuild it with the Japanese strings and hand the game a copy in %TEMP%\DungeonLordsJP.
   Nothing of the original data ships with the patch. */
#include "dljp.h"
#include <stdio.h>

typedef struct { uint32_t lo, hi, off; } Ent;

static const uint8_t *g_blob;
static const Ent *g_ent;
static uint32_t g_nent;
static char g_tmpdir[MAX_PATH];

uint64_t fnv64(const uint8_t *s, int n)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    int i;
    for (i = 0; i < n; i++) { h ^= s[i]; h *= 0x100000001b3ULL; }
    return h;
}

/* Japanese for an English string, or NULL */
const char *tr_lookup(const char *en, int n)
{
    uint64_t h;
    uint32_t lo = 0, hi = g_nent;
    if (!g_nent || n <= 0) return NULL;
    h = fnv64((const uint8_t *)en, n);
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        uint64_t k = (uint64_t)g_ent[mid].hi << 32 | g_ent[mid].lo;
        if (k == h) return (const char *)g_blob + g_ent[mid].off;
        if (k < h) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

static const char *tr(const char *en)
{
    const char *j = tr_lookup(en, (int)strlen(en));
    return j ? j : en;
}

/* ---- growable buffer ---- */
typedef struct { uint8_t *p; int n, cap; } Buf;
static void bput(Buf *b, const void *d, int n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 4096;
        b->p = (uint8_t *)realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}
static void bput32(Buf *b, uint32_t v) { bput(b, &v, 4); }
static void bset32(Buf *b, int at, uint32_t v) { memcpy(b->p + at, &v, 4); }
static void bputs(Buf *b, const char *s) { bput(b, s, (int)strlen(s) + 1); }

static uint8_t *read_all(const char *path, int *len)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, rd = 0;
    uint8_t *p;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    p = (uint8_t *)malloc(sz + 1);
    ReadFile(h, p, sz, &rd, NULL);
    CloseHandle(h);
    p[rd] = 0;
    *len = (int)rd;
    return p;
}

static int safe_str(const uint8_t *d, int len, uint32_t off) { return off < (uint32_t)len && memchr(d + off, 0, len - off) != NULL; }

/* count,(id,off)*,strings */
static int rebuild_idtable(const uint8_t *d, int len, Buf *o)
{
    uint32_t n = *(uint32_t *)d, i;
    int base;
    if (4 + n * 8 > (uint32_t)len) return 0;
    bput32(o, n);
    for (i = 0; i < n; i++) { bput32(o, ((uint32_t *)(d + 4))[i * 2]); bput32(o, 0); }
    for (i = 0; i < n; i++) {
        uint32_t off = ((uint32_t *)(d + 4))[i * 2 + 1];
        base = o->n;
        bset32(o, 4 + i * 8 + 4, base);
        bputs(o, safe_str(d, len, off) ? tr((const char *)d + off) : "");
    }
    return 1;
}

/* count,(id,off,len)*,strings */
static int rebuild_quest(const uint8_t *d, int len, Buf *o)
{
    uint32_t n = *(uint32_t *)d, i;
    if (4 + n * 12 > (uint32_t)len) return 0;
    bput32(o, n);
    for (i = 0; i < n; i++) { bput32(o, ((uint32_t *)(d + 4))[i * 3]); bput32(o, 0); bput32(o, 0); }
    for (i = 0; i < n; i++) {
        uint32_t off = ((uint32_t *)(d + 4))[i * 3 + 1], l = ((uint32_t *)(d + 4))[i * 3 + 2];
        char *tmp;
        const char *s;
        int at = o->n;
        if (off + l > (uint32_t)len) l = 0;
        tmp = (char *)malloc(l + 1);
        memcpy(tmp, d + off, l);
        tmp[l] = 0;
        s = tr(tmp);
        bputs(o, s);
        bset32(o, 4 + i * 12 + 4, at);
        bset32(o, 4 + i * 12 + 8, o->n - at);
        free(tmp);
    }
    return 1;
}

static int is_strtable(const uint8_t *b, uint32_t s)
{
    uint32_t c, i;
    if (s < 8) return 0;
    c = *(uint32_t *)b;
    if (!(c > 0 && c < 2000 && 4 + 4 * c <= s && ((uint32_t *)b)[1] == 4 + 4 * c && b[s - 1] == 0)) return 0;
    for (i = 1; i < c; i++) {
        uint32_t o = ((uint32_t *)b)[1 + i];
        if (o >= s || b[o - 1] != 0) return 0;
    }
    return 1;
}

static int is_keytable(const uint8_t *b, uint32_t s)
{
    uint32_t c;
    if (s < 9) return 0;
    c = *(uint32_t *)b;
    return c > 0 && c < 5000 && 4 + 5 * c <= s && *(uint32_t *)(b + 4) == 4 + 5 * c && b[8] == 3;
}

/* NPCDATA.PAK: (off,size)* then blobs; string tables and the topic table get translated */
static int rebuild_pak(const uint8_t *d, int len, Buf *o)
{
    uint32_t n = *(uint32_t *)d / 8, i, k;
    if (n == 0 || n * 8 > (uint32_t)len) return 0;
    for (i = 0; i < n; i++) { bput32(o, 0); bput32(o, 0); }
    for (i = 0; i < n; i++) {
        uint32_t off = ((uint32_t *)d)[i * 2], sz = ((uint32_t *)d)[i * 2 + 1];
        const uint8_t *b = d + off;
        int at = o->n;
        if (off + sz > (uint32_t)len) sz = 0;
        if (sz == 0) {
            /* keep empty entries pointing nowhere like the original */
            bset32(o, i * 8, off == 0 ? 0 : at);
            continue;
        }
        if (is_strtable(b, sz)) {
            uint32_t c = *(uint32_t *)b;
            bput32(o, c);
            for (k = 0; k < c; k++) bput32(o, 0);
            for (k = 0; k < c; k++) {
                uint32_t so = ((uint32_t *)b)[1 + k];
                bset32(o, at + 4 + k * 4, o->n - at);
                bputs(o, tr((const char *)b + so));
            }
        } else if (is_keytable(b, sz)) {
            uint32_t c = *(uint32_t *)b;
            bput32(o, c);
            for (k = 0; k < c; k++) { uint8_t z[5] = { 0, 0, 0, 0, b[4 + k * 5 + 4] }; bput(o, z, 5); }
            for (k = 0; k < c; k++) {
                uint32_t so = *(uint32_t *)(b + 4 + k * 5);
                uint32_t rel = o->n - at;
                memcpy(o->p + at + 4 + k * 5, &rel, 4);
                bputs(o, so < sz ? tr((const char *)b + so) : "");
            }
        } else bput(o, b, sz);
        bset32(o, i * 8, at);
        bset32(o, i * 8 + 4, o->n - at);
    }
    return 1;
}

static const char *base_name(const char *p)
{
    const char *b = p, *q;
    for (q = p; *q; q++) if (*q == '\\' || *q == '/' || *q == ':') b = q + 1;
    return b;
}

static const struct { const char *name; int kind; } g_files[] = {
    { "D6STRING.DAT", 0 }, { "INAME.DAT", 0 }, { "MNAME.DAT", 0 }, { "PNAME.DAT", 0 },
    { "QUESTTEXT.PAK", 1 }, { "NPCDATA.PAK", 2 },
};
static int g_built[8];

HANDLE text_open_hook(const char *name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    const char *b;
    int i;
    char out[MAX_PATH];
    if (!g_nent || !name || (acc & GENERIC_WRITE) || disp != OPEN_EXISTING) return NULL;
    b = base_name(name);
    for (i = 0; i < (int)(sizeof g_files / sizeof g_files[0]); i++)
        if (!_stricmp(b, g_files[i].name)) break;
    if (i == (int)(sizeof g_files / sizeof g_files[0])) return NULL;
    _snprintf(out, sizeof out, "%s\\%s", g_tmpdir, g_files[i].name);
    if (!g_built[i]) {
        int len = 0, ok = 0;
        uint8_t *d = read_all(name, &len);
        Buf o = { 0 };
        if (!d) return NULL;
        switch (g_files[i].kind) {
        case 0: ok = rebuild_idtable(d, len, &o); break;
        case 1: ok = rebuild_quest(d, len, &o); break;
        case 2: ok = rebuild_pak(d, len, &o); break;
        }
        free(d);
        if (ok) {
            HANDLE h = CreateFileA(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            DWORD wr;
            if (h != INVALID_HANDLE_VALUE) {
                WriteFile(h, o.p, o.n, &wr, NULL);
                CloseHandle(h);
                g_built[i] = 1;
            }
        }
        free(o.p);
        dlog("rebuilt %s ok=%d -> %d", b, ok, g_built[i]);
        if (!g_built[i]) return NULL;
    }
    {
        HANDLE h = CreateFileA(out, acc, share, sa, disp, flags, tmpl);
        return h == INVALID_HANDLE_VALUE ? NULL : h;
    }
}

void text_init(HMODULE self)
{
    HRSRC r = FindResourceA(self, MAKEINTRESOURCEA(1), "DLJP");
    HGLOBAL g;
    if (!r || !(g = LoadResource(self, r))) { dlog("no DLJP resource"); return; }
    g_blob = (const uint8_t *)LockResource(g);
    if (memcmp(g_blob, "DLJ1", 4)) { g_blob = NULL; return; }
    g_nent = *(uint32_t *)(g_blob + 4);
    g_ent = (const Ent *)(g_blob + 8);
    GetTempPathA(sizeof g_tmpdir, g_tmpdir);
    strcat(g_tmpdir, "DungeonLordsJP");
    CreateDirectoryA(g_tmpdir, NULL);
    dlog("translations: %u", g_nent);
}

/* ---- strings inside the exe: redirect push/mov imm32 and pointer tables to Japanese copies ---- */
typedef struct { uint32_t va, len, hlo, hhi, off; } XEnt;

static void patch_dword(uint8_t *p, uint32_t v)
{
    DWORD old;
    VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old);
    memcpy(p, &v, 4);
    VirtualProtect(p, 4, old, &old);
}

void exe_strings_init(HMODULE self)
{
    HRSRC r = FindResourceA(self, MAKEINTRESOURCEA(3), "DLJP");
    HGLOBAL g;
    const uint8_t *b;
    const XEnt *e;
    uint32_t n, i, k, nref = 0;
    uint8_t *base = (uint8_t *)0x400000;
    if (!r || !(g = LoadResource(self, r))) return;
    b = (const uint8_t *)LockResource(g);
    if (memcmp(b, "DLX1", 4)) return;
    n = *(uint32_t *)(b + 4);
    e = (const XEnt *)(b + 8);
    for (i = 0; i < n; i++) {
        uint64_t h = fnv64((const uint8_t *)e[i].va, e[i].len);
        uint32_t to = (uint32_t)(b + e[i].off);
        if (h != ((uint64_t)e[i].hhi << 32 | e[i].hlo) || ((uint8_t *)e[i].va)[e[i].len]) { dlog("exe string %08x mismatch", e[i].va); continue; }
        /* code: push imm32 (68), mov r32,imm32 (b8-bf), mov r/m32,imm32 (c7 ..) */
        for (k = 0x1001; k < 0x2c3000 - 4; k++) {
            uint8_t *p = base + k;
            if (*(uint32_t *)p != e[i].va) continue;
            if (p[-1] == 0x68 || (p[-1] >= 0xb8 && p[-1] <= 0xbf) || p[-2] == 0xc7 || p[-3] == 0xc7 || p[-4] == 0xc7 || p[-7] == 0xc7) {
                patch_dword(p, to); nref++;
            }
        }
        /* pointer tables in .rdata/.data (raw part) */
        for (k = 0x2c3000; k < 0x3ca000; k += 4) {
            uint8_t *p = base + k;
            if (*(uint32_t *)p == e[i].va) { patch_dword(p, to); nref++; }
        }
    }
    dlog("exe strings: %u, refs patched %u", n, nref);
}
