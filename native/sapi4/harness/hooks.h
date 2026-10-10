// Differential harness: replace functions of the ORIGINAL msttssyn.dll, running in the emulator, with
// decompiled C. Each hook patches the function's entry with the host-call trap (F1 lo hi); the adapter
// reads the guest arguments, calls the portable C in src/, writes the result back (EAX / ST0 / guest
// memory) and returns the way the original did (ret n). The golden corpus must stay bit-identical.
#pragma once
#include "emu.h"
#include "emu_internal.h"

// Addresses are the DLL's own (preferred base 0x63670000), as printed by the tools and objdump.
#define MSTTS_PREF_BASE 0x63670000u

typedef struct DecompHook {
    uint32_t addr;          // function entry, preferred-base address
    const char *name;       // C name (for SAPI4_HOOKS selection)
    EmuHostFn fn;           // adapter
    int argbytes;           // bytes popped on return (stdcall / thiscall "ret n"); 0 = cdecl
} DecompHook;

extern const DecompHook decomp_hooks[];
extern const int decomp_nhooks;

// Installs the selected hooks into a freshly loaded image. SAPI4_HOOKS (Mac builds): unset or "all" =
// every hook; "none"; "a,b,c" = only those; "-a,-b" = all except those. Returns the number installed.
int decomp_install_hooks(Emu *e, uint32_t load_base, uint32_t pref_base);

// guest pointer -> host pointer (bounds-checked; faults the guest on a bad pointer)
#define GPH(T, a, n) ((T *)(void *)x86_ptr(c, (a), (uint32_t)(n)))
// guest address of a DLL global, relocated
extern uint32_t decomp_load_delta;
extern _Thread_local Emu *decomp_emu;   // load base - preferred base
#define GADDR(pref) ((uint32_t)(pref) + decomp_load_delta)

// SAPI4_HOOK_STATS=1 (Mac builds): print how often each installed hook ran, to stderr
void decomp_report(Emu *e);

// runs a hook's adapter on this guest thread's coroutine (HOOKY hooks, see hooks.c)
void decomp_coro_run(Emu *e, X86 *c, EmuHostFn fn);
int decomp_in_coro(void);
void decomp_coro_block(void);   // park this coroutine; the guest thread's blocked state must be set first
