/* Dungeon Lords Japanese patch: entry point, patch helpers, D3D9 test hooks */
#include "dljp.h"
#include <stdio.h>
#include <stdarg.h>
#ifdef DLJP_TEST
#include <d3d9.h>
#endif

void proxy_init(void);
HRESULT WINAPI real_DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID *o, LPUNKNOWN u);

HMODULE g_self;
#ifdef DLJP_TEST
/* 試験版だけ: DLJP_TEST=1 で試験の操作口、DLJP_LOG=<ファイル> で記録 */
int g_test;
static FILE *g_log;
static CRITICAL_SECTION g_logcs;

void dlog(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    EnterCriticalSection(&g_logcs);
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logcs);
}
#endif

void patch_bytes(uint32_t at, const void *b, int n)
{
    DWORD old;
    VirtualProtect((void *)at, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)at, b, n);
    VirtualProtect((void *)at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, n);
}

void patch_jmp(uint32_t at, void *to)
{
    uint8_t b[5];
    b[0] = 0xe9;
    *(int32_t *)(b + 1) = (int32_t)((uint32_t)to - (at + 5));
    patch_bytes(at, b, 5);
}

void *iat_hook(const char *dll, const char *func, void *repl)
{
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress);
    for (; imp->Name; imp++) {
        IMAGE_THUNK_DATA *names, *addrs;
        if (_stricmp((char *)(base + imp->Name), dll)) continue;
        names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        addrs = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++) {
            IMAGE_IMPORT_BY_NAME *ibn;
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (!strcmp((char *)ibn->Name, func)) {
                void *old = (void *)addrs->u1.Function;
                DWORD prot;
                VirtualProtect(&addrs->u1.Function, 4, PAGE_READWRITE, &prot);
                addrs->u1.Function = (ULONG_PTR)repl;
                VirtualProtect(&addrs->u1.Function, 4, prot, &prot);
                return old;
            }
        }
    }
    return NULL;
}

/* ---------- CreateFileA: translated data files ---------- */
static int is_save_path(const char *p);
static void a2w(const char *a, WCHAR *w, int n);
static HANDLE (WINAPI *o_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

static HANDLE WINAPI h_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    HANDLE h = text_open_hook(name, acc, share, sa, disp, flags, tmpl);
    if (h) return h;
    if (is_save_path(name)) {
        WCHAR w[MAX_PATH];
        a2w(name, w, MAX_PATH);
        h = CreateFileW(w, acc, share, sa, disp, flags, tmpl);
        if (g_test) dlog("CreateFileW %s -> %p", name, h);
        return h;
    }
    return o_CreateFileA(name, acc, share, sa, disp, flags, tmpl);
}

/* ---------- save files: Japanese (EUC-JP) names -> real Unicode file names ---------- */
static int is_save_path(const char *p)
{
    if (!p) return 0;
    return !_strnicmp(p, "save/", 5) || !_strnicmp(p, "save\\", 5);
}

static void a2w(const char *a, WCHAR *w, int n)
{
    int k = 0;
    const uint8_t *s = (const uint8_t *)a;
    while (*s && k < n - 1) {
        if (jp_lead(s)) {
            if (!MultiByteToWideChar(20932, 0, (const char *)s, 2, w + k, 1)) w[k] = L'_';
            k++; s += 2;
        } else if (*s < 0x80) { w[k++] = *s++; }
        else { MultiByteToWideChar(CP_ACP, 0, (const char *)s, 1, w + k, 1); k++; s++; }
    }
    w[k] = 0;
}

static void w2a(const WCHAR *w, char *a, int n)
{
    int k = 0;
    for (; *w && k < n - 3; w++) {
        char b[4];
        BOOL bad = FALSE;
        int m;
        if (*w < 0x80) { a[k++] = (char)*w; continue; }
        m = WideCharToMultiByte(20932, 0, w, 1, b, sizeof b, NULL, &bad);
        if (m == 2 && !bad && (uint8_t)b[0] >= 0xa1 && (uint8_t)b[1] >= 0xa1) { a[k++] = b[0]; a[k++] = b[1]; continue; }
        m = WideCharToMultiByte(CP_ACP, 0, w, 1, b, sizeof b, NULL, NULL);
        if (m > 0 && k + m < n) { memcpy(a + k, b, m); k += m; }
    }
    a[k] = 0;
}

static void fd_w2a(const WIN32_FIND_DATAW *w, WIN32_FIND_DATAA *a)
{
    a->dwFileAttributes = w->dwFileAttributes;
    a->ftCreationTime = w->ftCreationTime;
    a->ftLastAccessTime = w->ftLastAccessTime;
    a->ftLastWriteTime = w->ftLastWriteTime;
    a->nFileSizeHigh = w->nFileSizeHigh;
    a->nFileSizeLow = w->nFileSizeLow;
    a->dwReserved0 = w->dwReserved0;
    a->dwReserved1 = w->dwReserved1;
    w2a(w->cFileName, a->cFileName, MAX_PATH);
    w2a(w->cAlternateFileName, a->cAlternateFileName, 14);
}

static HANDLE g_finds[16];
static HANDLE WINAPI h_FindFirstFileA(LPCSTR pat, LPWIN32_FIND_DATAA fd)
{
    WCHAR w[MAX_PATH];
    WIN32_FIND_DATAW wd;
    HANDLE h;
    int i;
    if (!is_save_path(pat)) { h = FindFirstFileA(pat, fd); if (g_test) dlog("FindFirstA %s -> %s", pat, h != INVALID_HANDLE_VALUE ? fd->cFileName : "-"); return h; }
    a2w(pat, w, MAX_PATH);
    h = FindFirstFileW(w, &wd);
    if (g_test) dlog("FindFirstW %s -> %d", pat, h != INVALID_HANDLE_VALUE);
    if (h == INVALID_HANDLE_VALUE) return h;
    fd_w2a(&wd, fd);
    for (i = 0; i < 16; i++) if (!g_finds[i]) { g_finds[i] = h; break; }
    return h;
}
static BOOL WINAPI h_FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd)
{
    int i;
    for (i = 0; i < 16; i++) if (g_finds[i] == h) {
        WIN32_FIND_DATAW wd;
        if (!FindNextFileW(h, &wd)) return FALSE;
        fd_w2a(&wd, fd);
        if (g_test) dlog("  next %s", fd->cFileName);
        return TRUE;
    }
    return FindNextFileA(h, fd);
}
static BOOL WINAPI h_FindClose(HANDLE h)
{
    int i;
    for (i = 0; i < 16; i++) if (g_finds[i] == h) g_finds[i] = NULL;
    return FindClose(h);
}
static BOOL WINAPI h_DeleteFileA(LPCSTR p)
{
    WCHAR w[MAX_PATH];
    if (!is_save_path(p)) return DeleteFileA(p);
    a2w(p, w, MAX_PATH);
    return DeleteFileW(w);
}
static BOOL WINAPI h_MoveFileA(LPCSTR a, LPCSTR b)
{
    WCHAR wa[MAX_PATH], wb[MAX_PATH];
    if (!is_save_path(a) && !is_save_path(b)) return MoveFileA(a, b);
    a2w(a, wa, MAX_PATH); a2w(b, wb, MAX_PATH);
    return MoveFileW(wa, wb);
}
static DWORD WINAPI h_GetFileAttributesA(LPCSTR p)
{
    WCHAR w[MAX_PATH];
    if (!is_save_path(p)) return GetFileAttributesA(p);
    a2w(p, w, MAX_PATH);
    return GetFileAttributesW(w);
}

#ifdef DLJP_TEST
/* ---------- test mode: windowed D3D, no cursor clipping ---------- */
static HRESULT (STDMETHODCALLTYPE *o_CreateDevice)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
static HRESULT (STDMETHODCALLTYPE *o_Reset)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
static HRESULT (STDMETHODCALLTYPE *o_Present)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);

static void force_windowed(D3DPRESENT_PARAMETERS *pp)
{
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
}

static HRESULT STDMETHODCALLTYPE h_Present(IDirect3DDevice9 *d, const RECT *a, const RECT *b, HWND w, const RGNDATA *r)
{
    if (g_test) testio_present(d);
    return o_Present(d, a, b, w, r);
}

static HRESULT STDMETHODCALLTYPE h_Reset(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *pp)
{
    force_windowed(pp);
    return o_Reset(d, pp);
}

static void vt_patch(void **vt, int idx, void *repl, void **orig)
{
    DWORD prot;
    if (vt[idx] == repl) return;
    VirtualProtect(&vt[idx], 4, PAGE_EXECUTE_READWRITE, &prot);
    *orig = vt[idx];
    vt[idx] = repl;
    VirtualProtect(&vt[idx], 4, prot, &prot);
}

static void **g_devvt;
static DWORD WINAPI watch_thread(LPVOID p)
{
    (void)p;
    for (;;) {
        Sleep(1000);
        if (g_devvt && g_devvt[17] != (void *)h_Present) {
            dlog("Present slot changed to %p, re-patching", g_devvt[17]);
            vt_patch(g_devvt, 17, (void *)h_Present, (void **)&o_Present);
        }
    }
}

static HRESULT STDMETHODCALLTYPE h_CreateDevice(IDirect3D9 *d3d, UINT ad, D3DDEVTYPE t, HWND w, DWORD fl, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    HRESULT hr;
    force_windowed(pp);
    dlog("CreateDevice %dx%d fmt %d", pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat);
    hr = o_CreateDevice(d3d, ad, t, w, fl, pp, out);
    if (SUCCEEDED(hr)) {
        void **vt = *(void ***)*out;
        vt_patch(vt, 16, (void *)h_Reset, (void **)&o_Reset);
        vt_patch(vt, 17, (void *)h_Present, (void **)&o_Present);
        dlog("device vt %p present %p", vt, o_Present);
        if (!g_devvt) { g_devvt = vt; CreateThread(NULL, 0, watch_thread, NULL, 0, NULL); }
    } else dlog("CreateDevice failed %08x", hr);
    return hr;
}

static IDirect3D9 *(WINAPI *o_Direct3DCreate9)(UINT);
static IDirect3D9 *WINAPI h_Direct3DCreate9(UINT v)
{
    IDirect3D9 *d = o_Direct3DCreate9(v);
    if (d) vt_patch(*(void ***)d, 16, (void *)h_CreateDevice, (void **)&o_CreateDevice);
    return d;
}

static BOOL WINAPI h_ClipCursor(const RECT *r) { (void)r; return TRUE; }
static HWND WINAPI h_SetCapture(HWND w) { (void)w; return NULL; }
static BOOL WINAPI h_ReleaseCapture(void) { return TRUE; }
#endif

HRESULT WINAPI p_DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID *o, LPUNKNOWN u)
{
    HRESULT hr = real_DirectInput8Create(h, v, r, o, u);
#ifdef DLJP_TEST
    if (g_test && SUCCEEDED(hr)) testio_wrap_dinput(*o);
#endif
    return hr;
}

static void init(HMODULE self)
{
#ifdef DLJP_TEST
    char buf[MAX_PATH];
    InitializeCriticalSection(&g_logcs);
    if (GetEnvironmentVariableA("DLJP_TEST", buf, sizeof buf) && buf[0] == '1') g_test = 1;
    if (GetEnvironmentVariableA("DLJP_LOG", buf, sizeof buf)) g_log = fopen(buf, "w");
#endif
    /* only patch the game we know */
    static const uint8_t sig_draw[5] = { 0x51, 0xd9, 0x44, 0x24, 0x14 }, sig_w[4] = { 0x56, 0x8b, 0x34, 0x8d };
    if (memcmp((void *)A_DRAWSTRING, sig_draw, 5) || memcmp((void *)A_STRWIDTH, sig_w, 4)) {
        dlog("unknown exe, patch disabled");
        return;
    }
    dlog("dljp start test=%d", g_test);
    o_CreateFileA = iat_hook("KERNEL32.dll", "CreateFileA", (void *)h_CreateFileA);
    iat_hook("KERNEL32.dll", "FindFirstFileA", (void *)h_FindFirstFileA);
    iat_hook("KERNEL32.dll", "FindNextFileA", (void *)h_FindNextFileA);
    iat_hook("KERNEL32.dll", "FindClose", (void *)h_FindClose);
    iat_hook("KERNEL32.dll", "DeleteFileA", (void *)h_DeleteFileA);
    iat_hook("KERNEL32.dll", "MoveFileA", (void *)h_MoveFileA);
    iat_hook("KERNEL32.dll", "GetFileAttributesA", (void *)h_GetFileAttributesA);
#ifdef DLJP_TEST
    /* 窓での表示（DLJP_WINDOWED=1）も試験版だけ */
    if (g_test || (GetEnvironmentVariableA("DLJP_WINDOWED", buf, sizeof buf) && buf[0] == '1'))
        o_Direct3DCreate9 = iat_hook("d3d9.dll", "Direct3DCreate9", (void *)h_Direct3DCreate9);
    if (g_test) {
        iat_hook("USER32.dll", "ClipCursor", (void *)h_ClipCursor);
        iat_hook("USER32.dll", "SetCapture", (void *)h_SetCapture);
        iat_hook("USER32.dll", "ReleaseCapture", (void *)h_ReleaseCapture);
        testio_init();
    }
#endif
    text_init(self);
    exe_strings_init(self);
    render_init();
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_self = h;
        proxy_init();
        init(h);
    }
    return TRUE;
}
