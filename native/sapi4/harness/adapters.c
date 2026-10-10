// Adapters: guest arguments -> the portable C in src/ -> guest results. Pointers are translated to host
// pointers into guest memory (bounds-checked for the extent the function touches).
#include "adapters.h"
#include "fe_lex.h"
#include "fe_word.h"
#include "fe_phrase.h"
#include "fe_output.h"
#include "fe_text.h"
#include "fe_lexer.h"
#include "fe_split.h"
#include "fe_input.h"
#include "fe_reader.h"
#include "fe_phones.h"
#include "fe_init.h"
static FILE *trace_file(void);
#include "dsp.h"
#include "fft.h"
#include "lpc.h"
#include "codec.h"
#include "effect.h"
#include "voice.h"
#include "tags.h"
#include "containers.h"
#include "senone.h"
#include "unitsel.h"
#include "fe_small.h"
#include "fe_rules.h"
#include "queue.h"
#include "fe_vm.h"
#include "voices.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define A(i) x86_arg(c, (i))
#define AI(i) ((int32_t)x86_arg(c, (i)))
static inline float argf(X86 *c, int i) { uint32_t u = x86_arg(c, i); float f; memcpy(&f, &u, 4); return f; }
// a double argument in stack slots i and i+1
static inline double argd2(X86 *c, int i) { uint64_t u = (uint64_t)x86_arg(c, i) | (uint64_t)x86_arg(c, i + 1) << 32; double d; memcpy(&d, &u, 8); return d; }
// host pointer to n elements of guest memory at a (NULL when n <= 0: the function never dereferences it)
#define PTR(T, a, n) ((n) > 0 ? GPH(T, (a), (uint32_t)(n) * sizeof(T)) : (T *)NULL)
// host pointer to guest a, checking the element range [lo, hi) the function touches (may be negative)
static void *span(X86 *c, uint32_t a, long lo, long hi, uint32_t es) {
    if (hi <= lo) return c->mem + a;
    uint32_t start = a + (uint32_t)(lo * (long)es);
    x86_ptr(c, start, (uint32_t)((hi - lo) * (long)es));
    return c->mem + a;
}
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void hk_dsp_allpole(Emu *e, X86 *c) {
    int order = AI(1), n = AI(4);
    int m = order > 0 ? order + 1 : 1;
    dsp_allpole(PTR(float, A(0), m), order, PTR(float, A(2), m), PTR(float, A(3), n), n);
}
void hk_dsp_fir(Emu *e, X86 *c) {
    int n = AI(1), order = AI(4);
    int m = order > 0 ? order : 1;
    dsp_fir(PTR(float, A(0), n), n, PTR(float, A(2), m), PTR(float, A(3), m), order);
}
void hk_dsp_iir_c0(Emu *e, X86 *c) {
    int n = AI(1), order = AI(4);
    int m = order > 0 ? order : 1;
    dsp_iir_c0(PTR(float, A(0), n), n, PTR(float, A(2), m), PTR(float, A(3), m), order);
}
void hk_dsp_sum_abs(Emu *e, X86 *c) {
    int n = AI(1);
    x86_fpu_push(c, dsp_sum_abs(PTR(float, A(0), n), n));
}
void hk_dsp_scale(Emu *e, X86 *c) {
    int n = AI(1);
    dsp_scale(PTR(float, A(0), n), n, argf(c, 2));
}
void hk_dsp_reverse(Emu *e, X86 *c) {
    int n = AI(1);
    dsp_reverse(PTR(float, A(0), n), n, PTR(float, A(2), n));
}
void hk_dsp_to_pcm16(Emu *e, X86 *c) {
    int n = AI(2);
    dsp_to_pcm16(PTR(int16_t, A(0), n), PTR(float, A(1), n), n);
}
void hk_dsp_to_pcm8(Emu *e, X86 *c) {
    int n = AI(2);
    dsp_to_pcm8(PTR(uint8_t, A(0), n), PTR(float, A(1), n), n);
}
void hk_dsp_max_abs(Emu *e, X86 *c) {
    int n = AI(1);
    x86_fpu_push(c, dsp_max_abs(PTR(float, A(0), n > 1 ? n : 1), n));
}

void hk_dsp_scale_b(Emu *e, X86 *c) {
    int n = AI(1);
    dsp_scale_b(PTR(float, A(0), n), n, argf(c, 2));
}
void hk_dsp_fold_pulse(Emu *e, X86 *c) {
    int n = AI(1), len = AI(3);
    int m = MIN(len / 2, n);
    float *dst = span(c, A(0), MIN(0, m), MAX(n, 0), 4);
    const float *src = span(c, A(2), 0, MAX(MAX(m, 0), len), 4);
    dsp_fold_pulse(dst, n, src, len);
}
static void halves(X86 *c, int add) {
    int off = AI(2), n = AI(3), len = AI(4);
    long b = (long)len - off - n + 1;
    float *dst = n > 0 ? span(c, A(0), MIN(off, b), MAX(off + n, b + n), 4) : NULL;
    const float *src = n > 0 ? span(c, A(1), 0, 2L * n, 4) : NULL;
    if (add) dsp_add_halves(dst, src, off, n, len); else dsp_place_halves(dst, src, off, n, len);
}
void hk_dsp_place_halves(Emu *e, X86 *c) { halves(c, 0); }
void hk_dsp_add_halves(Emu *e, X86 *c) { halves(c, 1); }
void hk_dsp_cmp_float(Emu *e, X86 *c) {
    E_RET(dsp_cmp_float(GPH(float, A(0), 4), GPH(float, A(1), 4)));
}
void hk_dsp_sort_floats(Emu *e, X86 *c) {
    int n = AI(1);
    E_RET(dsp_sort_floats(PTR(float, A(0), n), n));
}

void hk_fft_inverse_real(Emu *e, X86 *c) {
    int n = AI(1), m = AI(2);
    // the table is read up to sine[3*step*j + n/2] < 2n
    fft_inverse_real(PTR(float, A(0), n), n, m, PTR(float, A(3), n > 0 ? 2 * n : 0));
}

void hk_lpc_from_lsf(Emu *e, X86 *c) {
    int order = AI(2);
    lpc_from_lsf(PTR(float, A(0), order), PTR(float, A(1), order + 1), order, AI(3));
}

void hk_dsp_fit_period(Emu *e, X86 *c) {
    int srcn = AI(1), dstn = AI(3), winn = AI(5);
    dsp_fit_period(PTR(float, A(0), MAX(srcn, 1)), srcn, PTR(float, A(2), MAX(dstn, 1)), dstn,
                   PTR(float, A(4), MAX(winn + 1, 1)), winn);
}
void hk_dsp_period(Emu *e, X86 *c) {
    int srcn = AI(1), dstn = AI(3), winn = AI(7);
    dsp_period(PTR(float, A(0), MAX(MAX(srcn, dstn), 1)), srcn, PTR(float, A(2), MAX(dstn, 1)), dstn, argf(c, 4),
               AI(5), PTR(float, A(6), MAX(winn + 1, 1)), winn);
}
void hk_codec_bytes_to_floats(Emu *e, X86 *c) {
    int n = AI(1);
    // the escapes make the byte extent data dependent: check the start, then walk
    const int8_t *p = (const int8_t *)x86_ptr(c, A(0), 1);
    E_RET(codec_bytes_to_floats(p, n, PTR(float, A(2), n), AI(3)));
}

// thiscall: `this` in ECX
#define THIS(T) GPH(T, c->s.r[ECX], sizeof(T))
void hk_effect_db_to_gain(Emu *e, X86 *c) { x86_fpu_push(c, effect_db_to_gain(THIS(Effect), argf(c, 0))); }
void hk_effect_gain_copy(Emu *e, X86 *c) {
    uint32_t n = A(2);
    effect_gain_copy(THIS(Effect), PTR(float, A(0), n), PTR(float, A(1), n), n, argf(c, 3));
}
void hk_effect_mix_clip(Emu *e, X86 *c) {
    uint32_t n = A(2);
    effect_mix_clip(THIS(Effect), PTR(float, A(0), n), PTR(float, A(1), n), n, argf(c, 3));
}
void hk_effect_allpass(Emu *e, X86 *c) {
    uint32_t n = A(1);
    effect_allpass(THIS(Effect), GPH(Allpass, A(0), sizeof(Allpass)), n, PTR(float, A(2), n), PTR(float, A(3), n));
}
void hk_effect_chain(Emu *e, X86 *c) {
    int n = AI(1);
    effect_chain(THIS(Effect), PTR(float, A(0), n), n, GPH(uint32_t, A(2), 20));
}
void hk_effect_process(Emu *e, X86 *c) {
    int n = AI(1);
    effect_process(THIS(Effect), PTR(float, A(0), n), n);
}

static uint32_t host_to_guest(const void *p) { return p ? (uint32_t)((const uint8_t *)p - decomp_guest_mem) : 0; }
void hk_effect_ap_clear(Emu *e, X86 *c) { effect_ap_clear(THIS(Effect), GPH(Allpass, A(0), sizeof(Allpass))); E_RET(A(0)); }
void hk_effect_ap_init(Emu *e, X86 *c) {
    E_RET(effect_ap_init(THIS(Effect), GPH(Allpass, A(0), sizeof(Allpass)), argf(c, 1), AI(2), AI(3)));
}
void hk_effect_build_stages(Emu *e, X86 *c) {
    int16_t n = (int16_t)A(0);
    int k = n > 0 ? n : 1;
    E_RET((uint16_t)effect_build_stages(THIS(Effect), n, GPH(uint32_t, A(1), 4u * k), PTR(float, A(2), k),
                                        PTR(float, A(3), k), argf(c, 4)));
}
void hk_effect_free(Emu *e, X86 *c) { effect_free(THIS(Effect)); }
void hk_effect_preset(Emu *e, X86 *c) { E_RET(effect_preset(THIS(Effect), AI(0))); }
void hk_effect_init(Emu *e, X86 *c) { E_RET((uint16_t)effect_init(THIS(Effect), AI(0), AI(1))); }
void hk_effect_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(effect_ctor(THIS(Effect)))); }

void hk_voc_unit_decode(Emu *e, X86 *c) {
    E_RET(host_to_guest(voc_unit_decode(AI(0), GPH(VocVoice, A(1), sizeof(VocVoice)), GPH(VocSynthState, A(2), sizeof(VocSynthState)))));
}

void hk_voc_unit_free(Emu *e, X86 *c) { voc_unit_free(GPH(VocUnit, A(0), sizeof(VocUnit))); }
// the extents of these arrays are data dependent (counts, frame indices); guest memory is contiguous,
// so the host pointers are only range-checked at their start
#define P0(T, a) ((a) ? (T *)(void *)x86_ptr(c, (a), 1) : (T *)NULL)
void hk_voc_pitch_marks(Emu *e, X86 *c) {
    E_RET(voc_pitch_marks(P0(const float, A(0)), AI(1), P0(float, A(2)), argf(c, 3), argf(c, 4), P0(const float, A(5)),
                          P0(const float, A(6)), AI(7), AI(8), AI(9), P0(int32_t, A(10)), P0(float, A(11))));
}
void hk_voc_excitation(Emu *e, X86 *c) {
    voc_excitation(P0(const float, A(0)), AI(1), P0(const float, A(2)), P0(const float, A(3)), AI(4),
                   P0(const int32_t, A(5)), P0(const float, A(6)), P0(float, A(7)), P0(const float, A(8)),
                   P0(const float, A(9)), AI(10), P0(const float, A(11)), AI(12));
}

void hk_voc_synth_unit(Emu *e, X86 *c) {
    E_RET(voc_synth_unit(THIS(VocSynthState), GPH(VocRequest, A(0), sizeof(VocRequest))));
}

// the tag lexer: pp is a guest char** (a pointer to a guest pointer), buffers are bounded by n
#define PP(i) GPH(uint32_t, A(i), 4)
void hk_tag_lex_number(Emu *e, X86 *c) { E_RET(tag_lex_number(PP(0), A(1) ? GPH(int32_t, A(1), 4) : NULL)); }
void hk_tag_lex_word(Emu *e, X86 *c) { int n = AI(2); E_RET(tag_lex_word(PP(0), GPH(char, A(1), n > 0 ? n + 1 : 1), n)); }
void hk_tag_keyword(Emu *e, X86 *c) { E_RET(tag_keyword((const char *)x86_ptr(c, A(0), 1))); }
void hk_tag_lex_string(Emu *e, X86 *c) { int n = AI(2); E_RET(tag_lex_string(PP(0), GPH(char, A(1), n > 1 ? n + 1 : 2), n)); }
void hk_tag_lex_brace(Emu *e, X86 *c) { int n = AI(2); E_RET(tag_lex_brace(PP(0), GPH(char, A(1), n > 1 ? n + 1 : 2), n)); }
void hk_tag_lex(Emu *e, X86 *c) { int n = AI(2); E_RET(tag_lex(PP(0), GPH(char, A(1), n > 4 ? n + 1 : 5), n)); }
#define TAGH(name) void hk_##name(Emu *e, X86 *c) { int n = AI(4); \
    E_RET(name(AI(0), PP(1), GPH(TagOut, A(2), sizeof(TagOut)), GPH(char, A(3), n > 4 ? n + 1 : 5), n)); }
TAGH(tag_parse_unsupported)
TAGH(tag_parse_flag)
TAGH(tag_parse_number)
TAGH(tag_parse_string)
void hk_tag_parse(Emu *e, X86 *c) { E_RET(tag_parse((char *)x86_ptr(c, A(0), 1), GPH(TagOut, A(1), sizeof(TagOut)))); }

#define STR(i) ((const char *)x86_ptr(c, A(i), 1))
void hk_hash_lookup(Emu *e, X86 *c) {
    E_RET(hash_lookup(A(0) ? GPH(HashTable, A(0), sizeof(HashTable)) : NULL, A(1) ? STR(1) : NULL, A(2) ? GPH(int32_t, A(2), 4) : NULL));
}
void hk_named_index(Emu *e, X86 *c) { E_RET(named_index(GPH(NamedTable, A(0), sizeof(NamedTable)), STR(1))); }
void hk_named_item(Emu *e, X86 *c) { E_RET(named_item(GPH(NamedTable, A(0), sizeof(NamedTable)), AI(1))); }
void hk_prefix_cmp(Emu *e, X86 *c) { E_RET(prefix_cmp(STR(0), STR(1), GPH(int32_t, A(2), 4))); }
void hk_intvec_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(intvec_ctor(THIS(IntVec)))); }
void hk_intvec_free_data(Emu *e, X86 *c) { intvec_free_data(THIS(IntVec)); }
void hk_intvec_reserve(Emu *e, X86 *c) { intvec_reserve(THIS(IntVec), AI(0), AI(1)); }
void hk_vecarray_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(vecarray_ctor(THIS(VecArray)))); }
void hk_vecarray_destroy(Emu *e, X86 *c) { vecarray_destroy(THIS(VecArray)); }
void hk_vecarray_init(Emu *e, X86 *c) { E_RET(vecarray_init(THIS(VecArray), AI(0), AI(1), AI(2))); }
void hk_blob_copy80(Emu *e, X86 *c) { E_RET(blob_copy80(GPH(uint8_t, A(0), 0x50), GPH(Blob, A(1), sizeof(Blob)))); }

void hk_senone_lookup(Emu *e, X86 *c) {
    E_RET(senone_lookup(GPH(SenoneTree, A(0), sizeof(SenoneTree)), AI(1), AI(2), AI(3), AI(4), AI(5)));
}
void hk_phone_fixups(Emu *e, X86 *c) { int n = AI(1); phone_fixups(n > 0 ? GPH(char, A(0), 100u * n) : NULL, n); }

void hk_unit_select(Emu *e, X86 *c) { int n = AI(1); unit_select(THIS(UnitStage), n > 0 ? GPH(char, A(0), 100u * n + 1) : (char *)c->mem + A(0), n); }

void hk_unit_prosody(Emu *e, X86 *c) {
    int n = AI(1);
    E_RET(unit_prosody(THIS(UnitStage), n > 0 ? GPH(PhoneIn, A(0), sizeof(PhoneIn) * n) : NULL, n, GPH(int32_t, A(2), 4),
                       AI(3), GPH(UnitOutBuf, A(4), sizeof(UnitOutBuf))));
}
void hk_unit_prosody_a(Emu *e, X86 *c) { unit_prosody_a(THIS(UnitStage), AI(0)); }
void hk_unit_prosody_b(Emu *e, X86 *c) { unit_prosody_b(THIS(UnitStage), AI(0)); }

#define SPAN THIS(TextSpan)
void hk_span_f4(Emu *e, X86 *c) { E_RET(span_f4(SPAN)); }
void hk_span_pos(Emu *e, X86 *c) { E_RET(span_pos(SPAN)); }
void hk_span_len(Emu *e, X86 *c) { E_RET(span_len(SPAN)); }
void hk_span_f10(Emu *e, X86 *c) { E_RET(span_f10(SPAN)); }
void hk_span_cur(Emu *e, X86 *c) { E_RET(host_to_guest(span_cur(SPAN))); }
// the byte getters load AL over another value: EAX keeps that value's upper bits, as in the original
void hk_span_first(Emu *e, X86 *c) { const TextSpan *s = SPAN; E_RET((s->buf & ~0xffu) | (uint8_t)span_first(s)); }
void hk_span_last(Emu *e, X86 *c) { const TextSpan *s = SPAN; E_RET(((uint32_t)(s->pos + s->len) & ~0xffu) | (uint8_t)span_last(s)); }
void hk_span_set_pos(Emu *e, X86 *c) { E_RET(host_to_guest(span_set_pos(SPAN, AI(0)))); }
void hk_span_set_len(Emu *e, X86 *c) { E_RET(host_to_guest(span_set_len(SPAN, AI(0)))); }
void hk_span_set_f10(Emu *e, X86 *c) { E_RET(host_to_guest(span_set_f10(SPAN, AI(0)))); }
void hk_span_clear(Emu *e, X86 *c) { E_RET(host_to_guest(span_clear(SPAN))); }
void hk_span_pair_reset(Emu *e, X86 *c) { E_RET(host_to_guest(span_pair_reset(GPH(TextSpan, c->s.r[ECX], 0x24)))); }
void hk_pair_reset(Emu *e, X86 *c) { pair_reset(THIS(Pair)); }
void hk_block8_clear(Emu *e, X86 *c) { block8_clear(THIS(Block8)); E_RET(0); }
void hk_no_op_false8(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)no_op_false8()); }
void hk_no_op_zero8(Emu *e, X86 *c) { E_RET(no_op_zero8()); }
void hk_free_cdecl(Emu *e, X86 *c) { free_cdecl(A(0) ? (void *)x86_ptr(c, A(0), 1) : NULL); }
void hk_free_if(Emu *e, X86 *c) { E_RET(free_if(A(0) ? (void *)x86_ptr(c, A(0), 1) : NULL)); }
void hk_is_digit_char(Emu *e, X86 *c) { E_RET(is_digit_char((char)A(0))); }
void hk_char_class16(Emu *e, X86 *c) { E_RET(char_class16((uint16_t)A(0))); }
void hk_freelist_push(Emu *e, X86 *c) { freelist_push(GPH(FreeNode, A(0), sizeof(FreeNode))); E_RET(A(0)); }

// rule-module instances: one C function, one adapter per copy in the DLL
void hk_tapes_graphic_0(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)tapes_graphic((int16_t)A(0), (int16_t)A(1))); }
void hk_tapes_graphic_1(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)tapes_graphic((int16_t)A(0), (int16_t)A(1))); }
void hk_tapes_graphic_2(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)tapes_graphic((int16_t)A(0), (int16_t)A(1))); }
void hk_tapes_graphic_3(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)tapes_graphic((int16_t)A(0), (int16_t)A(1))); }
void hk_tapes_graphic_4(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)tapes_graphic((int16_t)A(0), (int16_t)A(1))); }
void hk_tapes_graphic_5(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)tapes_graphic((int16_t)A(0), (int16_t)A(1))); }
void hk_tapes_graphic_6(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)tapes_graphic((int16_t)A(0), (int16_t)A(1))); }
void hk_tapes_graphic_7(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)tapes_graphic((int16_t)A(0), (int16_t)A(1))); }
void hk_tapes_graphic_8(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)tapes_graphic((int16_t)A(0), (int16_t)A(1))); }
void hk_tapes_pad_0(Emu *e, X86 *c) { E_RET((A(0) & 0xffff0000u) | (uint16_t)tapes_pad(DLLVAR(const int16_t, 0x63739780), (int16_t)A(0))); }
void hk_tapes_pad_1(Emu *e, X86 *c) { E_RET((A(0) & 0xffff0000u) | (uint16_t)tapes_pad(DLLVAR(const int16_t, 0x637397b8), (int16_t)A(0))); }
void hk_tapes_pad_2(Emu *e, X86 *c) { E_RET((A(0) & 0xffff0000u) | (uint16_t)tapes_pad(DLLVAR(const int16_t, 0x637397f0), (int16_t)A(0))); }
void hk_tapes_pad_3(Emu *e, X86 *c) { E_RET((A(0) & 0xffff0000u) | (uint16_t)tapes_pad(DLLVAR(const int16_t, 0x63739828), (int16_t)A(0))); }
void hk_tapes_pad_4(Emu *e, X86 *c) { E_RET((A(0) & 0xffff0000u) | (uint16_t)tapes_pad(DLLVAR(const int16_t, 0x63739860), (int16_t)A(0))); }
void hk_tapes_pad_5(Emu *e, X86 *c) { E_RET((A(0) & 0xffff0000u) | (uint16_t)tapes_pad(DLLVAR(const int16_t, 0x63739898), (int16_t)A(0))); }
void hk_tapes_pad_6(Emu *e, X86 *c) { E_RET((A(0) & 0xffff0000u) | (uint16_t)tapes_pad(DLLVAR(const int16_t, 0x637398d0), (int16_t)A(0))); }
void hk_tapes_pad_7(Emu *e, X86 *c) { E_RET((A(0) & 0xffff0000u) | (uint16_t)tapes_pad(DLLVAR(const int16_t, 0x63739908), (int16_t)A(0))); }
void hk_tapes_pad_8(Emu *e, X86 *c) { E_RET((A(0) & 0xffff0000u) | (uint16_t)tapes_pad(DLLVAR(const int16_t, 0x63739948), (int16_t)A(0))); }
void hk_tapes_pad_9(Emu *e, X86 *c) { E_RET((A(0) & 0xffff0000u) | (uint16_t)tapes_pad(DLLVAR(const int16_t, 0x63739988), (int16_t)A(0))); }
void hk_rule_bsearch_0(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | rule_bsearch(DLLVAR(const uint8_t, 0x636cad90), A(0), (uint8_t)A(1), (uint8_t)A(2), (uint8_t)A(3))); }
void hk_rule_bsearch_1(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | rule_bsearch(DLLVAR(const uint8_t, 0x636cda80), A(0), (uint8_t)A(1), (uint8_t)A(2), (uint8_t)A(3))); }
void hk_rule_bsearch_2(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | rule_bsearch(DLLVAR(const uint8_t, 0x636ce848), A(0), (uint8_t)A(1), (uint8_t)A(2), (uint8_t)A(3))); }
void hk_rule_bsearch_3(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | rule_bsearch(DLLVAR(const uint8_t, 0x636d3e78), A(0), (uint8_t)A(1), (uint8_t)A(2), (uint8_t)A(3))); }
void hk_rule_bsearch_4(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | rule_bsearch(DLLVAR(const uint8_t, 0x63718da0), A(0), (uint8_t)A(1), (uint8_t)A(2), (uint8_t)A(3))); }

#define Q THIS(Queue)
void hk_queue_grow(Emu *e, X86 *c) { E_RET(queue_grow(Q, A(0))); }
void hk_queue_update_events(Emu *e, X86 *c) { queue_update_events(Q); }
void hk_queue_count(Emu *e, X86 *c) { E_RET(queue_count(Q)); }
void hk_queue_is_full(Emu *e, X86 *c) { E_RET(queue_is_full(Q)); }
void hk_queue_push(Emu *e, X86 *c) { E_RET(queue_push(Q, A(0) ? GPH(QItem, A(0), 20) : NULL)); }
void hk_queue_push_n(Emu *e, X86 *c) { uint32_t n = A(1); E_RET(queue_push_n(Q, A(0) ? GPH(QItem, A(0), n ? 20u * n : 1) : NULL, n)); }
void hk_queue_pop(Emu *e, X86 *c) { E_RET(queue_pop(Q, A(0) ? GPH(QItem, A(0), 20) : NULL)); }
void hk_locked_get_58(Emu *e, X86 *c) { E_RET(locked_get_58(GPH(uint8_t, c->s.r[ECX], 0x70))); }
void hk_locked_get_11c(Emu *e, X86 *c) { E_RET(locked_get_11c(GPH(uint8_t, c->s.r[ECX], 0x134))); }
void hk_locked_get_2cc(Emu *e, X86 *c) { E_RET(locked_get_2cc(GPH(uint8_t, c->s.r[ECX], 0x2e4))); }

void hk_unit_rate_update(Emu *e, X86 *c) { unit_rate_update(THIS(UnitStage)); }
void hk_unit_set_rate(Emu *e, X86 *c) { E_RET(unit_set_rate(THIS(UnitStage), A(0))); }
void hk_unit_reset_rate(Emu *e, X86 *c) { E_RET(unit_reset_rate(THIS(UnitStage))); }
void hk_unit_set_amp(Emu *e, X86 *c) { E_RET(unit_set_amp(THIS(UnitStage), A(0))); }

void hk_unit_send(Emu *e, X86 *c) { unit_send(THIS(UnitStage), GPH(UnitOutBuf, A(0), sizeof(UnitOutBuf))); }
// SAPI4_TRACE_PHONES=file: the phone lists the unit stage receives (to compare with port/fe.c)
static FILE *trace_file(void) {
    static FILE *f;
    static int tried;
    if (!tried) {
        tried = 1;
        const char *p = getenv("SAPI4_TRACE_PHONES");
        if (p) f = fopen(p, "w");
    }
    return f;
}
void phones_print(FILE *f, const PhoneIn *ph, int32_t n);
void hk_unit_phrase(Emu *e, X86 *c) {
    PhoneIn *ph = (PhoneIn *)(void *)x86_ptr(c, A(0), 1);
    if (trace_file()) phones_print(trace_file(), ph, AI(1));
    E_RET(unit_phrase(THIS(UnitStage), ph, AI(1)));
}
void hk_unit_thread_loop(Emu *e, X86 *c) { E_RET(unit_thread_loop(THIS(UnitStage))); }
void hk_unit_thread_proc(Emu *e, X86 *c) { E_RET(unit_thread_loop(GPH(UnitStage, A(0), sizeof(UnitStage)))); }

void hk_intvec_append(Emu *e, X86 *c) { E_RET(intvec_append(THIS(IntVec), AI(0))); }
void hk_vecarray_append(Emu *e, X86 *c) { E_RET(vecarray_append(THIS(VecArray), AI(0), AI(1))); }
void hk_lattice_cost(Emu *e, X86 *c) { x86_fpu_push(c, lattice_cost(AI(0), AI(1))); }
void hk_array_lacks(Emu *e, X86 *c) { int32_t n = AI(1); E_RET(array_lacks((int16_t)A(0), n, n > 0 ? GPH(int16_t, A(2), 2u * n) : NULL)); }
void hk_lattice_viterbi(Emu *e, X86 *c) { E_RET(lattice_viterbi(THIS(VecArray), A(0))); }

void hk_lattice_match(Emu *e, X86 *c) {
    E_RET(lattice_match((const char *)x86_ptr(c, A(0), 1), (const int16_t *)(void *)(c->mem + A(1)), AI(2),
                        GPH(int32_t, A(3), 4), AI(4), GPH(AltRules, A(5), 4), GPH(VecArray, A(6), sizeof(VecArray))));
}
void hk_unit_span_lookup(Emu *e, X86 *c) {
    E_RET(host_to_guest(unit_span_lookup(AI(0), AI(1), GPH(uint32_t, A(2), 4), GPH(uint32_t, A(3), 4), GPH(int32_t, A(4), 4))));
}
void hk_unit_lattice(Emu *e, X86 *c) { E_RET(unit_lattice(THIS(UnitStage), GPH(uint32_t, A(0), 4), GPH(int32_t, A(1), 4))); }

void hk_phone_table_index(Emu *e, X86 *c) { E_RET(phone_table_index(A(0) ? (const char *)x86_ptr(c, A(0), 1) : NULL)); }
void hk_voc_synth_request(Emu *e, X86 *c) {
    E_RET(voc_synth_request(THIS(VocEngine), GPH(UnitOut, A(0), sizeof(UnitOut)), GPH(FloatBuf, A(1), 8), GPH(int32_t, A(2), 4)));
}
void hk_voc_send_audio(Emu *e, X86 *c) { voc_send_audio(THIS(VocEngine), A(0) ? GPH(FloatBuf, A(0), 8) : NULL, AI(1)); }

void hk_identity32(Emu *e, X86 *c) { E_RET(identity32(A(0))); }
void hk_voc_thread_loop(Emu *e, X86 *c) { E_RET(voc_thread_loop(THIS(VocEngine))); }
void hk_voc_thread_proc(Emu *e, X86 *c) { E_RET(voc_thread_loop(GPH(VocEngine, A(0), sizeof(VocEngine)))); }
void hk_vm_shift_right_0(Emu *e, X86 *c) { vm_shift_right(&rule_vm[0], (int16_t)A(0), (int16_t)A(1)); }
void hk_vm_shift_right_1(Emu *e, X86 *c) { vm_shift_right(&rule_vm[1], (int16_t)A(0), (int16_t)A(1)); }
void hk_vm_shift_right_2(Emu *e, X86 *c) { vm_shift_right(&rule_vm[2], (int16_t)A(0), (int16_t)A(1)); }
void hk_vm_shift_left_0(Emu *e, X86 *c) { E_RET(vm_shift_left(&rule_vm[0], (int16_t)A(0), (int16_t)A(1), c->s.r[EAX])); }
void hk_vm_shift_left_1(Emu *e, X86 *c) { E_RET(vm_shift_left(&rule_vm[1], (int16_t)A(0), (int16_t)A(1), c->s.r[EAX])); }
void hk_vm_shift_left_2(Emu *e, X86 *c) { E_RET(vm_shift_left(&rule_vm[2], (int16_t)A(0), (int16_t)A(1), c->s.r[EAX])); }
void hk_vm_pop_range_0(Emu *e, X86 *c) { vm_pop_range(&rule_vm[0], (int16_t)A(0), (int16_t)A(1)); }
void hk_vm_pop_range_1(Emu *e, X86 *c) { vm_pop_range(&rule_vm[1], (int16_t)A(0), (int16_t)A(1)); }
void hk_vm_pop_range_2(Emu *e, X86 *c) { vm_pop_range(&rule_vm[2], (int16_t)A(0), (int16_t)A(1)); }
void hk_vm_push_until_0(Emu *e, X86 *c) { E_RET(vm_push_until(&rule_vm[0], AI(0))); }
void hk_vm_push_until_1(Emu *e, X86 *c) { E_RET(vm_push_until(&rule_vm[1], AI(0))); }
void hk_vm_push_until_2(Emu *e, X86 *c) { E_RET(vm_push_until(&rule_vm[2], AI(0))); }
void hk_vm_skip_block_0(Emu *e, X86 *c) { E_RET(vm_skip_block(&rule_vm[0], (uint8_t)A(0), (uint8_t)A(1))); }
void hk_vm_skip_block_1(Emu *e, X86 *c) { E_RET(vm_skip_block(&rule_vm[1], (uint8_t)A(0), (uint8_t)A(1))); }
void hk_vm_skip_block_2(Emu *e, X86 *c) { E_RET(vm_skip_block(&rule_vm[2], (uint8_t)A(0), (uint8_t)A(1))); }
void hk_vm_move_0(Emu *e, X86 *c) { E_RET(vm_move(&rule_vm[0], AI(0), AI(1), AI(2))); }
void hk_vm_move_1(Emu *e, X86 *c) { E_RET(vm_move(&rule_vm[1], AI(0), AI(1), AI(2))); }
void hk_vm_move_2(Emu *e, X86 *c) { E_RET(vm_move(&rule_vm[2], AI(0), AI(1), AI(2))); }
void hk_vm_cond_0(Emu *e, X86 *c) { E_RET(vm_cond(&cond_vm[0], (uint8_t)A(0), AI(1))); }
void hk_vm_cond_1(Emu *e, X86 *c) { E_RET(vm_cond(&cond_vm[1], (uint8_t)A(0), AI(1))); }
void hk_vm_cond_2(Emu *e, X86 *c) { E_RET(vm_cond(&cond_vm[2], (uint8_t)A(0), AI(1))); }
void hk_vm_run_0(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | vm_run(0)); }
void hk_vm_run_1(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | vm_run(1)); }
void hk_vm_run_2(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | vm_run(2)); }
void hk_vm_try_0(Emu *e, X86 *c) { E_RET(vm_try(0, AI(0), AI(1), GPH(RuleHdr, A(2), sizeof(RuleHdr)), c->s.r[EAX])); }
void hk_vm_try_1(Emu *e, X86 *c) { E_RET(vm_try(1, AI(0), AI(1), GPH(RuleHdr, A(2), sizeof(RuleHdr)), c->s.r[EAX])); }
void hk_vm_try_2(Emu *e, X86 *c) { E_RET(vm_try(2, AI(0), AI(1), GPH(RuleHdr, A(2), sizeof(RuleHdr)), c->s.r[EAX])); }
void hk_vm_match_0(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & 0xffff0000u) | (uint16_t)vm_match(0, AI(0), AI(1))); }
void hk_vm_match_1(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & 0xffff0000u) | (uint16_t)vm_match(1, AI(0), AI(1))); }
void hk_vm_match_2(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & 0xffff0000u) | (uint16_t)vm_match(2, AI(0), AI(1))); }
void hk_vm_apply_0(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | vm_apply(0, AI(0), GPH(RuleHdr, A(1), sizeof(RuleHdr)))); }
void hk_vm_apply_1(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | vm_apply(1, AI(0), GPH(RuleHdr, A(1), sizeof(RuleHdr)))); }
void hk_vm_apply_2(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | vm_apply(2, AI(0), GPH(RuleHdr, A(1), sizeof(RuleHdr)))); }
void hk_vm_exec_0(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | vm_exec(0)); }
void hk_vm_exec_1(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | vm_exec(1)); }
void hk_vm_exec_2(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | vm_exec(2)); }
void hk_vm_main_0(Emu *e, X86 *c) { E_RET(vm_main(0, AI(0), AI(1), GPH(int16_t, A(2), 2))); }
void hk_vm_main_1(Emu *e, X86 *c) { E_RET(vm_main(1, AI(0), AI(1), GPH(int16_t, A(2), 2))); }
void hk_vm_main_2(Emu *e, X86 *c) { E_RET(vm_main(2, AI(0), AI(1), GPH(int16_t, A(2), 2))); }
void hk_vm_proc_6368b28b(Emu *e, X86 *c) { E_RET(vm_proc(0, 3, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b2e0(Emu *e, X86 *c) { E_RET(vm_proc(0, 4, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b6df(Emu *e, X86 *c) { E_RET(vm_proc(0, 5, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b6f5(Emu *e, X86 *c) { E_RET(vm_proc(0, 6, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b70d(Emu *e, X86 *c) { E_RET(vm_proc(0, 7, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b725(Emu *e, X86 *c) { E_RET(vm_proc(0, 8, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b73d(Emu *e, X86 *c) { E_RET(vm_proc(0, 9, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b755(Emu *e, X86 *c) { E_RET(vm_proc(0, 10, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b305(Emu *e, X86 *c) { E_RET(vm_proc(0, 12, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b6bc(Emu *e, X86 *c) { E_RET(vm_proc(0, 13, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b76b(Emu *e, X86 *c) { E_RET(vm_proc(0, 14, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b783(Emu *e, X86 *c) { E_RET(vm_proc(0, 15, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6368b79b(Emu *e, X86 *c) { E_RET(vm_proc(0, 16, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_636908d3(Emu *e, X86 *c) { E_RET(vm_proc(1, 3, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_636908e9(Emu *e, X86 *c) { E_RET(vm_proc(1, 6, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63690901(Emu *e, X86 *c) { E_RET(vm_proc(1, 9, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63690917(Emu *e, X86 *c) { E_RET(vm_proc(1, 10, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6369092d(Emu *e, X86 *c) { E_RET(vm_proc(1, 11, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63690945(Emu *e, X86 *c) { E_RET(vm_proc(1, 12, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6369095b(Emu *e, X86 *c) { E_RET(vm_proc(1, 13, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63690973(Emu *e, X86 *c) { E_RET(vm_proc(1, 14, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6369098b(Emu *e, X86 *c) { E_RET(vm_proc(1, 15, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_636909a1(Emu *e, X86 *c) { E_RET(vm_proc(1, 16, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_636939b0(Emu *e, X86 *c) { E_RET(vm_proc(2, 3, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693984(Emu *e, X86 *c) { E_RET(vm_proc(2, 4, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693b64(Emu *e, X86 *c) { E_RET(vm_proc(2, 5, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693b7a(Emu *e, X86 *c) { E_RET(vm_proc(2, 6, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693b90(Emu *e, X86 *c) { E_RET(vm_proc(2, 7, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693ba6(Emu *e, X86 *c) { E_RET(vm_proc(2, 8, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693bbc(Emu *e, X86 *c) { E_RET(vm_proc(2, 9, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693bd2(Emu *e, X86 *c) { E_RET(vm_proc(2, 10, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693be8(Emu *e, X86 *c) { E_RET(vm_proc(2, 11, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693bfe(Emu *e, X86 *c) { E_RET(vm_proc(2, 12, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693c14(Emu *e, X86 *c) { E_RET(vm_proc(2, 13, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693c2a(Emu *e, X86 *c) { E_RET(vm_proc(2, 14, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693c40(Emu *e, X86 *c) { E_RET(vm_proc(2, 15, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693c56(Emu *e, X86 *c) { E_RET(vm_proc(2, 16, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693c6c(Emu *e, X86 *c) { E_RET(vm_proc(2, 17, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693c82(Emu *e, X86 *c) { E_RET(vm_proc(2, 18, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693c98(Emu *e, X86 *c) { E_RET(vm_proc(2, 19, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693cae(Emu *e, X86 *c) { E_RET(vm_proc(2, 20, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_636938b9(Emu *e, X86 *c) { E_RET(vm_proc(2, 23, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693cc4(Emu *e, X86 *c) { E_RET(vm_proc(2, 24, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693cdc(Emu *e, X86 *c) { E_RET(vm_proc(2, 28, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693cf2(Emu *e, X86 *c) { E_RET(vm_proc(2, 29, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693925(Emu *e, X86 *c) { E_RET(vm_proc(2, 31, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693d08(Emu *e, X86 *c) { E_RET(vm_proc(2, 32, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693d1e(Emu *e, X86 *c) { E_RET(vm_proc(2, 33, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693d34(Emu *e, X86 *c) { E_RET(vm_proc(2, 37, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693d4a(Emu *e, X86 *c) { E_RET(vm_proc(2, 38, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693d60(Emu *e, X86 *c) { E_RET(vm_proc(2, 39, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693d76(Emu *e, X86 *c) { E_RET(vm_proc(2, 40, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6369397f(Emu *e, X86 *c) { E_RET(vm_proc(2, 41, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693d8c(Emu *e, X86 *c) { E_RET(vm_proc(2, 43, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693da2(Emu *e, X86 *c) { E_RET(vm_proc(2, 44, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693dba(Emu *e, X86 *c) { E_RET(vm_proc(2, 45, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693dd2(Emu *e, X86 *c) { E_RET(vm_proc(2, 46, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693de8(Emu *e, X86 *c) { E_RET(vm_proc(2, 47, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693dfe(Emu *e, X86 *c) { E_RET(vm_proc(2, 48, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693e16(Emu *e, X86 *c) { E_RET(vm_proc(2, 49, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693e2e(Emu *e, X86 *c) { E_RET(vm_proc(2, 50, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693e44(Emu *e, X86 *c) { E_RET(vm_proc(2, 51, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_636938ef(Emu *e, X86 *c) { E_RET(vm_proc(2, 52, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693e5a(Emu *e, X86 *c) { E_RET(vm_proc(2, 55, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693e70(Emu *e, X86 *c) { E_RET(vm_proc(2, 56, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693e86(Emu *e, X86 *c) { E_RET(vm_proc(2, 57, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693e9c(Emu *e, X86 *c) { E_RET(vm_proc(2, 58, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693eb4(Emu *e, X86 *c) { E_RET(vm_proc(2, 59, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693ecc(Emu *e, X86 *c) { E_RET(vm_proc(2, 60, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693ee2(Emu *e, X86 *c) { E_RET(vm_proc(2, 61, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_lex_pack(Emu *e, X86 *c) { E_RET(lex_pack((uint8_t)A(0), (uint16_t)A(1))); }
void hk_lex_field18(Emu *e, X86 *c) { E_RET(lex_field18((uint16_t)A(0))); }
#define LEXF(addr, table, n) void hk_lex_field_##addr(Emu *e, X86 *c) { E_RET(lex_field(table, (uint16_t)A(0), n)); }
LEXF(6369a87e, 0x6371a748, 12)
LEXF(636992ad, 0x636d33f8, 12)
LEXF(63698dcf, 0x636d1bd8, 12)
LEXF(636988f7, 0x636cffe0, 11)
LEXF(6369844e, 0x636cefd0, 11)
LEXF(63697f78, 0x636cdba8, 9)
LEXF(63697ad1, 0x636cd1d0, 13)
LEXF(6369971c, 0x636d4078, 10)
void hk_vm_proc_6369a4ea(Emu *e, X86 *c) { E_RET(vm_proc(2, 21, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_636993f8(Emu *e, X86 *c) { E_RET(vm_proc(2, 25, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63698f1a(Emu *e, X86 *c) { E_RET(vm_proc(2, 26, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63698a3e(Emu *e, X86 *c) { E_RET(vm_proc(2, 27, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63698594(Emu *e, X86 *c) { E_RET(vm_proc(2, 34, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_636980bf(Emu *e, X86 *c) { E_RET(vm_proc(2, 35, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63697c17(Emu *e, X86 *c) { E_RET(vm_proc(2, 36, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6369773d(Emu *e, X86 *c) { E_RET(vm_proc(2, 53, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_636972c7(Emu *e, X86 *c) { E_RET(vm_proc(2, 54, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_lex_bsearch8w_0(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & 0xffff0000u) | (uint16_t)lex_bsearch8w(0x636cf1e0, A(0), (uint8_t)A(1), (uint8_t)A(2), (uint8_t)A(3))); }
void hk_lex_bsearch8w_1(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & 0xffff0000u) | (uint16_t)lex_bsearch8w(0x636d2db0, A(0), (uint8_t)A(1), (uint8_t)A(2), (uint8_t)A(3))); }
void hk_lex_bsearch8w_2(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & 0xffff0000u) | (uint16_t)lex_bsearch8w(0x636d01a0, A(0), (uint8_t)A(1), (uint8_t)A(2), (uint8_t)A(3))); }
void hk_lex_bsearch16(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & 0xffff0000u) | (uint16_t)lex_bsearch16(0x636c2a30, A(0), (uint16_t)A(1), (uint16_t)A(2), (uint8_t)A(3))); }
void hk_tapes_printable(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint32_t)tapes_printable((int16_t)A(0), (int16_t)A(1))); }
void hk_vm_proc_6369986b(Emu *e, X86 *c) { E_RET(vm_proc(2, 22, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_lex_trie_lookup(Emu *e, X86 *c) { E_RET((uint32_t)lex_trie_lookup((uint16_t)A(0), (uint16_t)A(1))); }
void hk_lex_trie_step(Emu *e, X86 *c) { E_RET(lex_trie_step(A(0), (uint8_t)A(1), GPH(uint32_t, A(2), 4), GPH(uint32_t, A(3), 4))); }
void hk_lex_skip_node(Emu *e, X86 *c) { lex_skip_node(); E_RET(c->s.r[EAX]); }
void hk_lex_huff_branches(Emu *e, X86 *c) { E_RET(lex_huff_branches()); }
void hk_lex_huff_letter(Emu *e, X86 *c) { E_RET(lex_huff_letter()); }
void hk_lex_huff_step(Emu *e, X86 *c) { E_RET(lex_huff_step()); }
void hk_lex_huff_size(Emu *e, X86 *c) { E_RET(lex_huff_size()); }
void hk_lex_huff_value(Emu *e, X86 *c) { E_RET(lex_huff_value()); }
void hk_lex_raw19(Emu *e, X86 *c) { E_RET(lex_raw19()); }
void hk_vm_proc_6368b383(Emu *e, X86 *c) { E_RET(vm_proc(0, 11, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6369048f(Emu *e, X86 *c) { E_RET(vm_proc(1, 4, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63690633(Emu *e, X86 *c) { E_RET(vm_proc(1, 5, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63690215(Emu *e, X86 *c) { E_RET(vm_proc(1, 7, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6369024b(Emu *e, X86 *c) { E_RET(vm_proc(1, 8, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_6369382c(Emu *e, X86 *c) { E_RET(vm_proc(2, 30, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_vm_proc_63693a03(Emu *e, X86 *c) { E_RET(vm_proc(2, 42, GPH(int16_t, A(0), 2), GPH(int16_t, A(1), 2), c->s.r[EAX])); }
void hk_tag_number(Emu *e, X86 *c) { tag_number((int16_t)A(0), (int16_t)A(1), (int16_t)A(2)); E_RET(c->s.r[EAX]); }
void hk_tag_pair(Emu *e, X86 *c) { tag_pair((int16_t)A(0), (int16_t)A(1), (int16_t)A(2)); E_RET(c->s.r[EAX]); }
void hk_count_words(Emu *e, X86 *c) { E_RET((uint32_t)count_words((int16_t)A(0), (int16_t)A(1))); }
void hk_word_stop_weight(Emu *e, X86 *c) { word_stop_weight(GPH(uint8_t, A(0), 0xb600), (int16_t)A(1), (int16_t)A(2)); E_RET(c->s.r[EAX]); }
void hk_word_cluster_find(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & 0xffff0000u) | (uint16_t)word_cluster_find((int16_t)A(0), (int16_t)A(1), (int16_t)A(2), (int16_t)A(3))); }
void hk_word_cluster(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & 0xffff0000u) | (uint16_t)word_cluster(GPH(int16_t, A(0), 2), (int16_t)A(1))); }
void hk_word_durations(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | word_durations(GPH(uint8_t, A(0), 0xb600))); }
void hk_ev_alloc(Emu *e, X86 *c) { EvNode *n = ev_alloc(); E_RET(n ? (uint32_t)((uint8_t *)n - decomp_guest_mem) : 0); }
void hk_ev_clear(Emu *e, X86 *c) { ev_clear(GPH(uint8_t, A(0), 0xb600)); E_RET(c->s.r[EAX]); }
void hk_ev_place(Emu *e, X86 *c) { E_RET((uint32_t)ev_place(GPH(uint8_t, A(0), 0xb600))); }
void hk_ph_near_dash(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | ph_near_dash(GPH(uint8_t, A(0) - 0x10, 0x10) + 0x10, AI(1), AI(2), AI(3))); }
void hk_ph_stress_marks(Emu *e, X86 *c) { ph_stress_marks(GPH(uint8_t, A(0), 1), GPH(uint8_t, A(1), 1), (int16_t)A(2), (int16_t)A(3), (uint8_t)A(4)); E_RET(c->s.r[EAX]); }
void hk_ph_marks(Emu *e, X86 *c) { ph_marks((int16_t)A(0), (int16_t)A(1), (uint8_t)A(2)); E_RET(c->s.r[EAX]); }
void hk_ph_vowel_factor(Emu *e, X86 *c) { int32_t o; uint8_t r = ph_vowel_factor(AI(0), &o); *GPH(int32_t, A(1), 4) = o; E_RET((c->s.r[EAX] & ~0xffu) | r); }
void hk_ph_final_factor(Emu *e, X86 *c) { int32_t o; ph_final_factor(AI(0), &o); *GPH(int32_t, A(1), 4) = o; E_RET(c->s.r[EAX]); }
void hk_ph_cluster_factor(Emu *e, X86 *c) { int32_t o = *GPH(int32_t, A(1), 4); uint8_t r = ph_cluster_factor(AI(0), &o); *GPH(int32_t, A(1), 4) = o; E_RET((c->s.r[EAX] & ~0xffu) | r); }
void hk_ph_split(Emu *e, X86 *c) { ph_split(AI(0)); E_RET(c->s.r[EAX]); }
void hk_ph_durations(Emu *e, X86 *c) { ph_durations((int16_t)A(0), (int16_t)A(1), (uint8_t)A(2)); E_RET(c->s.r[EAX]); }
void hk_ph_segments(Emu *e, X86 *c) { ph_segments((int16_t)A(0), (int16_t)A(1), (uint8_t)A(2)); E_RET(c->s.r[EAX]); }
void hk_ph_target(Emu *e, X86 *c) { ph_target(AI(0), AI(1)); E_RET(c->s.r[EAX]); }
void hk_ph_insert_tag(Emu *e, X86 *c) { ph_insert_tag(AI(0), AI(1)); E_RET(c->s.r[EAX]); }
void hk_ph_pitch(Emu *e, X86 *c) { ph_pitch(GPH(uint8_t, A(0), 0xb600), AI(1), AI(2)); E_RET(c->s.r[EAX]); }
void hk_ph_run(Emu *e, X86 *c) { E_RET((uint32_t)ph_run(GPH(uint8_t, A(0), 0xb600))); }
void hk_fe_c_run(Emu *e, X86 *c) { E_RET((uint32_t)fe_c_run(GPH(uint8_t, A(0), 0xb600))); }
void hk_fe_b_run(Emu *e, X86 *c) { E_RET((uint32_t)fe_b_run(GPH(uint8_t, A(0), 0xb600))); }
void hk_fe_prepare(Emu *e, X86 *c) { E_RET((uint32_t)fe_prepare(GPH(uint8_t, A(0), 0xb600))); }
void hk_fe_word_prepare(Emu *e, X86 *c) { E_RET((uint32_t)fe_word_prepare(AI(0))); }
void hk_fe_word_format(Emu *e, X86 *c) { E_RET((uint32_t)fe_word_format(AI(0), GPH(uint32_t, A(1), 4))); }
void hk_fe_event_count(Emu *e, X86 *c) { E_RET((uint32_t)fe_event_count(A(0) ? GPH(uint8_t, A(0), 0xb600) : NULL)); }
void hk_fe_word_event_count(Emu *e, X86 *c) { E_RET((uint32_t)fe_word_event_count(AI(0))); }
void hk_fe_events(Emu *e, X86 *c) { E_RET((uint32_t)fe_events(A(0) ? GPH(uint8_t, A(0), 0xb600) : NULL, GPH(int32_t, A(1), 4))); }
void hk_fe_word_events(Emu *e, X86 *c) { E_RET((uint32_t)fe_word_events(AI(0), GPH(int32_t, A(1), 4))); }
#define GSTR(a) GPH(char, a, 1)
void hk_txt_is_alpha(Emu *e, X86 *c) { E_RET((uint32_t)txt_is_alpha((char)A(0))); }
void hk_txt_approx(Emu *e, X86 *c) { E_RET((uint32_t)txt_approx((char)A(0), GSTR(A(1)))); }
void hk_txt_split(Emu *e, X86 *c) {
    char *r = GP(char, *GPH(uint32_t, A(3), 4));
    uint32_t v = (uint32_t)txt_split(GSTR(A(0)), GSTR(A(1)), GSTR(A(2)), &r);
    *GPH(uint32_t, A(3), 4) = (uint32_t)(r - (char *)decomp_guest_mem);
    E_RET(v);
}
void hk_txt_email_part(Emu *e, X86 *c) { txt_email_part(GSTR(A(0)), GSTR(A(1)), GSTR(A(2))); E_RET(c->s.r[EAX]); }
void hk_txt_emoticon(Emu *e, X86 *c) { E_RET((uint32_t)txt_emoticon(GPH(uint8_t, A(0), 0xc9))); }
void hk_txt_tilde(Emu *e, X86 *c) { E_RET((uint32_t)txt_tilde(GPH(uint8_t, A(0), 0xc9))); }
void hk_txt_normalize(Emu *e, X86 *c) { E_RET((uint32_t)txt_normalize(GPH(uint8_t, A(0), 0xc9))); }
void hk_txt_email(Emu *e, X86 *c) { E_RET((uint32_t)txt_email(GPH(uint8_t, A(0), 0xc9))); }
void hk_txt_thousands(Emu *e, X86 *c) { E_RET((uint32_t)txt_thousands(GPH(uint8_t, A(0), 0xc9))); }
void hk_txt_plus(Emu *e, X86 *c) { E_RET((uint32_t)txt_plus(GPH(uint8_t, A(0), 0xc9))); }
void hk_txt_tags(Emu *e, X86 *c) {
    char *p = GP(char, *GPH(uint32_t, A(0), 4));
    uint32_t v = (uint32_t)txt_tags(&p, GPH(uint8_t, A(1), 0xc9), AI(2));
    *GPH(uint32_t, A(0), 4) = (uint32_t)(p - (char *)decomp_guest_mem);
    E_RET(v);
}
void hk_txt_words(Emu *e, X86 *c) {
    char *p = GP(char, *GPH(uint32_t, A(0), 4));
    uint32_t v = (uint32_t)txt_words(&p, GPH(uint8_t, A(1), 0xc9), AI(2));
    *GPH(uint32_t, A(0), 4) = (uint32_t)(p - (char *)decomp_guest_mem);
    E_RET(v);
}
void hk_txt_rules(Emu *e, X86 *c) { E_RET((uint32_t)txt_rules(GPH(uint8_t, A(0), 0xc9), AI(1), AI(2))); }
void hk_txt_rewrite(Emu *e, X86 *c) { E_RET((uint32_t)txt_rewrite(GSTR(A(0)), GPH(uint32_t, A(1), 4), AI(2))); }
void hk_lex_find(Emu *e, X86 *c) { E_RET((uint32_t)lex_find(GPH(char, A(0), 1), A(1), GPH(uint32_t, A(2), 4), AI(3))); }
void hk_lex_is_dash(Emu *e, X86 *c) { E_RET((uint32_t)lex_is_dash((char)A(0))); }
void hk_lex_abbrev(Emu *e, X86 *c) { E_RET((uint32_t)lex_abbrev(GPH(char, A(0), 1), A(1))); }
void hk_lex_title(Emu *e, X86 *c) { E_RET((uint32_t)lex_title(GPH(char, A(0), 1), A(1), AI(2))); }
void hk_lex_common(Emu *e, X86 *c) { E_RET((uint32_t)lex_common(GPH(char, A(0), 1), A(1))); }
void hk_lex_emoticon(Emu *e, X86 *c) { E_RET((uint32_t)lex_emoticon(GPH(char, A(0), 1), AI(1), GPH(int32_t, A(2), 4))); }
void hk_lex_next(Emu *e, X86 *c) { uint32_t self = c->s.r[ECX]; lex_next(GPH(Lexer, self, sizeof(Lexer)), GPH(TextSpan, A(0), sizeof(TextSpan))); E_RET(self + 0x10); }
void hk_lex_init(Emu *e, X86 *c) { uint32_t self = c->s.r[ECX]; lex_init(GPH(Lexer, self, sizeof(Lexer)), A(0), AI(1)); E_RET(self + 0x10); }
void hk_lex_classes(Emu *e, X86 *c) { lex_classes(); E_RET(c->s.r[EAX]); }

// ------------------------------------------------------------------ sentence splitter (fe_split.c)
#undef SPAN
#define SPAN(a) GPH(TextSpan, (a), sizeof(TextSpan))
void hk_quotes_track(Emu *e, X86 *c) { quotes_track(THIS(Quotes), SPAN(A(0)), AI(1), AI(2), AI(3)); }
void hk_quotes_open(Emu *e, X86 *c) { E_RET((uint32_t)quotes_open(THIS(Quotes))); }
void hk_quotes_apostrophe(Emu *e, X86 *c) { E_RET((uint32_t)quotes_apostrophe(THIS(Quotes), SPAN(A(0)))); }
void hk_quotes_clamp(Emu *e, X86 *c) { quotes_clamp(GPH(Quotes, A(1), sizeof(Quotes))); }
void hk_brackets_update(Emu *e, X86 *c) {
    // (a 51st entry writes one dword past the object, as the original does)
    brackets_update(GPH(Brackets, c->s.r[ECX], sizeof(Brackets) + 4), SPAN(A(0)), SPAN(A(1)), GPH(char, A(2), 1));
    E_RET(0);
}
void hk_brackets_peek(Emu *e, X86 *c) { E_RET((uint32_t)brackets_peek(GPH(Brackets, c->s.r[ECX], sizeof(Brackets) + 4), GPH(int32_t, A(0), 4))); }
void hk_brackets_classify(Emu *e, X86 *c) { brackets_classify(SPAN(A(0)), GPH(int32_t, A(1), 4), GPH(int32_t, A(2), 4)); }
void hk_span_set_kind(Emu *e, X86 *c) { span_set_kind(THIS(TextSpan), AI(0)); E_RET(c->s.r[ECX]); }
void hk_span_differs(Emu *e, X86 *c) { E_RET((uint32_t)span_differs(THIS(TextSpan), SPAN(A(0)))); }
void hk_span_before_last(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | (uint8_t)span_before_last(THIS(TextSpan))); }
void hk_word_not_roman(Emu *e, X86 *c) { E_RET((uint32_t)word_not_roman(SPAN(A(0)))); }
void hk_word_initials(Emu *e, X86 *c) { E_RET((uint32_t)word_initials(SPAN(A(0)))); }
void hk_word_is_number(Emu *e, X86 *c) { E_RET((uint32_t)word_is_number(*SPAN(c->s.r[ESP] + 4))); }
void hk_word_number_name(Emu *e, X86 *c) { E_RET((uint32_t)word_number_name(GPH(char, A(0), 1), A(1))); }
void hk_word_equals(Emu *e, X86 *c) { E_RET((uint32_t)word_equals(GSTR(A(0)), GPH(char, A(1), 1), AI(2))); }
void hk_split_sentences(Emu *e, X86 *c) {
    int32_t max = AI(2);
    E_RET((uint32_t)split_sentences(AI(0), GPH(SplitText, A(1), sizeof(SplitText)), max,
                                    GPH(Sentence, A(3), sizeof(Sentence) * (uint32_t)(max > 0 ? max : 1)),
                                    GPH(int32_t, A(4), 4), GPH(int32_t, A(5), 4)));
}
void hk_text_sentences(Emu *e, X86 *c) { E_RET(host_to_guest(text_sentences(GSTR(A(0)), AI(1)))); }
void hk_text_prepare(Emu *e, X86 *c) { E_RET(host_to_guest(text_prepare(GSTR(A(0)), AI(1)))); }

// ------------------------------------------------------------------ text input (fe_input.c)
void hk_fe_entries_clear(Emu *e, X86 *c) { fe_entries_clear(THIS(FrontEnd)); E_RET(0); }
void hk_fe_entry_add_num(Emu *e, X86 *c) { E_RET((uint32_t)fe_entry_add_num(THIS(FrontEnd), AI(0), A(1))); }
void hk_fe_entry_add_str(Emu *e, X86 *c) { E_RET((uint32_t)fe_entry_add_str(THIS(FrontEnd), AI(0), GSTR(A(1)))); }
void hk_tag_mode_name(Emu *e, X86 *c) { E_RET(tag_mode_name(AI(0))); }
void hk_tag_mode_value(Emu *e, X86 *c) { E_RET((uint32_t)tag_mode_value(GSTR(A(0)))); }
void hk_tag_skip(Emu *e, X86 *c) { tag_skip(GPH(uint32_t, A(0), 4)); E_RET(A(0)); }
void hk_str_append(Emu *e, X86 *c) {
    uint32_t *pp = GPH(uint32_t, A(0), 4);
    char *cur = GP(char, *pp);
    str_append(&cur, GSTR(A(1)));
    *pp = host_to_guest(cur);
    E_RET(1);
}
void hk_fe_tags_to_escapes(Emu *e, X86 *c) {
    E_RET((uint32_t)fe_tags_to_escapes(THIS(FrontEnd), AI(0), AI(1), GSTR(A(2)), GPH(uint32_t, A(3), 4)));
}
void hk_fe_set_text(Emu *e, X86 *c) { E_RET((uint32_t)fe_set_text(THIS(FrontEnd), GSTR(A(0)))); }
void hk_fe_text_input(Emu *e, X86 *c) {
    if (trace_file()) fprintf(trace_file(), "text tags %d mode %d\n", AI(2), AI(3));
    E_RET((uint32_t)fe_text_input(THIS(FrontEnd), GPH(uint16_t, A(0), 2), GPH(int32_t, A(1), 4), AI(2), AI(3)));
}
void hk_phonemes_to_names(Emu *e, X86 *c) { E_RET((uint32_t)phonemes_to_names(GPH(uint16_t, A(0), 2), GPH(uint32_t, A(1), 4), AI(2))); }
void hk_phoneme_index(Emu *e, X86 *c) { E_RET((uint32_t)phoneme_index((uint16_t)A(0))); }
void hk_free_if2(Emu *e, X86 *c) { E_RET((uint32_t)free_if2(A(0) ? (void *)x86_ptr(c, A(0), 1) : NULL)); }

// ------------------------------------------------------------------ the word module's reader (fe_reader.c)
#define WOBJ(a) GPH(uint8_t, (a), 0xb600)
void hk_ev_add_text(Emu *e, X86 *c) { E_RET((uint32_t)ev_add_text(WOBJ(A(0)), AI(1), AI(2))); }
void hk_ev_add_word(Emu *e, X86 *c) { E_RET((uint32_t)ev_add_word(WOBJ(A(0)), AI(1), AI(2))); }
void hk_ev_add_index(Emu *e, X86 *c) { E_RET((uint32_t)ev_add_index(WOBJ(A(0)), AI(1), AI(2), AI(3))); }
void hk_ev_add_z(Emu *e, X86 *c) { E_RET((uint32_t)ev_add_z(WOBJ(A(0)), AI(1), AI(2), AI(3))); }
void hk_ev_add_value(Emu *e, X86 *c) { E_RET((uint32_t)ev_add_value(WOBJ(A(0)), AI(1), (uint16_t)A(2))); }
void hk_ev_add_mark(Emu *e, X86 *c) { E_RET((uint32_t)ev_add_mark(WOBJ(A(0)), AI(1), AI(2))); }
void hk_word_reset(Emu *e, X86 *c) { E_RET((uint32_t)word_reset(A(0) ? WOBJ(A(0)) : NULL)); }
void hk_word_voice(Emu *e, X86 *c) { E_RET((uint32_t)word_voice(WOBJ(A(0)), AI(1))); }
void hk_rd_back(Emu *e, X86 *c) { E_RET(rd_back()); }
void hk_rd_init(Emu *e, X86 *c) { rd_init(GSTR(A(0)), AI(1)); }
void hk_rd_phrase(Emu *e, X86 *c) { E_RET((uint32_t)rd_phrase((uint8_t)A(0))); }
void hk_rd_close_word(Emu *e, X86 *c) { E_RET((uint32_t)rd_close_word(AI(0), GPH(int32_t, A(1), 4))); }
void hk_word_run(Emu *e, X86 *c) {
    E_RET((uint32_t)word_run(A(0) ? WOBJ(A(0)) : NULL, GSTR(A(1)), GPH(int32_t, A(2), 4), AI(3)));
}
void hk_word_dispatch(Emu *e, X86 *c) { E_RET((uint32_t)word_dispatch(AI(0), GSTR(A(1)), GPH(int32_t, A(2), 4), AI(3))); }
void hk_words_feed(Emu *e, X86 *c) {
    E_RET((uint32_t)words_feed(AI(0), GSTR(A(1)), GPH(int32_t, A(2), 4), GPH(int32_t, A(3), 4)));
}
void hk_phone_code(Emu *e, X86 *c) { E_RET((c->s.r[EAX] & ~0xffu) | phone_code(GSTR(A(0)))); }
void hk_phones_to_codes(Emu *e, X86 *c) { E_RET(host_to_guest(phones_to_codes(GSTR(A(0))))); }
void hk_lex_entry_cmp(Emu *e, X86 *c) { E_RET((uint32_t)lex_entry_cmp(GPH(uint32_t, A(0), 4), GPH(uint32_t, A(1), 4))); }
void hk_user_lex_find(Emu *e, X86 *c) { E_RET(user_lex_find(GSTR(A(0)), GPH(UserLex, A(1), sizeof(UserLex)))); }
void hk_user_lex_word(Emu *e, X86 *c) { E_RET((uint32_t)user_lex_word(GSTR(A(0)), GPH(uint32_t, A(1), 4))); }
void hk_words_continue(Emu *e, X86 *c) { E_RET((uint32_t)words_continue(AI(0), GPH(int32_t, A(1), 4), GPH(int32_t, A(2), 4))); }
void hk_words_set_pitch(Emu *e, X86 *c) { words_set_pitch(AI(0), AI(1)); }
void hk_fe_after_feed(Emu *e, X86 *c) { E_RET((uint32_t)fe_after_feed(THIS(FrontEnd), AI(0))); }
void hk_fe_continue(Emu *e, X86 *c) { E_RET((uint32_t)fe_continue(THIS(FrontEnd))); }
void hk_tag_char_value(Emu *e, X86 *c) { E_RET((uint32_t)tag_char_value(GSTR(A(0)))); }
void hk_fe_voice_flags(Emu *e, X86 *c) { fe_voice_flags(THIS(FrontEnd), AI(0)); }
void hk_fe_text_end(Emu *e, X86 *c) { E_RET((uint32_t)fe_text_end(THIS(FrontEnd))); }
void hk_voice_rec(Emu *e, X86 *c) { E_RET(host_to_guest(voice_rec(GPH(uint8_t, A(0), 0xb20), A(1), GPH(int32_t, A(2), 4), GPH(int32_t, A(3), 4)))); }
void hk_voice_rec_count(Emu *e, X86 *c) { E_RET(voice_rec_count(GPH(uint8_t, A(0), 0xb1c))); }

// ------------------------------------------------------------------ phrase output (fe_phones.c)
#define PROS(a) GPH(Prosody, (a), sizeof(Prosody))
void hk_tapes_copy(Emu *e, X86 *c) {
    tapes_copy(AI(0), GPH(uint32_t, A(1), 4), GPH(uint32_t, A(2), 4), GPH(uint32_t, A(3), 4), GPH(uint32_t, A(4), 4), GPH(uint32_t, A(5), 4));
}
void hk_phrase_init(Emu *e, X86 *c) {
    E_RET((uint32_t)phrase_init(PROS(A(0)), GSTR(A(1)), GSTR(A(2)), GSTR(A(3)), GSTR(A(4)), GSTR(A(5)), GSTR(A(6)),
                                GSTR(A(7)), GSTR(A(8)), GSTR(A(9)), AI(10), GPH(uint8_t, A(11), 1), GPH(int32_t, A(12), 8)));
}
void hk_phrase_parse(Emu *e, X86 *c) { E_RET((uint32_t)phrase_parse(GSTR(A(0)), GPH(uint32_t, A(1), 4), GPH(uint32_t, A(2), 4), PROS(A(3)))); }
void hk_pitch_value(Emu *e, X86 *c) { x86_fpu_push(c, pitch_value(AI(0), AI(1), argd2(c, 2))); }
void hk_phone_name(Emu *e, X86 *c) { E_RET(phone_name((uint8_t)A(0))); }
void hk_name_is_vowel(Emu *e, X86 *c) { E_RET((uint32_t)name_is_vowel(GSTR(A(0)))); }
void hk_code_is_vowel(Emu *e, X86 *c) { E_RET((uint32_t)code_is_vowel((char)A(0))); }
void hk_phone_list(Emu *e, X86 *c) {
    E_RET((uint32_t)phone_list(AI(0), GSTR(A(1)), GSTR(A(2)), A(3) ? GPH(float, A(3), 4) : NULL,
                               A(4) ? GPH(PhoneIn, A(4), sizeof(PhoneIn)) : NULL, AI(5), AI(6), GSTR(A(7)), GSTR(A(8))));
}
void hk_words_phrase(Emu *e, X86 *c) { E_RET((uint32_t)words_phrase(AI(0))); }
void hk_phrase_free(Emu *e, X86 *c) { phrase_free(PROS(A(0)), AI(1)); }
void hk_phones_build(Emu *e, X86 *c) { E_RET((uint32_t)phones_build(AI(0), A(1) ? GPH(PhoneIn, A(1), sizeof(PhoneIn)) : NULL, AI(2), AI(3))); }
void hk_words_abort(Emu *e, X86 *c) { words_abort(AI(0)); }
void hk_str_dup0(Emu *e, X86 *c) { E_RET(host_to_guest(str_dup0(GSTR(A(0))))); }
void hk_calloc_n(Emu *e, X86 *c) { E_RET(host_to_guest(calloc_n(A(0), A(1)))); }
void hk_fe_set_pitch_level(Emu *e, X86 *c) { E_RET((uint32_t)fe_set_pitch_level(THIS(FrontEnd), (uint16_t)A(0))); }
void hk_fe_phrase_out(Emu *e, X86 *c) { E_RET((uint32_t)fe_phrase_out(THIS(FrontEnd), AI(0))); }
void hk_fe_thread(Emu *e, X86 *c) { E_RET((uint32_t)fe_thread(THIS(FrontEnd))); }

// ------------------------------------------------------------------ front end set-up (fe_init.c)
void hk_text_init(Emu *e, X86 *c) { E_RET((uint32_t)text_init(A(0), A(1) ? GSTR(A(1)) : NULL, A(2))); }
void hk_fe_once(Emu *e, X86 *c) { fe_once(); }
void hk_ev_list_init(Emu *e, X86 *c) { ev_list_init(WOBJ(A(0))); }
void hk_word_obj_init(Emu *e, X86 *c) { E_RET((uint32_t)word_obj_init(WOBJ(A(0)))); }
void hk_word_obj_create(Emu *e, X86 *c) { E_RET((uint32_t)word_obj_create()); }
void hk_word_set_mask(Emu *e, X86 *c) { E_RET((uint32_t)word_set_mask(A(0) ? WOBJ(A(0)) : NULL, (uint8_t)A(1))); }
void hk_word_set_events(Emu *e, X86 *c) { E_RET((uint32_t)word_set_events(AI(0), AI(1))); }
void hk_word_set_mode(Emu *e, X86 *c) { E_RET((uint32_t)word_set_mode(AI(0), AI(1))); }
void hk_word_set_rate(Emu *e, X86 *c) { E_RET((uint32_t)word_set_rate(AI(0), AI(1))); }
void hk_user_lex_builtin(Emu *e, X86 *c) { user_lex_builtin(GPH(uint32_t, A(0), 4)); }
void hk_user_lex_file(Emu *e, X86 *c) { user_lex_file(GPH(uint32_t, A(0), 4)); }
void hk_at_end(Emu *e, X86 *c) { E_RET((uint32_t)at_end(GSTR(A(0)))); }
void hk_read_line(Emu *e, X86 *c) {
    uint32_t *pp = GPH(uint32_t, A(2), 4);
    char *p = GP(char, *pp);
    read_line(GSTR(A(0)), AI(1), &p);
    *pp = host_to_guest(p);
}
void hk_words_set_f624(Emu *e, X86 *c) { words_set_f624(AI(0), AI(1)); }
void hk_words_open(Emu *e, X86 *c) { E_RET((uint32_t)words_open(AI(0), A(1) ? GSTR(A(1)) : NULL)); }
void hk_fe_entries_init(Emu *e, X86 *c) { fe_entries_init(THIS(FrontEnd)); E_RET(0); }
void hk_fe_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(fe_ctor(THIS(FrontEnd)))); }
void hk_fe_setup(Emu *e, X86 *c) {
    FrontEnd *fe = THIS(FrontEnd);
    E_RET((uint32_t)fe_setup(fe, GPH(uint8_t, A(0), 0x40), GPH(uint8_t, A(1), 0x40), GPH(uint8_t, A(2), 0x8da),
                             AI(3), AI(4), AI(5), A(6), A(7), AI(8)));
    if (trace_file())
        fprintf(trace_file(), "setup pitch %u f50 %d flags %x\n", *GPH(uint16_t, A(2) + 0x8d8, 2), AI(4), fe->flags);
}

// ------------------------------------------------------------------ voice loading (containers.c, vload.c)
void hk_next_prime(Emu *e, X86 *c) { E_RET((uint32_t)next_prime(AI(0))); }
void hk_hash_new(Emu *e, X86 *c) { E_RET(host_to_guest(hash_new(AI(0)))); }
void hk_hash_add(Emu *e, X86 *c) { E_RET((uint32_t)hash_add(A(0) ? GPH(HashTable, A(0), sizeof(HashTable)) : NULL, A(1) ? GSTR(A(1)) : NULL, AI(2))); }
void hk_hash_add_grow(Emu *e, X86 *c) { E_RET((uint32_t)hash_add_grow(A(0) ? GPH(HashTable, A(0), sizeof(HashTable)) : NULL, A(1) ? GSTR(A(1)) : NULL, AI(2))); }
void hk_phoneset_new(Emu *e, X86 *c) { E_RET(host_to_guest(phoneset_new(AI(0)))); }
void hk_phoneset_set(Emu *e, X86 *c) { phoneset_set(GPH(NamedTable, A(0), sizeof(NamedTable)), GSTR(A(1)), AI(2), AI(3), AI(4), AI(5)); }
void hk_phoneset_kind(Emu *e, X86 *c) { E_RET((uint32_t)phoneset_kind(GPH(NamedTable, A(0), sizeof(NamedTable)), AI(1))); }
void hk_phoneset_nparts(Emu *e, X86 *c) { E_RET((uint32_t)phoneset_nparts(GPH(NamedTable, A(0), sizeof(NamedTable)), AI(1))); }
void hk_phone_spec_split(Emu *e, X86 *c) { E_RET((uint32_t)phone_spec_split(GSTR(A(0)), GSTR(A(1)), GSTR(A(2)), GSTR(A(3)), GSTR(A(4)))); }
void hk_phoneset_load(Emu *e, X86 *c) { E_RET((uint32_t)phoneset_load(GPH(uint8_t, A(0), 4), GSTR(A(1)), GPH(uint32_t, A(2), 4))); }
void hk_senone_header_load(Emu *e, X86 *c) { E_RET((uint32_t)senone_header_load(GPH(uint8_t, A(0), 4), GPH(SenoneTree, A(1), sizeof(SenoneTree)), GPH(uint32_t, A(2), 4))); }
void hk_senone_tree_load(Emu *e, X86 *c) {
    E_RET((uint32_t)senone_tree_load(GPH(uint8_t, A(0), 4), GSTR(A(1)), GPH(PhoneSet, A(2), sizeof(PhoneSet)), GPH(uint32_t, A(3), 4)));
}
void hk_senone_tree_load2(Emu *e, X86 *c) { hk_senone_tree_load(e, c); }
void hk_alt_arg(Emu *e, X86 *c) {
    uint32_t *pp = GPH(uint32_t, A(1), 4);
    const char *p = GP(const char, *pp);
    alt_arg(AI(0), &p, GPH(uint8_t, A(2), 1), GPH(uint32_t, A(3), 4));
    *pp = host_to_guest(p);
    E_RET(0);
}
void hk_alt_parse(Emu *e, X86 *c) { E_RET((uint32_t)alt_parse(GSTR(A(0)), AI(1), GPH(uint32_t, A(2), 4))); }
void hk_alt_units_load(Emu *e, X86 *c) { alt_units_load(GPH(uint8_t, A(0), 4), GPH(uint16_t, A(1), 2), AI(2), GPH(uint32_t, A(3), 4)); }
void hk_unit_voice_scan(Emu *e, X86 *c) { unit_voice_scan(THIS(UnitStage), AI(0), AI(1)); }
void hk_unit_init(Emu *e, X86 *c) {
    E_RET((uint32_t)unit_init(THIS(UnitStage), GPH(uint8_t, A(0), 0x40), GPH(uint8_t, A(1), 0x40), GPH(uint8_t, A(2), 4),
                              GPH(uint8_t, A(3), 0x8f8), AI(4), AI(5), A(6), A(7)));
}

void hk_effect_set_fir(Emu *e, X86 *c) { effect_set_fir(THIS(Effect), AI(0), GPH(const float, A(1), 4u * (uint32_t)AI(0))); }
void hk_voc_gauss(Emu *e, X86 *c) {
    x86_fpu_push(c, voc_gauss(GPH(int32_t, A(0), 4), GPH(float, A(1), 4), GPH(float, A(2), 4)));
}
void hk_voc_noise_make(Emu *e, X86 *c) { E_RET(host_to_guest(voc_noise_make(AI(0)))); }
void hk_voc_sine_table(Emu *e, X86 *c) { voc_sine_table(AI(0), GPH(float, A(1), 8u * A(0))); }
void hk_voc_fade_window(Emu *e, X86 *c) { E_RET(host_to_guest(voc_fade_window(AI(0)))); }
void hk_voc_header_load(Emu *e, X86 *c) { E_RET(host_to_guest(voc_header_load(GPH(uint8_t, A(0), 4)))); }
void hk_voc_codebook_load(Emu *e, X86 *c) { E_RET(host_to_guest(voc_codebook_load(GPH(uint8_t, A(0), 4)))); }
void hk_voc_inventory_load(Emu *e, X86 *c) { E_RET(host_to_guest(voc_inventory_load(GPH(uint8_t, A(0), 4)))); }
void hk_voc_setup(Emu *e, X86 *c) {
    E_RET((uint32_t)voc_setup(THIS(VocSynthState), GPH(const VocVoice, A(0), sizeof(VocVoice)), AI(1)));
}
void hk_voc_engine_init(Emu *e, X86 *c) {
    E_RET((uint32_t)voc_engine_init(THIS(VocEngine), GPH(uint8_t, A(0), 0x40), GPH(uint8_t, A(1), 0x40), AI(2), AI(3),
                                    GPH(uint8_t, A(4), 4), AI(5), A(6), A(7), AI(8), A(9), A(10)));
}
void hk_voc_init(Emu *e, X86 *c) { voc_init(THIS(VocEngine), AI(0)); }

#define ML(i) GPH(ModeList, A(i), sizeof(ModeList))
void hk_list_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(list_ctor(THIS(ModeList)))); }
void hk_list_count(Emu *e, X86 *c) { E_RET(list_count(THIS(ModeList))); }
void hk_list_grow(Emu *e, X86 *c) { E_RET((uint32_t)list_grow(THIS(ModeList), A(0))); }
void hk_list_add(Emu *e, X86 *c) { E_RET((uint32_t)list_add(THIS(ModeList), A(0) ? GPH(uint8_t, A(0), A(1) ? A(1) : 1) : NULL, A(1))); }
void hk_list_item(Emu *e, X86 *c) { E_RET(list_item(THIS(ModeList), A(0))); }
void hk_wstr_copy(Emu *e, X86 *c) { wstr_copy(GPH(uint16_t, A(0), 2), GPH(const uint16_t, A(1), 2)); }
void hk_voice_read_header(Emu *e, X86 *c) { E_RET((uint32_t)voice_read_header(GPH(const char, A(0), 1), GPH(VoiceHdr, A(1), 0x8f8))); }
void hk_mode_info(Emu *e, X86 *c) { mode_info(GPH(const VoiceHdr, A(0), 0x8f8), GPH(TtsModeInfo, A(1), 0xaf0)); }
void hk_mode_info_cfg(Emu *e, X86 *c) {
    mode_info_cfg(GPH(VoiceCfg, A(0), 0xb18), GPH(const VoiceHdr, A(1), 0x8f8), GPH(TtsModeInfo, A(2), 0xaf0));
}
void hk_cfg_check_40(Emu *e, X86 *c) { E_RET((uint32_t)cfg_check_40(A(0), A(1), NULL)); }
void hk_cfg_check_20(Emu *e, X86 *c) { E_RET((uint32_t)cfg_check_20(A(0), A(1), NULL)); }
void hk_cfg_check_10(Emu *e, X86 *c) { E_RET((uint32_t)cfg_check_10(A(0), A(1), NULL)); }
void hk_cfg_read(Emu *e, X86 *c) {
    E_RET((uint32_t)cfg_read((int16_t)A(0), (int16_t)A(1), GPH(const char, A(2), 1), GPH(VoiceCfg, A(3), 0xb18), ML(4),
                             GPH(uint32_t, A(5), 4)));
}
void hk_mode_find(Emu *e, X86 *c) {
    E_RET((uint32_t)mode_find(ML(0), GPH(const uint8_t, c->s.r[ESP] + 8, 16), GPH(uint32_t, A(5), 4)));
}
void hk_voices_enum(Emu *e, X86 *c) { E_RET((uint32_t)voices_enum(GPH(const char, A(0), 1), ML(1), ML(2), ML(3))); }
void hk_cfg_load(Emu *e, X86 *c) { E_RET(host_to_guest(cfg_load(GPH(const char, A(0), 1), AI(1)))); }
void hk_mode_table_add(Emu *e, X86 *c) {
    E_RET((uint32_t)mode_table_add(GPH(const VoiceHdr, A(0), 0x8f8), ML(1), ML(2), GPH(int32_t, A(3), 4), GPH(int32_t, A(4), 4),
                                   GPH(int32_t, A(5), 4)));
}
void hk_queue_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(queue_ctor(THIS(Queue)))); }
void hk_queue_set_name(Emu *e, X86 *c) { queue_set_name(THIS(Queue), A(0) ? GPH(const char, A(0), 1) : NULL); }
void hk_queue_set_high(Emu *e, X86 *c) { queue_set_high(THIS(Queue), A(0)); }
void hk_unit_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(unit_ctor(THIS(UnitStage)))); }
void hk_voc_state_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(voc_state_ctor(THIS(VocSynthState)))); }
void hk_empty_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(empty_ctor(GPH(uint8_t, c->s.r[ECX], 1)))); }
void hk_voc_engine_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(voc_engine_ctor(THIS(VocEngine)))); }
void hk_fe_get_pitch(Emu *e, X86 *c) { E_RET((uint32_t)fe_get_pitch(THIS(FrontEnd), GPH(uint16_t, A(0), 2))); }
void hk_unit_get_rate(Emu *e, X86 *c) { E_RET((uint32_t)unit_get_rate(THIS(UnitStage), GPH(uint32_t, A(0), 4))); }
void hk_module_dir(Emu *e, X86 *c) { E_RET((uint32_t)module_dir(GPH(char, A(0), A(1)), A(1))); }
void hk_lex_word_bad(Emu *e, X86 *c) { E_RET((uint32_t)lex_word_bad(GSTR(A(0)))); }
void hk_old_code(Emu *e, X86 *c) { E_RET(old_code(GSTR(A(0)))); }
void hk_old_codes(Emu *e, X86 *c) { E_RET((uint32_t)old_codes(GSTR(A(0)), GPH(uint32_t, A(1), 4))); }
void hk_lex_pron_convert(Emu *e, X86 *c) { E_RET((uint32_t)lex_pron_convert(GSTR(A(0)), GPH(uint32_t, A(1), 4))); }
void hk_lex_pron_names(Emu *e, X86 *c) { E_RET((uint32_t)lex_pron_names((char *)GSTR(A(0)), GPH(uint32_t, A(1), 4))); }
void hk_free_if_a(Emu *e, X86 *c) { E_RET((uint32_t)free_if_a(A(0) ? GPH(uint8_t, A(0), 1) : NULL)); }
void hk_free_if_b(Emu *e, X86 *c) { E_RET((uint32_t)free_if_b(A(0) ? GPH(uint8_t, A(0), 1) : NULL)); }
