#pragma once
#include "hooks.h"
#define HOOK(addr, name, argbytes) void hk_##name(Emu *e, X86 *c);
#define HOOKY HOOK
#include "hooklist.h"
#undef HOOK
#undef HOOKY
