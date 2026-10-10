// The hook table: every decompiled function, by address.
// HOOK: runs directly in the host call. HOOKY: may yield or block like the original (Sleep, waits),
// so it runs on a coroutine that can be suspended mid-function (hooks.c).
#include "hooks.h"
#include "adapters.h"

#define HOOK(addr, name, argbytes)
#define HOOKY(addr, name, argbytes) static void co_##name(Emu *e, X86 *c) { decomp_coro_run(e, c, hk_##name); }
#include "hooklist.h"
#undef HOOK
#undef HOOKY

const DecompHook decomp_hooks[] = {
#define HOOK(addr, name, argbytes) { addr, #name, hk_##name, argbytes },
#define HOOKY(addr, name, argbytes) { addr, #name, co_##name, argbytes },
#include "hooklist.h"
#undef HOOK
#undef HOOKY
    { 0, NULL, NULL, 0 }
};
const int decomp_nhooks = (int)(sizeof decomp_hooks / sizeof decomp_hooks[0]) - 1;
