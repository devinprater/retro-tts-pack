// OLE32 structured storage, read-only: StgOpenStorage + IStorage / IStream host objects over a
// small compound-file (MS-CFB v3/v4) reader.
#include "emu_internal.h"
#include <fcntl.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define A(i) x86_arg(c, (i))

#include "cfb.h"

// ------------------------------------------------------------------ host objects
// Host objects live in a per-Emu table (Emu.ole_objs); the guest object is [vtable][table index].
typedef struct OleObj { int kind; int refs; Cfb *f; uint32_t dir; uint8_t *data; size_t len, pos; char name[64]; } OleObj; // kind 1 storage, 2 stream

static OleObj *obj_of(Emu *e, X86 *c, uint32_t guest) {
    uint32_t id = rd32(c, guest + 4);
    if (id >= 512 || !e->ole_objs[id]) x86_fault(c, "bad OLE object %08x", guest);
    return e->ole_objs[id];
}
static uint32_t new_obj(Emu *e, OleObj *o) {
    int id = -1;
    for (int i = 1; i < 512; i++) if (!e->ole_objs[i]) { id = i; break; }
    if (id < 0) x86_fault(&e->cpu, "too many OLE objects");
    e->ole_objs[id] = o;
    uint32_t g = emu_malloc(e, 8);
    wr32(&e->cpu, g, o->kind == 1 ? e->ole_stg_vt : e->ole_stm_vt);
    wr32(&e->cpu, g + 4, (uint32_t)id);
    return g;
}
static void wname(Emu *e, uint32_t a, char *out, size_t n) {
    size_t len;
    const uint16_t *w = emu_wstr(e, a, &len);
    size_t k = 0;
    for (; k < len && k < n - 1; k++) out[k] = (char)w[k];
    out[k] = 0;
}

static void o_QI(Emu *e, X86 *c) { (void)e; wr32(c, A(2), A(0)); obj_of(e, c, A(0))->refs++; E_RET(0); }
static void o_AddRef(Emu *e, X86 *c) { E_RET(++obj_of(e, c, A(0))->refs); }
static void o_Release(Emu *e, X86 *c) {
    OleObj *o = obj_of(e, c, A(0));
    uint32_t r = (uint32_t)--o->refs;
    if (!r) {
        uint32_t id = rd32(c, A(0) + 4);
        if (o->kind == 2) free(o->data);
        cfb_release(o->f);
        free(o);
        e->ole_objs[id] = NULL;
        emu_free(e, A(0));
    }
    E_RET(r);
}
static void o_notimpl(Emu *e, X86 *c) {
    (void)e;
    uint32_t idx = (c->s.eip - EMU_THUNK_BASE) / 4;
    (void)idx;
    EMU_LOG("[ole] %s not implemented\n", e->hosts[idx].name);
    E_RET(0x80004001u);
}
static void stg_OpenStream(Emu *e, X86 *c) {
    OleObj *s = obj_of(e, c, A(0));
    char name[64];
    wname(e, A(1), name, sizeof name);
    int i = cfb_find(s->f, s->f->ents[s->dir].child, name);
    if (i < 0 || s->f->ents[i].type != 2) {
        if (emu_trace_api) EMU_LOG("[ole] OpenStream('%s') -> not found\n", name);
        wr32(c, A(5), 0);
        E_RET(0x80030002u);
        return;
    }
    OleObj *o = calloc(1, sizeof *o);
    o->kind = 2; o->refs = 1; o->f = s->f; s->f->refs++;
    o->data = cfb_stream(s->f, &s->f->ents[i], &o->len);
    snprintf(o->name, sizeof o->name, "%s", name);
    wr32(c, A(5), new_obj(e, o));
    E_RET(0);
}
static void stg_OpenStorage(Emu *e, X86 *c) {
    OleObj *s = obj_of(e, c, A(0));
    char name[64];
    wname(e, A(1), name, sizeof name);
    int i = cfb_find(s->f, s->f->ents[s->dir].child, name);
    if (i < 0 || s->f->ents[i].type != 1) { wr32(c, A(6), 0); E_RET(0x80030002u); return; }
    OleObj *o = calloc(1, sizeof *o);
    o->kind = 1; o->refs = 1; o->f = s->f; s->f->refs++; o->dir = (uint32_t)i;
    snprintf(o->name, sizeof o->name, "%s", name);
    wr32(c, A(6), new_obj(e, o));
    E_RET(0);
}
static void fill_stat(Emu *e, X86 *c, uint32_t st, int type, uint64_t size) {
    memset(emu_ptr(e, st, 72), 0, 72);
    wr32(c, st + 4, (uint32_t)type);
    wr64(c, st + 8, size);
}
static void stg_Stat(Emu *e, X86 *c) { obj_of(e, c, A(0)); fill_stat(e, c, A(1), 1, 0); E_RET(0); }
static void stm_Read(Emu *e, X86 *c) {
    OleObj *o = obj_of(e, c, A(0));
    uint32_t n = A(2);
    size_t avail = o->pos < o->len ? o->len - o->pos : 0;
    if (n > avail) n = (uint32_t)avail;
    if (n) memcpy(emu_ptr(e, A(1), n), o->data + o->pos, n);
    o->pos += n;
    if (A(3)) wr32(c, A(3), n);
    E_RET(0);  // S_OK even on short read (as OLE does)
}
static void stm_Seek(Emu *e, X86 *c) {
    OleObj *o = obj_of(e, c, A(0));
    int64_t move = (int64_t)(((uint64_t)A(2) << 32) | A(1));
    uint32_t origin = A(3), out = A(4);
    int64_t base = origin == 0 ? 0 : origin == 1 ? (int64_t)o->pos : (int64_t)o->len;
    int64_t np = base + move;
    if (np < 0) { E_RET(0x80030019u); return; } // STG_E_INVALIDFUNCTION
    o->pos = (size_t)np;
    if (out) wr64(c, out, (uint64_t)np);
    E_RET(0);
}
static void stm_Stat(Emu *e, X86 *c) { OleObj *o = obj_of(e, c, A(0)); fill_stat(e, c, A(1), 2, o->len); E_RET(0); }
static void stm_Write(Emu *e, X86 *c) { (void)e; if (A(3)) wr32(c, A(3), 0); E_RET(0x80030005u); } // STG_E_ACCESSDENIED

static void h_StgOpenStorage(Emu *e, X86 *c) {
    char name[300], host[1100];
    wname(e, A(0), name, sizeof name);
    uint32_t pp = A(5);
    if (pp) wr32(c, pp, 0);
    if (!emu_host_path(e, name, host, sizeof host, 1)) { EMU_LOG("[ole] StgOpenStorage('%s') -> not found\n", name); E_RET(0x80030002u); return; }
    Cfb *f = cfb_open(host);
    if (!f) { EMU_LOG("[ole] StgOpenStorage('%s') -> not a compound file\n", name); E_RET(0x80030050u); return; } // STG_E_FILEALREADYEXISTS-ish
    OleObj *o = calloc(1, sizeof *o);
    o->kind = 1; o->refs = 1; o->f = f; o->dir = 0;
    snprintf(o->name, sizeof o->name, "%s", name);
    if (emu_trace_api) EMU_LOG("[ole] StgOpenStorage('%s') ok, %u entries\n", name, f->nents);
    wr32(c, pp, new_obj(e, o));
    E_RET(0);
}

void emu_register_ole(Emu *e) {
    static const EmuMethod stg[] = {
        { "IStorage::QueryInterface", o_QI, 2 }, { "IStorage::AddRef", o_AddRef, 0 }, { "IStorage::Release", o_Release, 0 },
        { "IStorage::CreateStream", o_notimpl, 5 }, { "IStorage::OpenStream", stg_OpenStream, 5 },
        { "IStorage::CreateStorage", o_notimpl, 5 }, { "IStorage::OpenStorage", stg_OpenStorage, 6 },
        { "IStorage::CopyTo", o_notimpl, 4 }, { "IStorage::MoveElementTo", o_notimpl, 4 }, { "IStorage::Commit", o_notimpl, 1 },
        { "IStorage::Revert", o_notimpl, 0 }, { "IStorage::EnumElements", o_notimpl, 4 }, { "IStorage::DestroyElement", o_notimpl, 1 },
        { "IStorage::RenameElement", o_notimpl, 2 }, { "IStorage::SetElementTimes", o_notimpl, 4 }, { "IStorage::SetClass", o_notimpl, 1 },
        { "IStorage::SetStateBits", o_notimpl, 2 }, { "IStorage::Stat", stg_Stat, 2 },
    };
    static const EmuMethod stm[] = {
        { "IStream::QueryInterface", o_QI, 2 }, { "IStream::AddRef", o_AddRef, 0 }, { "IStream::Release", o_Release, 0 },
        { "IStream::Read", stm_Read, 3 }, { "IStream::Write", stm_Write, 3 }, { "IStream::Seek", stm_Seek, 4 },
        { "IStream::SetSize", o_notimpl, 2 }, { "IStream::CopyTo", o_notimpl, 5 }, { "IStream::Commit", o_notimpl, 1 },
        { "IStream::Revert", o_notimpl, 0 }, { "IStream::LockRegion", o_notimpl, 5 }, { "IStream::UnlockRegion", o_notimpl, 5 },
        { "IStream::Stat", stm_Stat, 2 }, { "IStream::Clone", o_notimpl, 1 },
    };
    e->ole_stg_vt = emu_vtable(e, stg, 18);
    e->ole_stm_vt = emu_vtable(e, stm, 14);
}

void emu_register_ole_apis(void) {
    static const ApiDef apis[] = { { "OLE32.dll", "StgOpenStorage", h_StgOpenStorage, 24 } };
    emu_add_apis(apis, 1);
}

// frees the host side of every OLE object still open (emu_destroy)
void emu_ole_cleanup(Emu *e) {
    for (int i = 0; i < 512; i++) {
        OleObj *o = e->ole_objs[i];
        if (!o) continue;
        if (o->kind == 2) free(o->data);
        cfb_release(o->f);
        free(o);
        e->ole_objs[i] = NULL;
    }
}
