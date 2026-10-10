// The decompiled code's Win32 calls in the hook build: non-blocking ones run the emulator's own
// implementation through the DLL's import table (so they are the very same code), blocking ones use
// the emulator's wait / critical-section logic and park the coroutine instead of stopping the CPU.
#include "hooks.h"
#include "crt_vc.h"
#include "gptr.h"

static uint32_t api(uint32_t iat, int nargs, const uint32_t *args) {
    uint32_t thunk = *DLLVAR(uint32_t, iat);
    return emu_call(decomp_emu, thunk, nargs, args);
}
static uint32_t g(const void *p) { return p ? (uint32_t)((const uint8_t *)p - decomp_guest_mem) : 0; }
static void must_block(const char *what) {
    if (!decomp_in_coro()) x86_fault(emu_cpu(decomp_emu), "decomp: %s would block outside a HOOKY hook", what);
}

void vc_EnterCriticalSection(void *cs) {
    for (;;) {
        if (emu_cs_try_enter(decomp_emu, g(cs))) return;
        must_block("EnterCriticalSection");
        Thread *t = decomp_emu->cur;
        t->state = T_BLOCKED;
        t->gen = decomp_emu->gen;
        t->wake_at = 0;
        decomp_coro_block();
    }
}
void vc_LeaveCriticalSection(void *cs) { uint32_t a[1] = { g(cs) }; api(0x6369e014, 1, a); }
int32_t vc_SetEvent(uint32_t h) { uint32_t a[1] = { h }; return (int32_t)api(0x6369e038, 1, a); }
int32_t vc_ResetEvent(uint32_t h) { uint32_t a[1] = { h }; return (int32_t)api(0x6369e088, 1, a); }
uint32_t vc_WaitForMultipleObjects(uint32_t n, const uint32_t *hs, int all, uint32_t ms) {
    for (;;) {
        uint32_t r;
        if (emu_wait_poll(decomp_emu, n, hs, all, ms, &r)) return r;
        must_block("WaitFor...Object(s)");
        emu_wait_block_state(decomp_emu, ms);
        decomp_coro_block();
    }
}
uint32_t vc_WaitForSingleObject(uint32_t h, uint32_t ms) { return vc_WaitForMultipleObjects(1, &h, 0, ms); }
int32_t vc_PostMessageA(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    uint32_t a[4] = { hwnd, msg, wp, lp };
    return (int32_t)api(0x6369e1f8, 4, a);
}
int32_t vc_SetThreadPriority(uint32_t h, int32_t prio) { uint32_t a[2] = { h, (uint32_t)prio }; return (int32_t)api(0x6369e03c, 2, a); }
void vc_DebugBreak(void) { api(0x6369e084, 0, NULL); }
uint32_t vc_com_release(void *p) {
    uint32_t obj = g(p);
    uint32_t vt = *(uint32_t *)(void *)(decomp_guest_mem + obj);
    uint32_t fn = *(uint32_t *)(void *)(decomp_guest_mem + vt + 8);
    return emu_call(decomp_emu, fn, 1, &obj);
}

int32_t vc_CoCreateInstance(uint32_t clsid, uint32_t ctx, uint32_t iid, void **out) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - 16u) & ~15u;
    uint32_t slot = c->s.r[ESP];
    *(uint32_t *)(void *)(decomp_guest_mem + slot) = 0;
    uint32_t a[5] = { clsid + decomp_load_delta, 0, ctx, iid + decomp_load_delta, slot };
    int32_t r = (int32_t)api(0x6369e224, 5, a);
    uint32_t o = *(uint32_t *)(void *)(decomp_guest_mem + slot);
    c->s.r[ESP] = sp;
    *out = o ? decomp_guest_mem + o : NULL;
    return r;
}
int32_t vc_com_call(void *p, int slot, int nargs, const uint32_t *args) {
    uint32_t obj = g(p), a[16];
    uint32_t vt = *(uint32_t *)(void *)(decomp_guest_mem + obj);
    uint32_t fn = *(uint32_t *)(void *)(decomp_guest_mem + vt + 4u * (uint32_t)slot);
    a[0] = obj;
    for (int i = 0; i < nargs && i < 15; i++) a[i + 1] = args[i];
    return (int32_t)emu_call(decomp_emu, fn, nargs + 1, a);
}
int32_t vc_CoInitialize(void) { uint32_t a[1] = { 0 }; return (int32_t)api(0x6369e21c, 1, a); }
void vc_CoUninitialize(void) { api(0x6369e220, 0, NULL); }
uint32_t vc_CreateEventA(int32_t manual, int32_t initial) { uint32_t a[4] = { 0, (uint32_t)manual, (uint32_t)initial, 0 }; return api(0x6369e07c, 4, a); }
uint32_t vc_GetCurrentThread(void) { return api(0x6369e078, 0, NULL); }
// (buffers in C locals go through the guest stack)
uint32_t vc_GetModuleFileNameA(uint32_t hmod, char *buf, uint32_t n) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - n - 16u) & ~15u;
    uint32_t gb = c->s.r[ESP];
    memcpy(decomp_guest_mem + gb, buf, n);
    uint32_t a[3] = { hmod, gb, n };
    uint32_t r = api(0x6369e06c, 3, a);
    memcpy(buf, decomp_guest_mem + gb, n);
    c->s.r[ESP] = sp;
    return r;
}
int32_t vc_GetUserNameA(char *buf, uint32_t *n) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP], cap = *n;
    c->s.r[ESP] = (sp - cap - 32u) & ~15u;
    uint32_t gn = c->s.r[ESP], gb = gn + 16;
    memcpy(decomp_guest_mem + gn, n, 4);
    memcpy(decomp_guest_mem + gb, buf, cap);
    uint32_t a[2] = { gb, gn };
    int32_t r = (int32_t)api(0x6369e000, 2, a);
    memcpy(n, decomp_guest_mem + gn, 4);
    memcpy(buf, decomp_guest_mem + gb, cap);
    c->s.r[ESP] = sp;
    return r;
}
uint32_t vc_CreateFileA(const char *path, uint32_t access, uint32_t share, uint32_t disposition, uint32_t flags) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    size_t l = strlen(path) + 1;
    c->s.r[ESP] = (sp - (uint32_t)l - 16u) & ~15u;
    uint32_t gp_ = c->s.r[ESP];
    memcpy(decomp_guest_mem + gp_, path, l);
    uint32_t a[7] = { gp_, access, share, 0, disposition, flags, 0 };
    uint32_t r = api(0x6369e04c, 7, a);
    c->s.r[ESP] = sp;
    return r;
}
uint32_t vc_GetFileSize(uint32_t h) { uint32_t a[2] = { h, 0 }; return api(0x6369e020, 2, a); }
int32_t vc_ReadFile(uint32_t h, void *buf, uint32_t n, uint32_t *got) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - 16u) & ~15u;
    uint32_t gg = c->s.r[ESP];
    uint32_t a[5] = { h, g(buf), n, gg, 0 };
    int32_t r = (int32_t)api(0x6369e01c, 5, a);
    memcpy(got, decomp_guest_mem + gg, 4);
    c->s.r[ESP] = sp;
    return r;
}
int32_t vc_CloseHandle(uint32_t h) { return (int32_t)api(0x6369e018, 1, &h); }
uint32_t vc_GetLastError(void) { return api(0x6369e028, 0, NULL); }
void vc_InitializeCriticalSection(void *cs) { uint32_t a[1] = { g(cs) }; api(0x6369e054, 1, a); }

// storage through the emulator's COM objects (their methods are host functions behind guest vtables)
int32_t vc_stg_open_stream(void *stg, const uint16_t *name, void **stm) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    // (a name in a C local is copied onto the guest stack)
    int host = (const uint8_t *)name < decomp_guest_mem || (const uint8_t *)name >= decomp_guest_mem + decomp_emu->mem_size;
    uint32_t nb = 0;
    if (host) while (name[nb / 2]) nb += 2;
    c->s.r[ESP] = (sp - 16u - (host ? nb + 2 : 0)) & ~15u;
    uint32_t slot = c->s.r[ESP];
    *(uint32_t *)(void *)(decomp_guest_mem + slot) = 0;
    uint32_t gname = host ? slot + 16 : g(name);
    if (host) memcpy(decomp_guest_mem + gname, name, nb + 2);
    uint32_t a[5] = { gname, 0, 0x10, 0, slot };
    int32_t r = vc_com_call(stg, 4, 5, a);
    uint32_t o = *(uint32_t *)(void *)(decomp_guest_mem + slot);
    c->s.r[ESP] = sp;
    *stm = o ? decomp_guest_mem + o : NULL;
    return r;
}
int32_t vc_stm_read(void *stm, void *buf, uint32_t n, uint32_t *got) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    // a C local is not guest memory: read into the guest stack and copy
    int host = (uint8_t *)buf < decomp_guest_mem || (uint8_t *)buf >= decomp_guest_mem + decomp_emu->mem_size;
    c->s.r[ESP] = (sp - 16u - (host ? n : 0)) & ~15u;
    uint32_t slot = c->s.r[ESP], dst = host ? slot + 16 : g(buf);
    uint32_t a[3] = { dst, n, got ? slot : 0 };
    int32_t r = vc_com_call(stm, 3, 3, a);
    if (got) *got = *(uint32_t *)(void *)(decomp_guest_mem + slot);
    if (host) memcpy(buf, decomp_guest_mem + dst, n);
    c->s.r[ESP] = sp;
    return r;
}
int32_t vc_stm_size(void *stm, uint32_t *size) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - 0x60u) & ~15u;
    uint32_t st = c->s.r[ESP];
    uint32_t a[2] = { st, 1 };
    int32_t r = vc_com_call(stm, 12, 2, a);
    *size = *(uint32_t *)(void *)(decomp_guest_mem + st + 8);
    c->s.r[ESP] = sp;
    return r;
}
int32_t vc_stm_seek(void *stm, int32_t move, uint32_t origin, uint32_t *pos) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - 16u) & ~15u;
    uint32_t slot = c->s.r[ESP];
    uint32_t a[4] = { (uint32_t)move, move < 0 ? 0xffffffffu : 0, origin, pos ? slot : 0 };
    int32_t r = vc_com_call(stm, 5, 4, a);
    if (pos) *pos = *(uint32_t *)(void *)(decomp_guest_mem + slot);
    c->s.r[ESP] = sp;
    return r;
}
int32_t vc_StgOpenStorage(const uint16_t *path, uint32_t mode, void **stg) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - 16u) & ~15u;
    uint32_t slot = c->s.r[ESP];
    *(uint32_t *)(void *)(decomp_guest_mem + slot) = 0;
    uint32_t a[6] = { g(path), 0, mode, 0, 0, slot };
    int32_t r = (int32_t)api(0x6369e228, 6, a);
    uint32_t o = *(uint32_t *)(void *)(decomp_guest_mem + slot);
    c->s.r[ESP] = sp;
    *stg = o ? decomp_guest_mem + o : NULL;
    return r;
}
// (the pattern and the find data may be C locals: they go through the guest stack)
uint32_t vc_FindFirstFileA(const char *pattern, void *data) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    size_t n = strlen(pattern) + 1;
    c->s.r[ESP] = (sp - 0x140u - (uint32_t)n - 16u) & ~15u;
    uint32_t gd = c->s.r[ESP], gpat = gd + 0x140u;
    memcpy(decomp_guest_mem + gd, data, 0x140);
    memcpy(decomp_guest_mem + gpat, pattern, n);
    uint32_t a[2] = { gpat, gd };
    uint32_t r = api(0x6369e068, 2, a);
    memcpy(data, decomp_guest_mem + gd, 0x140);
    c->s.r[ESP] = sp;
    return r;
}
int32_t vc_FindNextFileA(uint32_t h, void *data) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - 0x150u) & ~15u;
    uint32_t gd = c->s.r[ESP];
    memcpy(decomp_guest_mem + gd, data, 0x140);
    uint32_t a[2] = { h, gd };
    int32_t r = (int32_t)api(0x6369e064, 2, a);
    memcpy(data, decomp_guest_mem + gd, 0x140);
    c->s.r[ESP] = sp;
    return r;
}
int32_t vc_FindClose(uint32_t h) { return (int32_t)api(0x6369e060, 1, &h); }
void *vc_stg_open_file(const char *path) { (void)path; return NULL; }
void *vc_stg_open_mem(const void *data, size_t len) { (void)data; (void)len; return NULL; }
