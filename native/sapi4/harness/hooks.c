#define _XOPEN_SOURCE 600
#include "hooks.h"
#include <ucontext.h>
#pragma clang diagnostic ignored "-Wdeprecated-declarations"   // ucontext: fine for a dev-only harness
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t decomp_load_delta;
_Thread_local uint8_t *decomp_guest_mem;
_Thread_local Emu *decomp_emu;

void *vc_malloc(size_t n) {
    uint32_t p = emu_malloc(decomp_emu, (uint32_t)n);
    return p ? decomp_guest_mem + p : NULL;
}
void *vc_realloc(void *p, size_t n) {
    uint32_t r = emu_realloc(decomp_emu, p ? (uint32_t)((uint8_t *)p - decomp_guest_mem) : 0, (uint32_t)n);
    return r ? decomp_guest_mem + r : NULL;
}
void *vc_calloc(size_t n, size_t size) {
    // as the emulator's calloc: a 32-bit product over 2 GB fails, otherwise malloc + zero
    uint64_t t = (uint64_t)(uint32_t)n * (uint32_t)size;
    if (t > 0x7FFFFFFF) return NULL;
    uint32_t p = emu_malloc(decomp_emu, (uint32_t)t);
    if (!p) return NULL;
    memset(decomp_guest_mem + p, 0, (size_t)t);
    return decomp_guest_mem + p;
}
void *vc_stack_push(uint32_t size, uint32_t below) {
    (void)below;
    X86 *c = emu_cpu(decomp_emu);
    c->s.r[ESP] -= size;
    return decomp_guest_mem + c->s.r[ESP];
}
void vc_stack_pop(void *frame, uint32_t size, uint32_t below) {
    (void)frame; (void)below;
    emu_cpu(decomp_emu)->s.r[ESP] += size;
}
void vc_free(void *p) {
    if (p) emu_free(decomp_emu, (uint32_t)((uint8_t *)p - decomp_guest_mem));
}

// ---------------------------------------------------------------- suspendable hooks
// A hook marked HOOKY (it may Sleep or block, like the original) runs its C on a coroutine stack, one
// per guest thread. When the C yields or blocks, the coroutine is parked and the host call returns
// with "retry" set: the guest thread stays on the trap instruction, other guest threads run, and when
// this one is scheduled again the trap re-executes and the coroutine resumes where it stopped - so a
// yield or a wait happens at the same point of the guest's execution as in the original.
enum { CO_IDLE, CO_RUNNING, CO_SUSPENDED };
enum { REQ_NONE, REQ_YIELD, REQ_BLOCK };
typedef struct Coro {
    ucontext_t ctx, back;
    int state, req;
    EmuHostFn fn;
    Emu *e;
    X86 *c;
    char *stack;
} Coro;
#define CORO_STACK (512 * 1024)
static _Thread_local Coro coros[EMU_MAX_THREADS + 1];
static _Thread_local Coro *cur_coro;

static void coro_entry(void) {
    Coro *co = cur_coro;
    co->fn(co->e, co->c);
    co->state = CO_IDLE;
    co->req = REQ_NONE;
    swapcontext(&co->ctx, &co->back);
}

void decomp_coro_run(Emu *e, X86 *c, EmuHostFn fn) {
    int slot = e->cur ? e->cur->id : 0;
    if (slot < 0 || slot > EMU_MAX_THREADS) x86_fault(c, "decomp: bad thread id");
    Coro *co = &coros[slot];
    if (co->state == CO_SUSPENDED) {
        if (co->fn != fn) x86_fault(c, "decomp: thread re-entered a different hook while one is suspended");
    } else {
        if (!co->stack) co->stack = malloc(CORO_STACK);
        co->fn = fn;
        co->e = e;
        co->c = c;
        getcontext(&co->ctx);
        co->ctx.uc_stack.ss_sp = co->stack;
        co->ctx.uc_stack.ss_size = CORO_STACK;
        co->ctx.uc_link = NULL;
        makecontext(&co->ctx, coro_entry, 0);
    }
    co->state = CO_RUNNING;
    co->req = REQ_NONE;
    Coro *prev = cur_coro;
    cur_coro = co;
    swapcontext(&co->back, &co->ctx);
    cur_coro = prev;
    if (co->state == CO_IDLE) return;              // finished: host_dispatch returns to the guest
    co->state = CO_SUSPENDED;                       // parked: stay on the trap, let others run
    if (co->req == REQ_BLOCK) emu_block(e);
    else {
        e->retry = 1;
        e->stop_reason = STOP_YIELD;
        c->stop = 1;
    }
}

int decomp_in_coro(void) { return cur_coro != NULL; }
void decomp_coro_block(void) {
    Coro *co = cur_coro;
    co->req = REQ_BLOCK;
    swapcontext(&co->ctx, &co->back);
}
static void coro_suspend(int req) {
    Coro *co = cur_coro;
    co->req = req;
    swapcontext(&co->ctx, &co->back);
}

// KERNEL32 Sleep as the emulator does it: advance the virtual clock and yield to the other threads.
// Outside a coroutine (a plain HOOK) the yield can only happen once the hook returns.
void vc_sleep(uint32_t ms) {
    decomp_emu->vtime += ms;
    if (cur_coro) {
        coro_suspend(REQ_YIELD);
        return;
    }
    decomp_emu->stop_reason = STOP_YIELD;
    emu_cpu(decomp_emu)->stop = 1;
}
void decomp_warn(const char *msg) {
#ifndef CV_NO_DEBUG_ENV
    fprintf(stderr, "decomp WARNING: %s\n", msg);
#else
    (void)msg;
#endif
}

// MSVCRT rand() on the calling guest thread's own state (exactly what the emulator's CRT does)
int vc_rand(void) {
    Thread *t = decomp_emu->cur;
    t->rand_seed = t->rand_seed * 214013u + 2531011u;
    return (int)((t->rand_seed >> 16) & 0x7FFF);
}

static int selected(const char *list, const char *name) {
    if (!list || !*list || !strcmp(list, "all")) return 1;
    if (!strcmp(list, "none")) return 0;
    int excl_mode = list[0] == '-';
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", list);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        if (*tok == '-') tok++;
        if (!strcmp(tok, name)) return !excl_mode;
    }
    return excl_mode;
}

int decomp_install_hooks(Emu *e, uint32_t load_base, uint32_t pref_base) {
    const char *list = NULL;
#ifndef CV_NO_DEBUG_ENV
    list = getenv("SAPI4_HOOKS");
#endif
    decomp_load_delta = load_base - pref_base;
    decomp_guest_mem = emu_cpu(e)->mem;
    decomp_emu = e;
    int n = 0;
    for (int i = 0; i < decomp_nhooks; i++) {
        const DecompHook *h = &decomp_hooks[i];
        if (!selected(list, h->name)) continue;
        uint32_t a = h->addr - pref_base + load_base;
        uint32_t thunk = emu_thunk(e, h->name, h->fn, h->argbytes);
        uint32_t idx = (thunk - EMU_THUNK_BASE) / 4;
        uint8_t *p = emu_ptr(e, a, 3);
        p[0] = 0xF1; p[1] = (uint8_t)idx; p[2] = (uint8_t)(idx >> 8);
        n++;
    }
#ifndef CV_NO_DEBUG_ENV
    if (getenv("SAPI4_VERBOSE")) fprintf(stderr, "decomp: %d of %d hooks installed\n", n, decomp_nhooks);
#endif
    return n;
}

void decomp_report(Emu *e) {
#ifndef CV_NO_DEBUG_ENV
    if (!getenv("SAPI4_HOOK_STATS")) return;
    fprintf(stderr, "guest heap: next %08x live %llu\n", e->heap_next, (unsigned long long)e->heap_live);
    for (int i = 0; i < e->nhosts; i++)
        for (int k = 0; k < decomp_nhooks; k++)
            if (e->hosts[i].fn == decomp_hooks[k].fn)
                fprintf(stderr, "hook %08x %-28s %llu calls\n", decomp_hooks[k].addr, decomp_hooks[k].name,
                        (unsigned long long)e->hosts[i].calls);
#else
    (void)e;
#endif
}
