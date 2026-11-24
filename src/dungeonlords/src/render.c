/* Japanese text rendering.
   The game draws every string through one function (0x461580) with bitmap fonts of 256 code
   slots. Japanese text is stored as EUC-JP (both bytes 0xA1-0xFE, never ASCII, so the game's
   markup characters '@' ']' '~' '%' stay safe). For each game font we keep a companion font
   whose texture is a glyph atlas filled on demand with GDI, rendered at 2x for sharpness. */
#include "dljp.h"
#include <stdio.h>
#include <math.h>
#include <d3d9.h>
#include <intrin.h>

static const GlyphFn g_glyph = (GlyphFn)A_GLYPH;
static const FlushFn g_flush = (FlushFn)A_FLUSH;
static const MkTexFn g_mktex = (MkTexFn)A_MKTEX;

#define NFONT 64
#define SS 2                 /* atlas pixels per virtual pixel */
#define ATLAS 1024           /* atlas size in pixels */
#define MAXSLOT 1600

typedef struct {
    GFont f;                 /* fake game font: texw/texh are virtual (ATLAS/SS) */
    GFont *src;
    GGlyph g[MAXSLOT];
    uint16_t slot_code[MAXSLOT];
    uint32_t slot_use[MAXSLOT];
    int16_t code_slot[94 * 94];
    int nslot, maxslot, cols;
    int cell;                /* virtual cell size */
    int em, adv, yoff;
    uint32_t fill, shadow;
    int has_shadow;
    HFONT hf;
} JFont;

static JFont *g_jf[NFONT];
static uint32_t g_stamp;
static HDC g_dc;
static HBITMAP g_dib;
static uint32_t *g_dibbits;
static int g_dibsize;
static WCHAR g_face[64] = L"DLJP Serif";
static int g_weight = 600;
static float g_emk = 1.05f;

int jp_lead(const uint8_t *s)
{
    return s[0] >= 0xa1 && s[0] <= 0xfe && s[1] >= 0xa1 && s[1] <= 0xfe;
}

static int code_index(int c) { return ((c >> 8) - 0xa1) * 94 + ((c & 255) - 0xa1); }

/* ---- font texture sampling: fill / shadow colour of the original font ---- */
static void sample_colors(JFont *j, GFont *f)
{
    IDirect3DTexture9 *t = f->tex ? (IDirect3DTexture9 *)f->tex[0] : NULL;
    D3DLOCKED_RECT lr;
    double fr = 0, fg = 0, fb = 0, sr = 0, sg = 0, sb = 0;
    int nf = 0, ns = 0, total = 0, c, maxl = 0, pass;
    j->fill = 0xffe8e8e8; j->shadow = 0xff101010; j->has_shadow = 1;
    if (!t || FAILED(IDirect3DTexture9_LockRect(t, 0, &lr, NULL, D3DLOCK_READONLY))) return;
    for (pass = 0; pass < 2; pass++) {
        for (c = 'A'; c <= 'Z'; c++) {
            int gi = f->map[c], x, y;
            GGlyph *g;
            if (gi < 0) continue;
            g = &f->glyphs[gi];
            for (y = g->y; y < g->y + g->h && y < f->texh; y++)
                for (x = g->x; x < g->x + g->w && x < f->texw; x++) {
                    uint32_t p = ((uint32_t *)((uint8_t *)lr.pBits + y * lr.Pitch))[x];
                    int a = p >> 24, r = (p >> 16) & 255, gg = (p >> 8) & 255, b = p & 255;
                    int l = (r * 3 + gg * 6 + b) / 10;
                    if (a < 200) continue;
                    if (pass == 0) { if (l > maxl) maxl = l; total++; continue; }
                    if (l * 2 >= maxl) { fr += r; fg += gg; fb += b; nf++; }
                    else if (l * 4 < maxl) { sr += r; sg += gg; sb += b; ns++; }
                }
        }
    }
    IDirect3DTexture9_UnlockRect(t, 0);
    if (nf) j->fill = 0xff000000 | ((int)(fr / nf) << 16) | ((int)(fg / nf) << 8) | (int)(fb / nf);
    j->has_shadow = total && ns * 10 > total;
    if (ns) j->shadow = 0xff000000 | ((int)(sr / ns) << 16) | ((int)(sg / ns) << 8) | (int)(sb / ns);
}

static JFont *jfont_get(int fid)
{
    GFont *f;
    JFont *j;
    int gi, caph, yoffa, base;
    if (fid < 0 || fid >= NFONT) return NULL;
    f = G_FONTS[fid];
    if (!f) return NULL;
    if (g_jf[fid] && g_jf[fid]->src == f) return g_jf[fid];
    j = (JFont *)calloc(1, sizeof(JFont));
    j->src = f;
    gi = f->map['A'];
    if (gi < 0) gi = 0;
    caph = f->glyphs[gi].h;
    yoffa = f->glyphs[gi].yoff;
    j->em = (int)floor(caph * g_emk + 0.5);
    if (j->em < 8) j->em = 8;
    j->adv = j->em;
    j->cell = j->em + 3;
    j->cols = ATLAS / (j->cell * SS);
    j->maxslot = j->cols * j->cols;
    if (j->maxslot > MAXSLOT) j->maxslot = MAXSLOT;
    /* baseline of the Latin capitals = bottom of 'A' minus its 1px shadow */
    base = caph - yoffa - 1;                       /* relative to the y passed to the emitter */
    j->yoff = (int)floor(j->em * 0.88 + 0.5) + 1 - base;   /* glyph top = y - yoff; +1 = cell padding */
    memset(j->code_slot, 0xff, sizeof j->code_slot);
    j->f = *f;
    j->f.texw = ATLAS / SS;
    j->f.texh = ATLAS / SS;
    j->f.count = j->maxslot;
    j->f.glyphs = j->g;
    j->f.tex = NULL;
    g_jf[fid] = j;
    dlog("jfont %d: capH %d yoffA %d em %d yoff %d slots %d", fid, caph, yoffa, j->em, j->yoff, j->maxslot);
    return j;
}

static int ensure_texture(JFont *j)
{
    int desc[3];
    if (j->f.tex) return 1;
    desc[0] = ATLAS; desc[1] = ATLAS;
    desc[2] = (int)calloc(ATLAS * ATLAS, 4);
    j->f.tex = g_mktex(desc);
    free((void *)desc[2]);
    if (!j->f.tex) return 0;
    sample_colors(j, j->src);
    j->hf = CreateFontW(-j->em * SS, 0, 0, 0, g_weight, 0, 0, 0, SHIFTJIS_CHARSET, OUT_TT_PRECIS,
                        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, g_face);
    dlog("jfont tex ok fill %08x shadow %08x (%d)", j->fill, j->shadow, j->has_shadow);
    return 1;
}

static void ensure_dib(int px)
{
    BITMAPINFO bi = { 0 };
    if (g_dib && g_dibsize >= px) return;
    if (!g_dc) g_dc = CreateCompatibleDC(NULL);
    if (g_dib) DeleteObject(g_dib);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = px;
    bi.bmiHeader.biHeight = -px;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    g_dib = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, (void **)&g_dibbits, NULL, 0);
    g_dibsize = px;
    SelectObject(g_dc, g_dib);
}

static void render_slot(JFont *j, int slot, int code)
{
    int px = j->cell * SS, x, y, sh = SS;
    uint8_t eb[3] = { (uint8_t)(code >> 8), (uint8_t)code, 0 };
    WCHAR w[2] = { 0 };
    IDirect3DTexture9 *t = (IDirect3DTexture9 *)j->f.tex[0];
    D3DLOCKED_RECT lr;
    RECT r;
    int sx = (slot % j->cols) * px, sy = (slot / j->cols) * px;
    static uint8_t cov[256 * 256];
    if (!MultiByteToWideChar(20932, 0, (char *)eb, 2, w, 1)) w[0] = L'?';
    ensure_dib(px);
    memset(g_dibbits, 0, g_dibsize * g_dibsize * 4);
    SelectObject(g_dc, j->hf);
    SetBkMode(g_dc, TRANSPARENT);
    SetTextColor(g_dc, RGB(255, 255, 255));
    SetTextAlign(g_dc, TA_BASELINE | TA_LEFT);
    TextOutW(g_dc, SS, SS + (int)floor(j->em * SS * 0.88 + 0.5), w, 1);
    GdiFlush();
    for (y = 0; y < px; y++)
        for (x = 0; x < px; x++) {
            uint32_t p = g_dibbits[y * g_dibsize + x];
            cov[y * px + x] = (uint8_t)((((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255)) / 3);
        }
    r.left = sx; r.top = sy; r.right = sx + px; r.bottom = sy + px;
    if (FAILED(IDirect3DTexture9_LockRect(t, 0, &lr, &r, 0))) return;
    for (y = 0; y < px; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)lr.pBits + y * lr.Pitch);
        for (x = 0; x < px; x++) {
            int a1 = cov[y * px + x];
            int a2 = (j->has_shadow && x >= sh && y >= sh) ? cov[(y - sh) * px + (x - sh)] : 0;
            int ao = a1 + a2 * (255 - a1) / 255, i;
            uint32_t out = 0;
            if (ao) {
                out = (uint32_t)ao << 24;
                for (i = 0; i < 24; i += 8) {
                    int cf = (j->fill >> i) & 255, cs = (j->shadow >> i) & 255;
                    int v = (cf * a1 + cs * a2 * (255 - a1) / 255) / ao;
                    out |= (uint32_t)(v > 255 ? 255 : v) << i;
                }
            }
            row[x] = out;
        }
    }
    IDirect3DTexture9_UnlockRect(t, 0);
    j->g[slot].ch = 0;
    j->g[slot].x = sx / SS;
    j->g[slot].y = sy / SS;
    j->g[slot].w = j->cell;
    j->g[slot].h = j->cell;
    j->g[slot].yoff = j->yoff;
}

static int jfont_slot(JFont *j, int code)
{
    int ci = code_index(code), s = j->code_slot[ci], i, best;
    uint32_t bu;
    if (s >= 0) { j->slot_use[s] = g_stamp; return s; }
    if (j->nslot < j->maxslot) s = j->nslot++;
    else {
        best = -1; bu = 0xffffffff;
        for (i = 0; i < j->maxslot; i++)
            if (j->slot_use[i] != g_stamp && j->slot_use[i] < bu) { bu = j->slot_use[i]; best = i; }
        if (best < 0) return -1;
        s = best;
        j->code_slot[code_index(j->slot_code[s])] = -1;
    }
    j->slot_code[s] = (uint16_t)code;
    j->code_slot[ci] = (int16_t)s;
    j->slot_use[s] = g_stamp;
    render_slot(j, s, code);
    return s;
}

/* ---- replacements for the game's text functions ---- */
#define MAXCH 168

/* test mode: remember every English string that reaches the screen untranslated */
#ifdef DLJP_TEST
static void log_untranslated(const uint8_t *s)
{
    static uint32_t seen[8192];
    static int nseen;
    uint32_t h = 2166136261u;
    const uint8_t *p;
    int letters = 0, i;
    int run = 0, maxrun = 0, jp = 0;
    for (p = s; *p; p++) {
        if (*p >= 0x80) { jp = 1; run = 0; }
        else if ((*p | 32) >= 'a' && (*p | 32) <= 'z') { letters++; if (++run > maxrun) maxrun = run; }
        else run = 0;
        h = (h ^ *p) * 16777619u;
    }
    if (letters < 2 || (jp && maxrun < 4)) return;
    for (i = 0; i < nseen; i++) if (seen[i] == h) return;
    if (nseen < 8192) seen[nseen++] = h;
    dlog("UNTR|%s", s);
}
#endif

/* savegame names keep the English "Save N - ... - Lvl N" (so the English game still lists them);
   show those two words in Japanese */
static const uint8_t *disp_fix(const uint8_t *s, uint8_t *buf, int cap)
{
    static char jsave[16], jlvl[16];
    const uint8_t *p;
    int n;
    if (!jsave[0]) {
        WideCharToMultiByte(20932, 0, L"\x30bb\x30fc\x30d6 ", -1, jsave, sizeof jsave, NULL, NULL);
        WideCharToMultiByte(20932, 0, L"\x30ec\x30d9\x30eb ", -1, jlvl, sizeof jlvl, NULL, NULL);
    }
    if (!strncmp((const char *)s, "Save ", 5) && s[5] >= '0' && s[5] <= '9') {
        _snprintf((char *)buf, cap, "%s%s", jsave, s + 5);
        buf[cap - 1] = 0;
        return buf;
    }
    n = (int)strlen((const char *)s);
    for (p = s + n; p > s && p[-1] >= '0' && p[-1] <= '9'; p--) ;
    if (p < s + n && p - s >= 4 && !strncmp((const char *)p - 4, "Lvl ", 4)) {
        int k = (int)(p - 4 - s);
        if (k + 16 + n < cap) {
            memcpy(buf, s, k);
            strcpy((char *)buf + k, jlvl);
            strcat((char *)buf, (const char *)p);
            return buf;
        }
    }
    return s;
}

static void __fastcall my_drawstring(int fid, const uint8_t *s, int x, int y, int color, float scale)
{
    uint8_t fixbuf[256];
    GFont *f;
    JFont *j = NULL;
    float fx, fy, inv;
    int vi = 0, n = 0, jn = 0, filt, i;
    struct { float x; uint16_t code; } jq[MAXCH];
    if (!((double)scale > 0.05)) return;
    f = G_FONTS[fid];
    s = disp_fix(s, fixbuf, sizeof fixbuf);
#ifdef DLJP_TEST
    if (g_test) {
        log_untranslated(s);
        if (s[0] >= 0xa1 && s[1] == 0) dlog("LONE %02x font %d from %p", s[0], fid, _ReturnAddress());
        if (strlen((const char *)s) < 12 && s[0] >= 0x80 && !jp_lead(s)) dlog("ODD %02x %02x %02x %02x font %d from %p", s[0], s[1], s[2], s[3], fid, _ReturnAddress());
        if (s[-1] >= 0xa1 && jp_lead(s)) dlog("MID %02x|%02x %02x font %d from %p", s[-1], s[0], s[1], fid, _ReturnAddress());
        if (s[0] && s[1] && !jp_lead(s) && jp_lead(s + 1)) dlog("SKEW %02x %02x %02x font %d from %p", s[0], s[1], s[2], fid, _ReturnAddress());
    }
#endif
    inv = 1.0f / scale;
    fx = (float)x * inv;
    fy = (float)y * inv;
    g_stamp++;
    while (*s) {
        if (jp_lead(s)) {
            if (!j) j = jfont_get(fid);
            if (jn < MAXCH) { jq[jn].x = fx; jq[jn].code = (uint16_t)(s[0] << 8 | s[1]); jn++; }
            fx += (float)(j->adv + f->spacing);
            s += 2;
            continue;
        }
        if (*s == ' ') fx += (float)f->spacew;
        else if (f->map[*s] != -1 && vi + 6 < 0x400) {
            float a = g_glyph(f, f->map[*s], fx, fy, color, vi, scale);
            vi += 6; n++;
            fx += a;
            fx += (float)f->spacing;
        }
        s++;
    }
    filt = (G_SCRW == 0x400 && scale == 1.0f) ? f->filt : 1;
    g_flush(f->tex, filt, n * 2, G_VERTS);
    if (jn && j && ensure_texture(j)) {
        vi = 0; n = 0;
        for (i = 0; i < jn; i++) {
            int sl = jfont_slot(j, jq[i].code);
            if (sl < 0 || vi + 6 >= 0x400) continue;
            g_glyph(&j->f, sl, jq[i].x, fy, color, vi, scale);
            vi += 6; n++;
        }
        if (n) g_flush(j->f.tex, 1, n * 2, G_VERTS);
    }
}

static int char_w(int fid, GFont *f, const uint8_t *s, int *len)
{
    if (jp_lead(s)) {
        JFont *j = jfont_get(fid);
        *len = 2;
        return j->adv + f->spacing;
    }
    *len = 1;
    if (*s == ' ') return f->spacew;
    if (f->map[*s] == -1) return 0;
    return f->glyphs[f->map[*s]].w + f->spacing;
}

static int __fastcall my_strwidth(int fid, const uint8_t *s)
{
    GFont *f = G_FONTS[fid];
    int w = 0, l;
    uint8_t fixbuf[256];
    s = disp_fix(s, fixbuf, sizeof fixbuf);
    while (*s) { w += char_w(fid, f, s, &l); s += l; }
    return w;
}

/* bytes of s that fit in maxw (whole characters) */
static int __fastcall my_charsfit(int fid, const uint8_t *s, int maxw)
{
    GFont *f = G_FONTS[fid];
    int w = 0, n = 0, l;
    while (s[n]) {
        w += char_w(fid, f, s + n, &l);
        if (w > maxw) break;
        n += l;
    }
    return n;
}

/* EUC-JP characters that must not start a line */
static int no_start(const uint8_t *p)
{
    static const char *list = "、。，．・：；？！ー）」』】〕｝〉》ゃゅょっぁぃぅぇぉャュョッァィゥェォ…～";
    static uint16_t codes[64];
    static int nc = -1;
    int i;
    if (nc < 0) {
        /* list is UTF-8 source; convert once to EUC-JP codes */
        WCHAR wb[64];
        char eb[256];
        int nw = MultiByteToWideChar(CP_UTF8, 0, list, -1, wb, 64), ne;
        ne = WideCharToMultiByte(20932, 0, wb, nw - 1, eb, sizeof eb, NULL, NULL);
        nc = 0;
        for (i = 0; i + 1 < ne && nc < 64; i += 2) codes[nc++] = (uint16_t)((uint8_t)eb[i] << 8 | (uint8_t)eb[i + 1]);
    }
    if (!jp_lead(p)) return *p == '.' || *p == ',' || *p == '!' || *p == '?' || *p == ')';
    for (i = 0; i < nc; i++) if (codes[i] == (p[0] << 8 | p[1])) return 1;
    return 0;
}

static int no_end(const uint8_t *p)
{
    /* 「『（ */
    return jp_lead(p) && p[0] == 0xa1 && (p[1] == 0xd6 || p[1] == 0xd8 || p[1] == 0xca);
}

static int __fastcall my_wrapbreak(int fid, const uint8_t *s, int maxw)
{
    int fit = my_charsfit(fid, s, maxw), n, len = (int)strlen((const char *)s), i, l, prev, best = 0;
    if (fit >= len) n = fit;
    else {
        /* candidate break positions: after a space, or between two characters where one is Japanese */
        prev = -1;
        for (i = 0; i <= fit; i += l) {
            l = jp_lead(s + i) ? 2 : 1;
            if (i > 0) {
                int ok = 0;
                if (s[i - 1] == ' ') ok = 1;
                else if (prev >= 0 && (jp_lead(s + prev) || jp_lead(s + i)) && s[i] != ' ') ok = 1;
                if (ok && no_start(s + i)) ok = 0;
                if (ok && prev >= 0 && no_end(s + prev)) ok = 0;
                if (ok) best = i;
            }
            prev = i;
            if (!s[i]) break;
        }
        n = best > 0 ? best : fit;
        /* a closing mark that overflowed by one char hangs on this line instead of starting the next */
        if (n == fit && fit < len && no_start(s + fit)) n = fit + (jp_lead(s + fit) ? 2 : 1);
    }
    while (n > 0 && s[n - 1] == ' ') n--;
    return n;
}

/* ---- the game's Latin-1 case converters, made to skip Japanese characters ---- */
static int is_up(int c) { return (c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xdf); }
static int is_lo(int c) { return (c >= 'a' && c <= 'z') || (c >= 0xe0 && c <= 0xff); }

static void case_conv(uint8_t *s, int mode)
{
    int word = 1;            /* next letter starts a word */
    while (*s) {
        int c = *s;
        if (jp_lead(s)) { s += 2; word = 1; continue; }
        switch (mode) {
        case 0: if (is_lo(c)) *s = (uint8_t)(c - 0x20); break;                       /* upper */
        case 1: if (is_up(c)) *s = (uint8_t)(c + 0x20); break;                       /* lower */
        case 2: case 3:
            if (is_up(c) || is_lo(c)) {
                if (word) { if (is_lo(c)) *s = (uint8_t)(c - 0x20); }
                else if (mode == 3 && is_up(c)) *s = (uint8_t)(c + 0x20);
                word = 0;
            } else if (c != 39) word = 1;
            break;
        }
        s++;
    }
}
static void __fastcall my_upper(uint8_t *s) { case_conv(s, 0); }
static void __fastcall my_lower(uint8_t *s) { case_conv(s, 1); }
static void __fastcall my_capwords(uint8_t *s) { case_conv(s, 2); }
static void __fastcall my_title(uint8_t *s) { case_conv(s, 3); }

extern HMODULE g_self;

static void load_font(void)
{
    HRSRC r = FindResourceA(g_self, MAKEINTRESOURCEA(2), "DLJP");
    HGLOBAL g;
    DWORD n = 0;
    if (!r || !(g = LoadResource(g_self, r))) { lstrcpyW(g_face, L"Yu Mincho"); return; }
    if (!AddFontMemResourceEx(LockResource(g), SizeofResource(g_self, r), NULL, &n) || !n) lstrcpyW(g_face, L"Yu Mincho");
}

void render_init(void)
{
#ifdef DLJP_TEST
    char buf[128];
#endif
    load_font();
#ifdef DLJP_TEST
    /* 字形の調整用（試験版だけ） */
    if (GetEnvironmentVariableA("DLJP_FACE", buf, sizeof buf)) MultiByteToWideChar(CP_ACP, 0, buf, -1, g_face, 64);
    if (GetEnvironmentVariableA("DLJP_WEIGHT", buf, sizeof buf)) g_weight = atoi(buf);
    if (GetEnvironmentVariableA("DLJP_EMK", buf, sizeof buf)) g_emk = (float)atof(buf);
#endif
    patch_jmp(A_DRAWSTRING, (void *)my_drawstring);
    patch_jmp(A_STRWIDTH, (void *)my_strwidth);
    patch_jmp(A_CHARSFIT, (void *)my_charsfit);
    patch_jmp(A_WRAPBREAK, (void *)my_wrapbreak);
    /* tooltip lines: first byte <= 0x18 is a font code, read as signed char -> EUC lead bytes
       were taken for codes. movsx eax,byte [ecx+0xc2cc40] -> movzx */
    { static const uint8_t sig[7] = { 0x0f, 0xbe, 0x81, 0x40, 0xcc, 0xc2, 0x00 }, mz = 0xb6;
      if (!memcmp((void *)0x4f8a11, sig, 7)) patch_bytes(0x4f8a12, &mz, 1); }
    patch_jmp(0x4a20f0, (void *)my_upper);
    patch_jmp(0x4a2140, (void *)my_lower);
    patch_jmp(0x4a2190, (void *)my_capwords);
    patch_jmp(0x4a2210, (void *)my_title);
}
