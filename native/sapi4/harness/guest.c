// Calls from decompiled C into functions of the original that are not decompiled yet. Each stub
// marshals its arguments to the guest (pointers to C locals are copied onto the guest stack, below the
// hooked function's frame) and runs the original with emu_call. A portable build has no stubs: a
// function listed here must be decompiled before it links.
#include "hooks.h"
#include "crt_vc.h"
#include "unitsel.h"
#include "containers.h"
#include "voice.h"
#include "fe_vm.h"
#include "fe_input.h"
#include "fe_phones.h"
#include "fe_init.h"
#include <string.h>

static uint32_t guest_call(uint32_t pref, int nargs, const uint32_t *args) {
    return emu_call(decomp_emu, pref + decomp_load_delta, nargs, args);
}
static uint32_t host_to_guest_ptr(const void *p) { return p ? (uint32_t)((const uint8_t *)p - decomp_guest_mem) : 0; }

int32_t tree_query(const SenoneTree *t, TreeQuery *q) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - (uint32_t)sizeof *q) & ~15u;
    uint32_t gq = c->s.r[ESP];
    memcpy(decomp_guest_mem + gq, q, sizeof *q);
    uint32_t args[2] = { host_to_guest_ptr(t), gq };
    int32_t r = (int32_t)guest_call(0x6367233c, 2, args);
    memcpy(q, decomp_guest_mem + gq, sizeof *q);
    c->s.r[ESP] = sp;
    return r;
}

__attribute__((unused)) static uint32_t guest_thiscall(uint32_t pref, const void *self, int nargs, const uint32_t *args) {
    emu_cpu(decomp_emu)->s.r[ECX] = host_to_guest_ptr(self);
    return guest_call(pref, nargs, args);
}

double lattice_cost_other(uint32_t fn, int32_t a, int32_t b) {
    uint32_t args[2] = { (uint32_t)a, (uint32_t)b };
    emu_call(decomp_emu, fn, 2, args);
    return x86_fpu_pop(emu_cpu(decomp_emu));
}

int32_t voc_resample(uint32_t obj, uint32_t method, float *in, GPTR(float) *out, int32_t n) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - 16u) & ~15u;
    uint32_t g = c->s.r[ESP];
    memcpy(decomp_guest_mem + g, out, 4);
    uint32_t args[3] = { host_to_guest_ptr(in), g, (uint32_t)n };
    c->s.r[ECX] = obj;
    int32_t r = (int32_t)guest_call(method, 3, args);
    memcpy(out, decomp_guest_mem + g, 4);
    c->s.r[ESP] = sp;
    return r;
}

void voc_resample_flush(uint32_t obj, uint32_t method) {
    emu_cpu(decomp_emu)->s.r[ECX] = obj;
    guest_call(method, 0, NULL);
}

uint8_t vm_proc_guest(int inst, uint8_t n, int16_t *left, int16_t *len) {
    static const uint32_t table[3] = { 0x63721cd0, 0x63721f30, 0x63721f80 };
    uint32_t fn = CODEADDR(DLLVAR(uint32_t, table[inst])[n]);
    uint32_t a[2] = { host_to_guest_ptr(left), host_to_guest_ptr(len) };
    return (uint8_t)guest_call(fn, 2, a);
}

int32_t text_from_other(char *text, GPTR(char) *out) {
    X86 *c = emu_cpu(decomp_emu);
    uint32_t sp = c->s.r[ESP];
    c->s.r[ESP] = (sp - 16u) & ~15u;
    uint32_t g = c->s.r[ESP];
    memcpy(decomp_guest_mem + g, out, 4);
    uint32_t args[2] = { host_to_guest_ptr(text), g };
    int32_t r = (int32_t)guest_call(0x636744c4, 2, args);
    memcpy(out, decomp_guest_mem + g, 4);
    c->s.r[ESP] = sp;
    return r;
}



int32_t senone_tree_load_old(void *stg, const char *name, const void *ps, GPTR(SenoneTree) *out) {
    uint32_t a[4] = { host_to_guest_ptr(stg), host_to_guest_ptr(name), host_to_guest_ptr(ps), host_to_guest_ptr(out) };
    return (int32_t)guest_call(0x636727fd, 4, a);
}

uint32_t voc_resampler_new(int slot) {
    static const uint32_t size[4] = { 0x19c, 0x138, 0x138, 0xa0 };
    static const uint32_t ctor[4] = { 0x63678180, 0x6367801c, 0x63677eb8, 0x636782e4 };
    uint32_t obj = host_to_guest_ptr(vc_malloc(size[slot]));
    if (!obj) return 0;
    emu_cpu(decomp_emu)->s.r[ECX] = obj;
    return guest_call(ctor[slot], 0, NULL);
}
