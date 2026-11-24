/* Dungeon Lords Steam Edition (DLSteamEdition.exe) Japanese patch - shared declarations */
#pragma once
#include <windows.h>
#include <stdint.h>

/* ---- game (DLSteamEdition.exe, image base 0x400000, no relocation) ---- */
typedef struct { int ch, x, y, w, h, yoff; } GGlyph;          /* 24 bytes */
typedef struct {
    int texw, texh, count, spacing, f10, spacew, filt;
    int map[256];
    GGlyph *glyphs;                                            /* +0x41c */
    void **tex;                                                /* +0x420  ([0] = IDirect3DTexture9*) */
} GFont;

#define G_FONTS      ((GFont **)0x7e4338)
#define G_VERTS      ((void *)0x7e4410)
#define G_SCRW       (*(int *)0x7522d0)
#define G_LANG       (*(char *)0x755728)

#define A_DRAWSTRING 0x461580   /* fastcall(font id, str, x, y, color, float scale)  ret 0x10 */
#define A_STRWIDTH   0x461680   /* fastcall(font id, str) -> width */
#define A_CHARSFIT   0x4616d0   /* fastcall(font id, str, maxw) -> chars */
#define A_WRAPBREAK  0x461750   /* fastcall(font id, str, maxw) -> line length */
#define A_GLYPH      0x4612a0   /* fastcall(GFont*, idx, x, y, color, vi, scale) -> float advance */
#define A_FLUSH      0x4156f0   /* fastcall(texobj, filter, tri count, verts) */
#define A_MKTEX      0x418d00   /* fastcall(&{w,h,pixels}) -> texobj */

typedef float (__fastcall *GlyphFn)(GFont *f, int idx, float x, float y, int color, int vi, float scale);
typedef void  (__fastcall *FlushFn)(void **tex, int filt, int tris, void *verts);
typedef void **(__fastcall *MkTexFn)(int *desc);

/* ---- dljp.c ---- */
#ifdef DLJP_TEST
extern int g_test;                      /* DLJP_TEST=1 */
void dlog(const char *fmt, ...);
#else
/* 配布版には試験の仕組みも記録も入れない */
#define g_test 0
#define dlog(...) ((void)0)
#endif
void patch_jmp(uint32_t at, void *to);
void patch_bytes(uint32_t at, const void *b, int n);
void *iat_hook(const char *dll, const char *func, void *repl);

#ifdef DLJP_TEST
/* ---- testio.c（試験版だけ） ---- */
void testio_init(void);
void testio_present(void *dev);         /* IDirect3DDevice9* */
HRESULT testio_wrap_dinput(void *di);   /* IDirectInput8A* */
#endif

/* ---- render.c ---- */
void render_init(void);
int  jp_lead(const uint8_t *s);         /* 1 if s starts an EUC-JP double byte char */

/* ---- text.c ---- */
void text_init(HMODULE self);
void exe_strings_init(HMODULE self);
uint64_t fnv64(const uint8_t *s, int n);
HANDLE text_open_hook(const char *name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl);
