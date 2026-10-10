#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* The tables are read from the image the caller supplies; nothing here is a
   copy of the original's data. */

#ifndef BST_NO_TRACE
int bst_trace = 0;
#endif

/* The 1995 build's table directory. Every address here was found by reading
   the original's code; nothing is inferred from another build. */
const bst_tabmap BST_MAP_1995 = {
    .chattr        = 0x10021020, .letterattr = 0x10021120,
    .casemap       = 0x10021220, .symmap     = 0x10021320,

    .phattr1       = 0x10021648, .phattr2    = 0x100216C8,
    .classtab      = 0x10021440, .exctab     = 0x10021448,
    .basedur       = 0x10021420, .coefgain   = 0x10020000,

    .tokstates     = 0x10021748, .names      = 0x1002EBD0,
    .code_lo       = 0x10001000, .code_hi    = 0x10019000,
    .tok_stride = 12,
    .tok_state_off = 0, .tok_state_w = 4,
    .tok_handler_off = 4, .tok_handler_w = 4,
    .tok_next_off = 8, .tok_next_w = 4,
    .lts_index_stride = 4,
    .rule_stride = 8, .rule_prio_w = 2,
    .trie_st_off = 4, .trie_li_off = 8, .trie_base_off = 12,
    .trie_max_off = 14, .trie_po_off = 16,
    .silence_f0 = 0xC1, .unvoiced_dur = 0x6E, .unvoiced_reps = 1,
    .voice_span = 0x19A,
    .vowel_dur_shift = 0, .trn_round = 1,

    .code_medial   = 0x10020EA0, .code_initial  = 0x10020F10,
    .ph_single     = 0x10092D68, .ph_single_max = 0x10092E14,
    .ph_pair       = 0x10092E18, .ph_pair_max   = 0x10092F50,
    .bucket_index  = {
        0x100432D0, 0x10047AE0, 0x1004C970, 0x10050090, 0x10055BA0,
        0x1005D4A0, 0x100612A8, 0x10065AC8, 0x100697D0, 0x1006E008,
        0x10074500, 0x1007C9C0, 0x100857F0, 0x10088A80, 0x10092B20,
    },
    .bucket_data   = {
        0x10037728, 0x10043570, 0x10047BE8, 0x1004CA90, 0x10050160,
        0x10055CF8, 0x1005D668, 0x10061398, 0x10065BD8, 0x100698B0,
        0x1006E108, 0x10074678, 0x1007CBC0, 0x10085A08, 0x10088B40,
    },

    .suffix_ptrs   = 0x10020E90, .lts_index = 0x1002E818,
    .suffix_n      = 3, .suffix_max = 3,
    .dispatch      = 0x10022ED0, .rules     = 0x10093000,
    .patterns      = 0x1002EFB8, .outputs   = 0x10021C80,

    .trie_desc     = 0x10030898,
    .modmap        = 0x10037638, .modtab    = 0x10037668,

    .trans_pitch   = 0x10022B58, .vowel_dur  = 0x10022DA8,
    .stress_num    = 0x10092FD8, .stress_add = 0x10092FC0,
    .sound_add     = 0x10092F58,

    .diph_records  = 0x100292A0, .diph_offsets = 0x1002D508,
    .diph_offsets_end = 0x1002E818, .voices    = 0x10022284,

    .h = {
        [BST_H_LETTER]   = 0x10009800, [BST_H_DIGIT]    = 0x10009810,
        [BST_H_EAT1]     = 0x10009840, [BST_H_SPACE]    = 0x10009850,
        [BST_H_DOT]      = 0x10009870, [BST_H_CURRENCY] = 0x10009890,
        [BST_H_PUNCT]    = 0x100098B0, [BST_H_DASH]     = 0x100098D0,
        [BST_H_EXPONENT] = 0x10009AA0, [BST_H_MODE1]    = 0x10009AD0,
        [BST_H_MODE2]    = 0x10009AE0, [BST_H_DEL]      = 0x10009AF0,
        [BST_H_OPENER]   = 0x10009B10, [BST_H_TILDE]    = 0x10009B70,
        [BST_H_APOSDOT]  = 0x10009BD0, [BST_H_APOS]     = 0x10009BF0,
        [BST_H_POSSESS]  = 0x10009C10, [BST_H_EAT2]     = 0x10007030,
        [BST_H_EAT3]     = 0x10007040, [BST_H_WORD]     = 0x10007AC0,
        [BST_H_NUMBER]   = 0x10005E40, [BST_H_DOTTED]   = 0x10007BA0,
        [BST_H_SEP]      = 0x10009DE0, [BST_H_GROUPS]   = 0x10005460,
        [BST_H_SEPNUM]   = 0x10005FA0, [BST_H_MONEY]    = 0x100046D0,
        [BST_H_DASH2]    = 0x10007650, [BST_H_ORDINAL]  = 0x10009930,
        [BST_H_ORDEMIT]  = 0x10001B00, [BST_H_PUNCTOUT] = 0x100070A0,
        [BST_H_DOTOUT]   = 0x100074F0,
    },
    .s = {
        [BST_S_POINT]     = 0x1002E7A0, [BST_S_DOLLARS] = 0x1002E780, [BST_S_DOLLAR] = 0x1002E77C,
        [BST_S_AND]       = 0x1002E784, [BST_S_CENTS]    = 0x1002E790,
        [BST_S_ORD_ST]    = 0x1002E7CC, [BST_S_ORD_ND]   = 0x1002E7D0,
        [BST_S_ORD_RD]    = 0x1002E7D4, [BST_S_ORD_TIETH]= 0x1002E7E0,
        [BST_S_ORD_FIFTH] = 0x1002E7D8, [BST_S_ORD_FIRST]= 0x1002E7DC,
        [BST_S_ORD_TH]    = 0x1002E7E4, [BST_S_GRPSEP]   = 0x1002E7EC,
        [BST_S_PLURAL]    = 0x1002E7DC, [BST_S_DIGITS]   = 0x1002EDF8,
        [BST_S_TENS]      = 0x1002EE20, [BST_S_TEENS]    = 0x1002EE48,
        [BST_S_SCALES]    = 0x1002EF30, [BST_S_OH]       = 0x1002E79C,
        [BST_S_HUNDRED]   = 0x1002E794, [BST_S_ZERO]     = 0x1002EEB8,
    },
};

/* The 1998 English module's table directory, as far as it has been settled.
   Addresses are (segment << 16) | offset, which is what bst_image_init_ne
   lays the module out to use.

   The tables that are the engine rather than the language were placed by
   matching the 1995 build's bytes; the transition table and its handlers by
   aligning the two state machines, which agree row for row; the dictionary
   buckets by reading the switch that selects them; and the pointer slots that
   name a spoken word by which text makes the engine read them.

   A zero means not yet settled, not absent. bst_map_gaps reports them. */
const bst_tabmap BST_MAP_1998_ENG = {
    .dict_silent    = 'e',
    .trie_ent       = 3,
    .suffix_n       = 3, .suffix_max = 3,
    .near_ptrs    = 1,
    .unvoiced_no_frac = 1,
    .long_silence_chunk = 1,
    .long_silence_f0 = 0xD1,
    .chattr        = 0x00101476, .letterattr = 0x00101576,
    .casemap       = 0x00101676, .symmap     = 0x00101776,

    .phattr1       = 0x00100ef8, .phattr2    = 0x00100f76,
    .classtab      = 0x00100b80, .exctab     = 0x00100b86,
    .basedur       = 0x00100b60, .coefgain   = 0x0010055e,

    .tokstates     = 0x001011dc, .names      = 0x00101d1e,
    .code_lo       = 0x00000000, .code_hi    = 0x0001e458,
    .tok_stride = 6,
    .tok_state_off = 0, .tok_state_w = 1,
    .tok_handler_off = 1, .tok_handler_w = 2,
    .tok_next_off = 5, .tok_next_w = 1,
    .lts_index_stride = 3,
    .rule_stride = 7, .rule_prio_w = 1,
    .trie_st_off = 1, .trie_li_off = 5, .trie_base_off = 9,
    .trie_max_off = 11, .trie_po_off = 13,
    .silence_f0 = 0xD1, .unvoiced_dur = 0x6E, .unvoiced_reps = 8,
    .voice_span = 410,
    .vowel_dur_shift = 1, .trn_round = 0,

    .code_medial   = 0x00100068, .code_initial  = 0x001000d2,
    .ph_single     = 0x00100ff4, .ph_single_max = 0x001010a0,
    .ph_pair       = 0x001010a2, .ph_pair_max   = 0x001011da,
    .bucket_index  = {
        0x0008a6ca, 0x0008eac4, 0x00094bf2, 0x00098064, 0x0009d9b0,
        0x000a650a, 0x000a9eec, 0x000ae59e, 0x000b3972, 0x000b7aca,
        0x000bdba2, 0x000c7dd6, 0x000d860e, 0x000db550, 0x000e9ab0,
    },
    .bucket_data   = {
        0x00080000, 0x0008a922, 0x00090000, 0x00094d0c, 0x0009812a,
        0x000a0000, 0x000a668a, 0x000a9fc8, 0x000b0000, 0x000b3a44,
        0x000b7bb2, 0x000c0000, 0x000d0000, 0x000d8808, 0x000e0000,
    },

    .suffix_ptrs   = 0x0010005c, .lts_index = 0x00060000,
    .dispatch      = 0x00050000, .rules     = 0x00030000,
    .patterns      = 0x00020000, .outputs = 0x00040000,

    .trie_desc     = 0x00070000,
    .modmap        = 0x00102452, .modtab = 0x00102654,

    .trans_pitch   = 0x001001ec, .vowel_dur  = 0x00100438,
    .stress_num    = 0x001001c8, .stress_add = 0x001001b6,
    .sound_add     = 0x00100154,

    .diph_records   = 0x000f6018, .diph_offsets = 0x000fa280,
    .diph_offsets_end = 0x000fb590, .voices    = 0x000f0000,

    .h = {
        [BST_H_LETTER]   = 0x00008376, [BST_H_DIGIT]    = 0x0000837c,
        [BST_H_EAT1]     = 0x00008388, [BST_H_SPACE]    = 0x00008394,
        [BST_H_DOT]      = 0x0000839c, [BST_H_CURRENCY] = 0x000083aa,
        [BST_H_PUNCT]    = 0x000083c0, [BST_H_DASH]     = 0x000083d2,
        [BST_H_EXPONENT] = 0x000084ea, [BST_H_MODE1]    = 0x0000850c,
        [BST_H_MODE2]    = 0x0000851a, [BST_H_DEL]      = 0x00008528,
        [BST_H_OPENER]   = 0x0000853c, [BST_H_TILDE]    = 0x00008588,
        [BST_H_APOSDOT]  = 0x000085ca, [BST_H_APOS]     = 0x000085e0,
        [BST_H_POSSESS]  = 0x000085ee, [BST_H_EAT2]     = 0x0000b648,
        [BST_H_EAT3]     = 0x0000b654, [BST_H_WORD]     = 0x0000a9c6,
        [BST_H_NUMBER]   = 0x0000951a, [BST_H_DOTTED]   = 0x0000aa88,
        [BST_H_SEP]      = 0x00008736, [BST_H_GROUPS]   = 0x00008bf2,
        [BST_H_SEPNUM]   = 0x00009650, [BST_H_MONEY]    = 0x0000992a,
        [BST_H_DASH2]    = 0x0000a6ca, [BST_H_ORDINAL]  = 0x000083fa,
        [BST_H_ORDEMIT]  = 0x0000c054, [BST_H_PUNCTOUT] = 0x0000a2e4,
        [BST_H_DOTOUT]   = 0x0000a5ba,
    },
    .s = {
        [BST_S_DOLLARS] = 0x001023A4, [BST_S_DOLLAR] = 0x001023A0, [BST_S_AND]      = 0x001023a8,
        [BST_S_CENTS]    = 0x001023b4, [BST_S_HUNDRED]  = 0x001023b8,
        [BST_S_OH]       = 0x001023c0, [BST_S_POINT]    = 0x001023c4,
        [BST_S_ORD_ST]   = 0x001023f0, [BST_S_ORD_ND]   = 0x001023f4,
        [BST_S_ORD_RD]   = 0x001023f8, [BST_S_ORD_FIFTH]= 0x001023fc,
        [BST_S_ORD_FIRST]= 0x00102400, [BST_S_ORD_TIETH]= 0x00102404,
        [BST_S_ORD_TH]   = 0x00102408, [BST_S_GRPSEP]   = 0x00102410,
        [BST_S_PLURAL]   = 0x00102400,
        [BST_S_DIGITS]   = 0x00101f76, [BST_S_TENS]     = 0x00101fe0,
        [BST_S_TEENS]    = 0x00102058, [BST_S_SCALES]   = 0x00102140,
        [BST_S_ZERO]     = 0x00102036,
    },
};

/* The other five 1998 languages, written by tools/analysis/mkmap.py, which
   reproduces the English map above address for address. */
/* KGMDUT. */
const bst_tabmap BST_MAP_1998_DUT = {
    .dict_silent    = 'o',
    .trie_ent       = 3,
    .trn_flank_tenths = 7,
    .no_suffix      = 1,
    .stress_rule    = 5,
    .ph_kind        = 4,
    .suffix_n       = 17, .suffix_max = 6,
    .near_ptrs    = 1,
    .unvoiced_no_frac = 1,
    .dur_mult       = 53,
    .pitch_rate     = 10400,
    .inton_mode     = 3,
    .slope_mult     = 167,
    .inton_dur_mult = 887,
    .min_period     = 62,
    .long_silence_chunk = 1,
    .long_silence_f0 = 0xD1,
    .vdur_kind      = 1,
    .vdur_slow      = 0x41,
    .trn_kind       = 6,
    .trn_whole      = 1,
    .trn_round      = 1,
    .pair_class = {
        0, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 1, 7, 7, 7, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0,
    },
    .basedur        = 0x000a0da6,
    .casemap        = 0x000a1914,
    .chattr         = 0x000a1714,
    .classtab       = 0x000a0dc6,
    .code_initial   = 0x000a0264,
    .code_medial    = 0x000a01fa,
    .coefgain       = 0x000a07a4,
    .diph_offsets   = 0x00099858,
    .diph_offsets_end = 0x0009b22e,
    .diph_records   = 0x00093a98,
    .dispatch       = 0x00050000,
    .exctab         = 0x000a0dcc,
    .letterattr     = 0x000a1814,
    .lts_index      = 0x00060000,
    .modmap         = 0x00080000,
    .modtab         = 0x00080012,
    .modtab_rows    = 0x000a0000,
    .names          = 0x000a1fe4,
    .outputs        = 0x00040000,
    .patterns       = 0x00020000,
    .ph_pair        = 0x000a133a,
    .ph_pair_max    = 0x000a1472,
    .ph_single      = 0x000a128c,
    .ph_single_max  = 0x000a1338,
    .phattr1        = 0x000a1180,
    .phattr2        = 0x000a1206,
    .rules          = 0x00030000,
    .sound_add      = 0x000a02e6,
    .stress_add     = 0x000a035a,
    .stress_num     = 0x000a036c,
    .suffix_ptrs    = 0x000a00aa,
    .symmap         = 0x000a1a14,
    .tokstates      = 0x000a1474,
    .trans_pitch    = 0x000a0390,
    .trie_desc      = 0x00070000,
    .voices         = 0x00090000,
    .vowel_dur      = 0x000a0648,
    .code_lo = 0x00000000, .code_hi = 0x0000d0d0,
    .tok_stride = 6, .tok_state_off = 0, .tok_state_w = 1,
    .tok_handler_off = 1, .tok_handler_w = 2,
    .tok_next_off = 5, .tok_next_w = 1,
    .lts_index_stride = 3, .rule_stride = 7, .rule_prio_w = 1,
    .trie_st_off = 1, .trie_li_off = 5, .trie_base_off = 9,
    .trie_max_off = 11, .trie_po_off = 13,
    .silence_f0 = 0xD1, .unvoiced_dur = 0x68, .unvoiced_reps = 8,
    .voice_span = 500,
    .vowel_dur_shift = 0, .code_shift = 9,
    .s = {
        [BST_S_AND] = 0x000a25d2,
        [BST_S_CENTS] = 0x000a25de,
        [BST_S_DIGITS] = 0x000a2242,
        [BST_S_DOLLARS] = 0x000A25CE, [BST_S_DOLLAR] = 0x000A25CA,
        [BST_S_GRPSEP] = 0x000a263a,
        [BST_S_HUNDRED] = 0x000a25e2,
        [BST_S_OH] = 0x000a25ea,
        [BST_S_ORD_FIFTH] = 0x000a2626,
        [BST_S_ORD_FIRST] = 0x000a262a,
        [BST_S_ORD_ND] = 0x000a261e,
        [BST_S_ORD_RD] = 0x000a2622,
        [BST_S_ORD_ST] = 0x000a261a,
        [BST_S_ORD_TH] = 0x000a2632,
        [BST_S_ORD_TIETH] = 0x000a262e,
        [BST_S_PLURAL] = 0x000a262a,
        [BST_S_POINT] = 0x000a25ee,
        [BST_S_SCALES] = 0x000a240c,
        [BST_S_TEENS] = 0x000a2324,
        [BST_S_TENS] = 0x000a22b4,
        [BST_S_ZERO] = 0x000a2302,
    },
    .bucket_index = { 0x000816ea, 0x00081b7a, 0x00082294, 0x000830fc, 0x0008387a, 0x0008413a, 0x00084200, 0x00084656, 0x00084898, 0x000849de, 0x00084a6c, 0x00084d90, 0x00085ba4, 0x000863b2, 0x000878e4, },
    .bucket_data  = { 0x000800de, 0x0008174c, 0x00081b90, 0x000822b4, 0x00083136, 0x0008389a, 0x00084160, 0x00084208, 0x0008466c, 0x000848a6, 0x000849e8, 0x00084a72, 0x00084da2, 0x00085be8, 0x000863d2, },
    .h = {
        [BST_H_APOS] = 0x0000762c,
        [BST_H_APOSDOT] = 0x00007616,
        [BST_H_CURRENCY] = 0x000074e2,
        [BST_H_DASH] = 0x0000750a,
        [BST_H_DASH2] = 0x00009782,
        [BST_H_DEL] = 0x00007574,
        [BST_H_DIGIT] = 0x000074b4,
        [BST_H_DOT] = 0x000074d4,
        [BST_H_DOTOUT] = 0x00009672,
        [BST_H_DOTTED] = 0x00009b40,
        [BST_H_EAT1] = 0x000074c0,
        [BST_H_EAT2] = 0x0000a456,
        [BST_H_EAT3] = 0x0000a462,
        [BST_H_EXPONENT] = 0x00007536,
        [BST_H_GROUPS] = 0x00007c3e,
        [BST_H_LETTER] = 0x000074ae,
        [BST_H_MODE1] = 0x00007558,
        [BST_H_MODE2] = 0x00007566,
        [BST_H_MONEY] = 0x00008a00,
        [BST_H_NUMBER] = 0x000085f0,
        [BST_H_OPENER] = 0x00007588,
        [BST_H_ORDEMIT] = 0x0000adf2,
        [BST_H_ORDINAL] = 0x00007532,
        [BST_H_POSSESS] = 0x0000763a,
        [BST_H_PUNCT] = 0x000074f8,
        [BST_H_PUNCTOUT] = 0x0000939c,
        [BST_H_SEP] = 0x00007782,
        [BST_H_SEPNUM] = 0x00008726,
        [BST_H_SPACE] = 0x000074cc,
        [BST_H_TILDE] = 0x000075d4,
        [BST_H_WORD] = 0x00009a7e,
    },
};

/* KGMFRN. */
const bst_tabmap BST_MAP_1998_FRN = {
    .run_stop_skip  = 0x26,
    .trn_between_tenths = 7,
    .dict_silent    = 's',
    .trie_ent       = 3,
    .no_suffix      = 1,
    .ph_kind        = 6,
    .suffix_n       = 6, .suffix_max = 6,
    .near_ptrs    = 1,
    .unvoiced_no_frac = 1,
    .level_max      = 4,
    .num_kind          = 4,
    .num_group_kind    = 3,
    .num_two_kind      = 3,
    .num_scale_kind    = 3,
    .num_liaison       = 0x59,
    .num_four_as_hundreds = 1,
    .inton_mode     = 3,
    .long_silence_chunk = 1,
    .long_silence_f0 = 0xD1,
    .vdur_kind      = 4,
    .trn_kind       = 4,
    .punct_s8       = { 5, 3, 9, 1, 9, 3, 9, 5, 9 },
    .punct_s9       = { 0, 0x4C, 0x4C, 0, 0x4C, 0x4C, 0x48, 0, 0x4C },
    .acc_kind       = 1,
    .stress_rule    = 4,
    .pair_class = {
        0, 7, 7, 7, 2, 2, 2, 1, 1, 1, 1, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .basedur        = 0x000a0ae2,
    .casemap        = 0x000a15a2,
    .chattr         = 0x000a13a2,
    .classtab       = 0x000a0b02,
    .code_initial   = 0x000a008a,
    .code_medial    = 0x000a00f4,
    .coefgain       = 0x000a04e0,
    .diph_offsets   = 0x000953fc,
    .diph_offsets_end = 0x000960ce,
    .diph_records   = 0x00091d4c,
    .dispatch       = 0x00050000,
    .exctab         = 0x000a0b08,
    .letterattr     = 0x000a14a2,
    .lts_index      = 0x00060000,
    .modmap         = 0x00080000,
    .modtab         = 0x00080024,
    .modtab_rows    = 0x000a0000,
    .names          = 0x000a1aec,
    .outputs        = 0x00040000,
    .patterns       = 0x00020000,
    .ph_pair        = 0x000a0fac,
    .ph_pair_max    = 0x000a10e4,
    .ph_single      = 0x000a0efe,
    .ph_single_max  = 0x000a0faa,
    .phattr1        = 0x000a0e1e,
    .phattr2        = 0x000a0e8e,
    .rules          = 0x00030000,
    .sound_add      = 0x000a0176,
    .stress_add     = 0x000a01c8,
    .stress_num     = 0x000a01da,
    .suffix_ptrs    = 0x000a0072,
    .symmap         = 0x000a16a2,
    .tokstates      = 0x000a10ea,
    .trans_pitch    = 0x000a01fe,
    .trie_desc      = 0x00070000,
    .voices         = 0x00090000,
    .vowel_dur      = 0x000a03ea,
    .code_lo = 0x00000000, .code_hi = 0x0000d432,
    .tok_stride = 6, .tok_state_off = 0, .tok_state_w = 1,
    .tok_handler_off = 1, .tok_handler_w = 2,
    .tok_next_off = 5, .tok_next_w = 1,
    .lts_index_stride = 3, .rule_stride = 7, .rule_prio_w = 1,
    .trie_st_off = 1, .trie_li_off = 5, .trie_base_off = 9,
    .trie_max_off = 11, .trie_po_off = 13,
    .silence_f0 = 0xD1, .unvoiced_dur = 0x6E, .unvoiced_reps = 8,
    .voice_span = 250,
    .vowel_dur_shift = 1, .trn_round = 0, .code_shift = -8,
    .cmd_code = 0x76,
    .s = {
        [BST_S_AND] = 0x000a211c,
        [BST_S_CENTS] = 0x000a2128,
        [BST_S_DIGITS] = 0x000a1d4c,
        [BST_S_DOLLARS] = 0x000A2118, [BST_S_DOLLAR] = 0x000A2114,
        [BST_S_GRPSEP] = 0x000a2184,
        [BST_S_HUNDRED] = 0x000a212c,
        [BST_S_OH] = 0x000a2134,
        [BST_S_ORD_FIFTH] = 0x000a2170,
        [BST_S_ORD_FIRST] = 0x000a2174,
        [BST_S_ORD_ND] = 0x000a2168,
        [BST_S_ORD_RD] = 0x000a216c,
        [BST_S_ORD_ST] = 0x000a2164,
        [BST_S_ORD_TH] = 0x000a217c,
        [BST_S_ORD_TIETH] = 0x000a2178,
        [BST_S_PLURAL] = 0x000a2174,
        [BST_S_POINT] = 0x000a2138,
        [BST_S_SCALES] = 0x000a1f1a,
        [BST_S_TEENS] = 0x000a1e18,
        [BST_S_TENS] = 0x000a1db6,
        [BST_S_ZERO] = 0x000a1e0c,
    },
    .bucket_index = { 0x00080918, 0x00080c94, 0x00081018, 0x00081566, 0x000819d2, 0x00081e04, 0x00082276, 0x000824c2, 0x000826f8, 0x00082958, 0x00082b0c, 0x00082c9a, 0x00082d94, 0x00082fe8, 0x00083248, },
    .bucket_data  = { 0x000800c0, 0x0008093c, 0x00080ca4, 0x00081028, 0x0008157e, 0x000819e6, 0x00081e18, 0x0008228a, 0x000824ce, 0x00082704, 0x00082966, 0x00082b16, 0x00082ca4, 0x00082d9c, 0x00082ff6, },
    .h = {
        [BST_H_APOS] = 0x0000752a,
        [BST_H_APOSDOT] = 0x00007514,
        [BST_H_CURRENCY] = 0x0000730e,
        [BST_H_DASH] = 0x00007336,
        [BST_H_DASH2] = 0x00009794,
        [BST_H_DEL] = 0x00007472,
        [BST_H_DIGIT] = 0x000072e0,
        [BST_H_DOT] = 0x00007300,
        [BST_H_DOTOUT] = 0x00009684,
        [BST_H_DOTTED] = 0x00009b52,
        [BST_H_EAT1] = 0x000072ec,
        [BST_H_EAT2] = 0x0000a69c,
        [BST_H_EAT3] = 0x0000a6a8,
        [BST_H_EXPONENT] = 0x00007434,
        [BST_H_GROUPS] = 0x00007bd4,
        [BST_H_LETTER] = 0x000072da,
        [BST_H_MODE1] = 0x00007456,
        [BST_H_MODE2] = 0x00007464,
        [BST_H_MONEY] = 0x00008a12,
        [BST_H_NUMBER] = 0x00008602,
        [BST_H_OPENER] = 0x00007486,
        [BST_H_ORDEMIT] = 0x0000b0ee,
        [BST_H_ORDINAL] = 0x0000735e,
        [BST_H_POSSESS] = 0x00007538,
        [BST_H_PUNCT] = 0x00007324,
        [BST_H_PUNCTOUT] = 0x000093ae,
        [BST_H_SEP] = 0x00007680,
        [BST_H_SEPNUM] = 0x00008738,
        [BST_H_SPACE] = 0x000072f8,
        [BST_H_TILDE] = 0x000074d2,
        [BST_H_WORD] = 0x00009a90,
    },
};

/* KGMGRM. */
const bst_tabmap BST_MAP_1998_GRM = {
    .run_stop_skip  = 0x1D,
    .dict_silent    = 'n',
    .trie_ent       = 3,
    .syl_weak       = { 0x80, 0x81 },
    .suffix_n       = 19, .suffix_max = 6,
    .near_ptrs    = 1,
    .strip_kind     = 1,
    .suffix_tail    = { [0] = 0x10, [1] = 0x1B, [2] = 0x0C, [3] = 0x0A },
    .unvoiced_no_frac = 1,
    .inton_mode     = 3,
    .long_silence_chunk = 1,
    .long_silence_f0 = 0xD1,
    .ph_kind        = 5,
    .pair_class = {
        0, 7, 2, 7, 2, 7, 2, 2, 2, 2, 1, 1, 1, 1, 1, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 1, 7, 7, 7, 7, 2,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0,
    },
    .vdur_kind      = 3,
    .trn_kind       = 3,
    .trn_between_wide = 1,
    .fall_code      = 0x3C,
    .basedur        = 0x000a0d8e,
    .casemap        = 0x000a18ea,
    .chattr         = 0x000a16ea,
    .classtab       = 0x000a0dae,
    .code_initial   = 0x000a0260,
    .code_medial    = 0x000a01f6,
    .coefgain       = 0x000a078c,
    .diph_offsets   = 0x00098e94,
    .diph_offsets_end = 0x0009a786,
    .diph_records   = 0x000934bc,
    .dispatch       = 0x00050000,
    .exctab         = 0x000a0db4,
    .letterattr     = 0x000a17ea,
    .lts_index      = 0x00060000,
    .modmap         = 0x00080000,
    .modtab         = 0x00080022,
    .modtab_rows    = 0x000a0000,
    .names          = 0x000a1ff8,
    .outputs        = 0x00040000,
    .patterns       = 0x00020000,
    .ph_pair        = 0x000a1310,
    .ph_pair_max    = 0x000a1448,
    .ph_single      = 0x000a1262,
    .ph_single_max  = 0x000a130e,
    .phattr1        = 0x000a1156,
    .phattr2        = 0x000a11dc,
    .rules          = 0x00030000,
    .sound_add      = 0x000a02e2,
    .stress_add     = 0x000a0354,
    .stress_num     = 0x000a0366,
    .suffix_ptrs    = 0x000a00aa,
    .symmap         = 0x000a19ea,
    .tokstates      = 0x000a144a,
    .trans_pitch    = 0x000a038a,
    .trie_desc      = 0x00070000,
    .voices         = 0x00090000,
    .vowel_dur      = 0x000a0636,
    .code_lo = 0x00000000, .code_hi = 0x0000e084,
    .tok_stride = 6, .tok_state_off = 0, .tok_state_w = 1,
    .tok_handler_off = 1, .tok_handler_w = 2,
    .tok_next_off = 5, .tok_next_w = 1,
    .lts_index_stride = 3, .rule_stride = 7, .rule_prio_w = 1,
    .trie_st_off = 1, .trie_li_off = 5, .trie_base_off = 9,
    .trie_max_off = 11, .trie_po_off = 13,
    .silence_f0 = 0xD1, .unvoiced_dur = 0x6E, .unvoiced_reps = 8,
    .voice_span = 450,
    .vowel_dur_shift = 1, .trn_round = 0, .code_shift = 8,
    .s = {
        [BST_S_AND] = 0x000a268c,
        [BST_S_CENTS] = 0x000a2698,
        [BST_S_DIGITS] = 0x000a2258,
        [BST_S_DOLLARS] = 0x000A2688, [BST_S_DOLLAR] = 0x000A2684,
        [BST_S_GRPSEP] = 0x000a26f4,
        [BST_S_HUNDRED] = 0x000a269c,
        [BST_S_OH] = 0x000a26a4,
        [BST_S_ORD_FIFTH] = 0x000a26e0,
        [BST_S_ORD_FIRST] = 0x000a26e4,
        [BST_S_ORD_ND] = 0x000a26d8,
        [BST_S_ORD_RD] = 0x000a26dc,
        [BST_S_ORD_ST] = 0x000a26d4,
        [BST_S_ORD_TH] = 0x000a26ec,
        [BST_S_ORD_TIETH] = 0x000a26e8,
        [BST_S_PLURAL] = 0x000a26e4,
        [BST_S_POINT] = 0x000a26a8,
        [BST_S_SCALES] = 0x000a2430,
        [BST_S_TEENS] = 0x000a2348,
        [BST_S_TENS] = 0x000a22ce,
        [BST_S_ZERO] = 0x000a2318,
    },
    .bucket_index = { 0x0008216c, 0x00082e36, 0x000848a0, 0x00084f88, 0x00085a94, 0x000862c0, 0x00087192, 0x00087ca6, 0x000885ac, 0x00088cb6, 0x000895a0, 0x00089a50, 0x0008a142, 0x0008a5b8, 0x0008b2b0, },
    .bucket_data  = { 0x000800f6, 0x000821d6, 0x00082e60, 0x000848f6, 0x00084fa0, 0x00085aba, 0x000862de, 0x000871c2, 0x00087ccc, 0x000885ce, 0x00088cce, 0x000895c0, 0x00089a62, 0x0008a15c, 0x0008a5ca, },
    .h = {
        [BST_H_APOS] = 0x0000804e,
        [BST_H_APOSDOT] = 0x00008038,
        [BST_H_CURRENCY] = 0x00007f04,
        [BST_H_DASH] = 0x00007f2c,
        [BST_H_DASH2] = 0x0000a208,
        [BST_H_DEL] = 0x00007f96,
        [BST_H_DIGIT] = 0x00007ed6,
        [BST_H_DOT] = 0x00007ef6,
        [BST_H_DOTOUT] = 0x0000a0f8,
        [BST_H_DOTTED] = 0x0000a5c6,
        [BST_H_EAT1] = 0x00007ee2,
        [BST_H_EAT2] = 0x0000aefc,
        [BST_H_EAT3] = 0x0000af08,
        [BST_H_EXPONENT] = 0x00007f58,
        [BST_H_GROUPS] = 0x0000867a,
        [BST_H_LETTER] = 0x00007ed0,
        [BST_H_MODE1] = 0x00007f7a,
        [BST_H_MODE2] = 0x00007f88,
        [BST_H_MONEY] = 0x00009486,
        [BST_H_NUMBER] = 0x00009076,
        [BST_H_OPENER] = 0x00007faa,
        [BST_H_ORDEMIT] = 0x0000b882,
        [BST_H_ORDINAL] = 0x00007f54,
        [BST_H_POSSESS] = 0x0000805c,
        [BST_H_PUNCT] = 0x00007f1a,
        [BST_H_PUNCTOUT] = 0x00009e22,
        [BST_H_SEP] = 0x000081a4,
        [BST_H_SEPNUM] = 0x000091ac,
        [BST_H_SPACE] = 0x00007eee,
        [BST_H_TILDE] = 0x00007ff6,
        [BST_H_WORD] = 0x0000a504,
    },
};

/* KGMITL. */
const bst_tabmap BST_MAP_1998_ITL = {
    .no_weak_mark   = 1,
    .dict_silent    = 'o',
    .trie_ent       = 3,
    .no_suffix      = 1,
    .trn_flank_tenths = 7,
    .suffix_n       = 0, .suffix_max = 6,
    .near_ptrs    = 1,
    .unvoiced_no_frac = 1,
    .ph_kind        = 2,
    .vdur_kind      = 2,
    .inton_mode     = 3,
    .long_silence_chunk = 1,
    .long_silence_f0 = 0xD1,
    .trn_kind       = 2,
    .trn_thin       = 2,
    .stress_rule    = 1,
    .mark_emph      = 0x33,
    .pair_class = {
        0, 7, 7, 7, 2, 2, 2, 7, 7, 7, 7, 1, 1, 1, 1, 1,
        7, 7, 7, 7, 7, 1, 1, 1, 7, 7, 7, 7, 6, 6, 6, 6,
        6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .basedur        = 0x000a0a6e,
    .casemap        = 0x000a1524,
    .chattr         = 0x000a1324,
    .classtab       = 0x000a0a8e,
    .code_initial   = 0x000a0066,
    .code_medial    = 0x000a00d0,
    .coefgain       = 0x000a046c,
    .diph_offsets   = 0x0009364c,
    .diph_offsets_end = 0x000940b6,
    .diph_records   = 0x00090f3c,
    .dispatch       = 0x00050000,
    .exctab         = 0x000a0a94,
    .letterattr     = 0x000a1424,
    .lts_index      = 0x00060000,
    .modmap         = 0x00080000,
    .modtab         = 0x00080010,
    .modtab_rows    = 0x000a0000,
    .names          = 0x000a1dc4,
    .outputs        = 0x00040000,
    .patterns       = 0x00020000,
    .ph_pair        = 0x000a0f4a,
    .ph_pair_max    = 0x000a1082,
    .ph_single      = 0x000a0e9c,
    .ph_single_max  = 0x000a0f48,
    .phattr1        = 0x000a0db8,
    .phattr2        = 0x000a0e2a,
    .rules          = 0x00030000,
    .sound_add      = 0x000a0152,
    .stress_add     = 0x000a019c,
    .stress_num     = 0x000a01ae,
    .suffix_ptrs    = 0x000a0053,
    .symmap         = 0x000a1624,
    .tokstates      = 0x000a1084,
    .trans_pitch    = 0x000a01d2,
    .trie_desc      = 0x00070000,
    .voices         = 0x00090000,
    .vowel_dur      = 0x000a038e,
    .code_lo = 0x00000000, .code_hi = 0x0000d2de,
    .tok_stride = 6, .tok_state_off = 0, .tok_state_w = 1,
    .tok_handler_off = 1, .tok_handler_w = 2,
    .tok_next_off = 5, .tok_next_w = 1,
    .lts_index_stride = 3, .rule_stride = 7, .rule_prio_w = 1,
    .trie_st_off = 1, .trie_li_off = 5, .trie_base_off = 9,
    .trie_max_off = 11, .trie_po_off = 13,
    .silence_f0 = 0xD1, .unvoiced_dur = 0x6E, .unvoiced_reps = 8,
    .voice_span = 130,
    .vowel_dur_shift = 1, .trn_round = 0, .code_shift = -12,
    .s = {
        [BST_S_AND] = 0x000a2544,
        [BST_S_CENTS] = 0x000a2550,
        [BST_S_DIGITS] = 0x000a209e,
        [BST_S_DOLLARS] = 0x000A2540, [BST_S_DOLLAR] = 0x000A253C,
        [BST_S_GRPSEP] = 0x000a25ac,
        [BST_S_HUNDRED] = 0x000a2554,
        [BST_S_OH] = 0x000a255c,
        [BST_S_ORD_FIFTH] = 0x000a2598,
        [BST_S_ORD_FIRST] = 0x000a259c,
        [BST_S_ORD_ND] = 0x000a2590,
        [BST_S_ORD_RD] = 0x000a2594,
        [BST_S_ORD_ST] = 0x000a258c,
        [BST_S_ORD_TH] = 0x000a25a4,
        [BST_S_ORD_TIETH] = 0x000a25a0,
        [BST_S_PLURAL] = 0x000a259c,
        [BST_S_POINT] = 0x000a2560,
        [BST_S_SCALES] = 0x000a22a2,
        [BST_S_TEENS] = 0x000a21ba,
        [BST_S_TENS] = 0x000a2126,
        [BST_S_ZERO] = 0x000a215e,
    },
    .bucket_index = { 0x0008139e, 0x00082018, 0x0008290a, 0x00083408, 0x00083d60, 0x000847de, 0x00084dd8, 0x0008535c, 0x00085a72, 0x00085f98, 0x000862a2, 0x00086634, 0x00086b7c, 0x00086edc, 0x00086fc8, },
    .bucket_data  = { 0x0008009c, 0x000813f2, 0x0008204e, 0x00082930, 0x0008343a, 0x00083d8a, 0x0008480e, 0x00084df4, 0x00085378, 0x00085a92, 0x00085fb2, 0x000862b4, 0x00086648, 0x00086b96, 0x00086eee, },
    .h = {
        [BST_H_APOS] = 0x00007760,
        [BST_H_APOSDOT] = 0x0000774a,
        [BST_H_CURRENCY] = 0x00007616,
        [BST_H_DASH] = 0x0000763e,
        [BST_H_DASH2] = 0x0000997a,
        [BST_H_DEL] = 0x000076a8,
        [BST_H_DIGIT] = 0x000075e8,
        [BST_H_DOT] = 0x00007608,
        [BST_H_DOTOUT] = 0x0000986a,
        [BST_H_DOTTED] = 0x00009d38,
        [BST_H_EAT1] = 0x000075f4,
        [BST_H_EAT2] = 0x0000a674,
        [BST_H_EAT3] = 0x0000a680,
        [BST_H_EXPONENT] = 0x0000766a,
        [BST_H_GROUPS] = 0x00007d72,
        [BST_H_LETTER] = 0x000075e2,
        [BST_H_MODE1] = 0x0000768c,
        [BST_H_MODE2] = 0x0000769a,
        [BST_H_MONEY] = 0x00008bf8,
        [BST_H_NUMBER] = 0x000087e8,
        [BST_H_OPENER] = 0x000076bc,
        [BST_H_ORDEMIT] = 0x0000affa,
        [BST_H_ORDINAL] = 0x00007666,
        [BST_H_POSSESS] = 0x0000776e,
        [BST_H_PUNCT] = 0x0000762c,
        [BST_H_PUNCTOUT] = 0x00009594,
        [BST_H_SEP] = 0x000078b6,
        [BST_H_SEPNUM] = 0x0000891e,
        [BST_H_SPACE] = 0x00007600,
        [BST_H_TILDE] = 0x00007708,
        [BST_H_WORD] = 0x00009c76,
    },
};

/* KGMSPN. */
const bst_tabmap BST_MAP_1998_SPN = {
    .dict_silent    = 'o',
    .trie_ent       = 3,
    .no_suffix      = 1,
    .trn_between_tenths = 6,
    .trn_flank_tenths = 7,
    .ph_kind        = 1,
    .suffix_n       = 0, .suffix_max = 6,
    .near_ptrs    = 1,
    .unvoiced_no_frac = 1,
    .trn_whole      = 1,
    .inton_mode     = 3,
    .long_silence_chunk = 1,
    .long_silence_f0 = 0xD1,
    .fall_on_next   = 1,
    .hdr_shape      = 0x52,
    .trn_kind       = 1,
    .vdur_kind      = 1,
    .stress_rule    = 1,
    .mark_emph      = 0x33,
    .pair_class = {
        0, 7, 7, 7, 2, 7, 2, 7, 2, 7, 1, 1, 1, 1, 7, 7,
        7, 7, 7, 7, 1, 1, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .basedur        = 0x000a0a4c,
    .casemap        = 0x000a14fa,
    .chattr         = 0x000a12fa,
    .classtab       = 0x000a0a6c,
    .code_initial   = 0x000a0058,
    .code_medial    = 0x000a00c2,
    .coefgain       = 0x000a044a,
    .diph_offsets   = 0x00093520,
    .diph_offsets_end = 0x00093efa,
    .diph_records   = 0x00090e10,
    .dispatch       = 0x00050000,
    .exctab         = 0x000a0a72,
    .letterattr     = 0x000a13fa,
    .lts_index      = 0x00060000,
    .modmap         = 0x00080000,
    .modtab         = 0x00080008,
    .modtab_rows    = 0x000a0000,
    .names          = 0x000a1ba0,
    .outputs        = 0x00040000,
    .patterns       = 0x00020000,
    .ph_pair        = 0x000a0f20,
    .ph_pair_max    = 0x000a1058,
    .ph_single      = 0x000a0e72,
    .ph_single_max  = 0x000a0f1e,
    .phattr1        = 0x000a0d92,
    .phattr2        = 0x000a0e02,
    .rules          = 0x00030000,
    .sound_add      = 0x000a0144,
    .stress_add     = 0x000a018c,
    .stress_num     = 0x000a019e,
    .suffix_ptrs    = 0x000a0053,
    .symmap         = 0x000a15fa,
    .tokstates      = 0x000a105a,
    .trans_pitch    = 0x000a01c2,
    .trie_desc      = 0x00070000,
    .voices         = 0x00090000,
    .vowel_dur      = 0x000a0372,
    .code_lo = 0x00000000, .code_hi = 0x0000d49e,
    .tok_stride = 6, .tok_state_off = 0, .tok_state_w = 1,
    .tok_handler_off = 1, .tok_handler_w = 2,
    .tok_next_off = 5, .tok_next_w = 1,
    .lts_index_stride = 3, .rule_stride = 7, .rule_prio_w = 1,
    .trie_st_off = 1, .trie_li_off = 5, .trie_base_off = 9,
    .trie_max_off = 11, .trie_po_off = 13,
    .silence_f0 = 0xD1, .unvoiced_dur = 0x6E, .unvoiced_reps = 8,
    .voice_span = 120,
    .vowel_dur_shift = 1, .trn_round = 0, .code_shift = -13,
    .s = {
        [BST_S_AND] = 0x000a22a4,
        [BST_S_CENTS] = 0x000a22b0,
        [BST_S_DIGITS] = 0x000a1e82,
        [BST_S_DOLLARS] = 0x000A22A0, [BST_S_DOLLAR] = 0x000A229C,
        [BST_S_GRPSEP] = 0x000a230c,
        [BST_S_HUNDRED] = 0x000a22b4,
        [BST_S_OH] = 0x000a22bc,
        [BST_S_ORD_FIFTH] = 0x000a22f8,
        [BST_S_ORD_FIRST] = 0x000a22fc,
        [BST_S_ORD_ND] = 0x000a22f0,
        [BST_S_ORD_RD] = 0x000a22f4,
        [BST_S_ORD_ST] = 0x000a22ec,
        [BST_S_ORD_TH] = 0x000a2304,
        [BST_S_ORD_TIETH] = 0x000a2300,
        [BST_S_PLURAL] = 0x000a22fc,
        [BST_S_POINT] = 0x000a22c0,
        [BST_S_SCALES] = 0x000a2084,
        [BST_S_TEENS] = 0x000a1f9c,
        [BST_S_TENS] = 0x000a1f00,
        [BST_S_ZERO] = 0x000a1f42,
    },
    .bucket_index = { 0x000800d0, 0x000800e4, 0x000800f2, 0x00080282, 0x0008011c, 0x00080144, 0x00080170, 0x00080188, 0x000801bc, 0x000801ce, 0x000801e0, 0x00080206, 0x0008021e, 0x00080248, 0x0008027a, },
    .bucket_data  = { 0x0008007c, 0x000800d6, 0x000800ea, 0x00080280, 0x000800f8, 0x00080122, 0x0008014a, 0x00080176, 0x0008018e, 0x000801c2, 0x000801d4, 0x000801e6, 0x0008020c, 0x00080224, 0x0008024e, },
    .h = {
        [BST_H_APOS] = 0x00007952,
        [BST_H_APOSDOT] = 0x0000793c,
        [BST_H_CURRENCY] = 0x00007808,
        [BST_H_DASH] = 0x00007830,
        [BST_H_DASH2] = 0x00009ae6,
        [BST_H_DEL] = 0x0000789a,
        [BST_H_DIGIT] = 0x000077da,
        [BST_H_DOT] = 0x000077fa,
        [BST_H_DOTOUT] = 0x000099d6,
        [BST_H_DOTTED] = 0x00009ea4,
        [BST_H_EAT1] = 0x000077e6,
        [BST_H_EAT2] = 0x0000a82e,
        [BST_H_EAT3] = 0x0000a83a,
        [BST_H_EXPONENT] = 0x0000785c,
        [BST_H_GROUPS] = 0x00007f64,
        [BST_H_LETTER] = 0x000077d4,
        [BST_H_MODE1] = 0x0000787e,
        [BST_H_MODE2] = 0x0000788c,
        [BST_H_MONEY] = 0x00008d64,
        [BST_H_NUMBER] = 0x00008954,
        [BST_H_OPENER] = 0x000078ae,
        [BST_H_ORDEMIT] = 0x0000b1b4,
        [BST_H_ORDINAL] = 0x00007858,
        [BST_H_POSSESS] = 0x00007960,
        [BST_H_PUNCT] = 0x0000781e,
        [BST_H_PUNCTOUT] = 0x00009700,
        [BST_H_SEP] = 0x00007aa8,
        [BST_H_SEPNUM] = 0x00008a8a,
        [BST_H_SPACE] = 0x000077f2,
        [BST_H_TILDE] = 0x000078fa,
        [BST_H_WORD] = 0x00009de2,
    },
};

/* The 2006 English build's table directory.

   The 2006 family is the 1995 engine recompiled: the same structures, the
   same record shapes, and for English the same sound numbering, which is what
   lets the phoneme strings in it be found by searching for the 1995 build's.
   Three things moved. The two dictionary code limits became immediates, so
   the counts are given here instead of an address. The stress scaling became
   a byte table over thirty-two where the earlier builds hold a numerator and
   a denominator. And the variant tables the record pass reads were reordered,
   which is why they are this build's own and not the 1995 ones. */
const bst_tabmap BST_MAP_2006_ENG = {
    .tail_chars        = { '.', ' ', ' ' },
    .pair_prescan      = 1,
    .trn_dur_wide  = 1,
    .schwa_after   = 1,
    .pair_class = {
        0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 1, 1,
        1, 7, 7, 2, 2, 2, 7, 2, 7, 3, 3, 2, 7, 2, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5,
        4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .chattr        = 0x10082014, .letterattr = 0x10081D14,
    .casemap       = 0x10081F14, .symmap     = 0x10081E14,

    .phattr1       = 0x1008182C, .phattr2    = 0x100818AC,
    .classtab      = 0x1001EC4C, .exctab     = 0x1001EC54,
    .basedur       = 0x1001EC2C, .coefgain   = 0x1001E828,

    .tokstates     = 0x100810F8, .names      = 0x1008192C,
    .code_lo       = 0x10001000, .code_hi    = 0x10015000,
    .tok_stride = 12,
    .tok_state_off = 0, .tok_state_w = 4,
    .tok_handler_off = 4, .tok_handler_w = 4,
    .tok_next_off = 8, .tok_next_w = 4,
    .lts_index_stride = 4,
    .rule_stride = 8, .rule_prio_w = 2,
    .trie_st_off = 4, .trie_li_off = 8, .trie_base_off = 12,
    .trie_max_off = 14, .trie_po_off = 16,
    .silence_f0 = 0xD1, .unvoiced_dur = 0x68, .unvoiced_reps = 1,
    .min_period = 0x3E, .interp_round_mask = 0x007, .coef_round_mask = 0x006,
    .class_shift = 13, .long_silence_f0 = 0xD8, .exc_two_way = 1,
    .unvoiced_chunk = 1, .no_breath_break = 1, .no_closing_phrase = 1,
    .sepnum_first_three = 1, .close_pause = 1, .comma_ends_text = 1,
    .close_paren_comma = 1,
    .brace_ends_text = 1,
    .speak_now_inline = 1,
    .possessive_is = 1,
    .dur_mult = 0x35, .nearest_round = 1, .pitch_rate = 0x28A0,
    .slope_mult = 0xA7, .inton_dur_mult = 0x377, .inton_slope_shift = 1,
    .dur_frac_shift = 1, .contour_round = 1,
    .voice_span = 0, .voice_stride = 4,
    .vowel_dur_shift = 0, .trn_round = 1,
    .stress_shift = 5,

    .code_medial   = 0x10082124, .code_initial  = 0x10082190,
    .ph_single     = 0x1001E644, .ph_single_max = 0,
    .ph_pair       = 0x1001E6F0, .ph_pair_max   = 0,
    .ph_single_n   = 0x54,       .ph_pair_n     = 0x9A,
    .bucket_index  = {
        0x10029A7C, 0x1002E578, 0x10033660, 0x10037120, 0x1003CFA4,
        0x10044E6C, 0x10048F58, 0x1004D98C, 0x1005192C, 0x10056410,
        0x1005D33C, 0x10065CD8, 0x1006EDFC, 0x100723A0, 0x10078C5C,
    },
    .bucket_data   = {
        0x1001ED40, 0x10029CE8, 0x1002E684, 0x10033788, 0x10037200,
        0x1003D104, 0x10045044, 0x1004904C, 0x1004DAA0, 0x10051A10,
        0x10056518, 0x1005D4D8, 0x10065EE8, 0x1006F018, 0x10072464,
    },

    .suffix_ptrs   = 0x10082118, .lts_index = 0x10086EFC,
    .suffix_n      = 3,
    .suffix_max    = 3,
    .dispatch      = 0x10086B48, .rules     = 0x10083AD8,
    .patterns      = 0x100821FC, .outputs   = 0x10085C70,

    .trie_desc     = 0x10079AF0,
    .modmap        = 0x1001EC5C, .modtab    = 0x1001EC84,

    .trans_pitch   = 0x10079650, .vowel_dur  = 0x1007989C,
    .stress_num    = 0x10079AE0, .stress_add = 0x10079ACC,
    .sound_add     = 0x10079A68,

    .diph_records  = 0x100190F8, .diph_offsets = 0x1001D360,
    .diph_offsets_end = 0x1001E5C4, .voices    = 0x100150E8,

    .h = {
        [BST_H_HIGH] = 0x1000ED00,
        [BST_H_HIGHOUT] = 0x10001FB0,
        [BST_H_HIGH2] = 0x1000EEC0,
        [BST_H_LETTER]   = 0x1000EBE0, [BST_H_DIGIT]    = 0x1000EC00,
        [BST_H_EAT1]     = 0x1000EC40, [BST_H_SPACE]    = 0x1000EC50,
        [BST_H_DOT]      = 0x1000EC60, [BST_H_CURRENCY] = 0x1000EC70,
        [BST_H_PUNCT]    = 0x1000EC90, [BST_H_DASH]     = 0x1000ECB0,
        [BST_H_EXPONENT] = 0x1000EE50, [BST_H_MODE1]    = 0x1000EE80,
        [BST_H_MODE2]    = 0x1000EE90, [BST_H_DEL]      = 0x1000EEA0,
        [BST_H_OPENER]   = 0x1000EED0, [BST_H_TILDE]    = 0x1000EF30,
        [BST_H_APOSDOT]  = 0x1000EF80, [BST_H_APOS]     = 0x1000EFA0,
        [BST_H_POSSESS]  = 0x1000EFB0, [BST_H_EAT2]     = 0x1000F2B0,
        [BST_H_EAT3]     = 0x1000F2C0, [BST_H_WORD]     = 0x10001FF0,
        [BST_H_NUMBER]   = 0x10009440, [BST_H_DOTTED]   = 0x100020A0,
        [BST_H_SEP]      = 0x1000F140, [BST_H_GROUPS]   = 0x10009CC0,
        [BST_H_SEPNUM]   = 0x100095D0, [BST_H_MONEY]    = 0x10003410,
        [BST_H_DASH2]    = 0x1000BF80, [BST_H_ORDINAL]  = 0x1000ED10,
        [BST_H_ORDEMIT]  = 0x1000A830, [BST_H_PUNCTOUT] = 0x1000B9F0,
        [BST_H_DOTOUT]   = 0x1000BE30,
    },
    .s = {
        [BST_S_POINT]     = 0x100799F4, [BST_S_DOLLARS] = 0x100799D4, [BST_S_DOLLAR] = 0x100799D0,
        [BST_S_AND]       = 0x100799D8, [BST_S_CENTS]    = 0x100799E4,
        [BST_S_ORD_ST]    = 0x10079A20, [BST_S_ORD_ND]   = 0x10079A24,
        [BST_S_ORD_RD]    = 0x10079A28, [BST_S_ORD_TIETH]= 0x10079A34,
        [BST_S_ORD_FIFTH] = 0x10079A2C, [BST_S_ORD_FIRST]= 0x10079A30,
        [BST_S_ORD_TH]    = 0x10079A38, [BST_S_GRPSEP]   = 0x10079A40,
        [BST_S_PLURAL]    = 0x10079A30, [BST_S_DIGITS]   = 0x10081B54,
        [BST_S_TENS]      = 0x10081B7C, [BST_S_TEENS]    = 0x10081BA4,
        [BST_S_SCALES]    = 0x10081C8C, [BST_S_OH]       = 0x100799F0,
        [BST_S_HUNDRED]   = 0x100799E8, [BST_S_ZERO]     = 0x10081C14,
    },
};

/* The other twelve 2006 builds, carried over from the English one by
   tools/analysis/mkmap06.py. Each comment says how many of its functions
   paired and how many addresses that carried; what did not carry is named
   after the directory it belongs to, and left zero. */

/* ARA: 184 functions matched, 2824 addresses carried */
const bst_tabmap BST_MAP_2006_ARA = {
    .tail_chars        = { '.', ' ', ' ' },
    .num_silent        = 1,
    .pair_prescan      = 1,
    .syl_free          = 1,
    .mark_kind         = 4,
    .no_unrelease      = 1,
    .no_glottal        = 1,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .vdur_kind     = 1,
    .trn_kind      = 2,
    .stress_kind   = 4,
    .ph_kind       = 11,
    .join_words    = 1,
    .vdur_slow     = 0x41,
    .trn_whole     = 1,
    .pair_class = {
        0, 2, 1, 7, 7, 7, 7, 7, 2, 2, 2, 2, 7, 7, 7, 7,
        1, 1, 7, 7, 7, 7, 2, 2, 7, 7, 7, 7, 2, 7, 7, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .no_suffix     = 1,
    .close_pause   = 7,
    .comma_ends_text = 1, .punct_dropped = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .xlat_kind         = 1,
    .xlat_first        = 0x10023FBF,
    .xlat_second       = 0x10024027,
    .xlat_map = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E,
        0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E,
        0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E,
        0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E,
        0x2E, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5,
        0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0x2E, 0xED, 0xEE, 0xEF, 0xF0, 0x2E, 0xF2, 0xF3, 0xF4,
        0x2E, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFB, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E, 0xFC, 0xFD, 0x2E, 0x2E,
        0x9B, 0x9F, 0xA3, 0x99, 0x2E, 0x9D, 0xA1, 0x2E, 0x98, 0x2E, 0x97, 0x2E, 0x2E, 0x2E, 0x2E, 0x2E,
    },
    .code_shift        = 4,
    .chattr            = 0x100218D0,
    .letterattr        = 0x100215D0,
    .casemap           = 0x100217D0,
    .symmap            = 0x100216D0,
    .dict_silent       = 'o',
    .phattr1           = 0x100210B8,
    .phattr2           = 0x1002113C,
    .classtab          = 0x1001F93C,
    .exctab            = 0x1001F944,
    .basedur           = 0x1001F91C,
    .coefgain          = 0x1001F518,
    .tokstates         = 0x10020968,
    .names             = 0x100211C0,
    .code_medial       = 0x10021A44,
    .code_initial      = 0x100219D8,
    .ph_single         = 0x1001F334,
    .ph_pair           = 0x1001F3E0,
    .suffix_ptrs       = 0x100219D4,
    .suffix_n       = 1,
    .suffix_max       = 0,
    .lts_index         = 0x10022B60,
    .dispatch          = 0x10022A74,
    .rules             = 0x10022018,
    .patterns          = 0x10021AB0,
    .outputs           = 0x10022860,
    .trie_desc         = 0x10020908,
    .modmap            = 0x1001F94C,
    .modtab            = 0x1001F950,
    .trans_pitch    = 0x10020418,
    .vowel_dur      = 0x10020694,
    .stress_num        = 0x100208F8,
    .stress_add        = 0x100208E4,
    .sound_add         = 0x10020878,
    .diph_records      = 0x10017F68,
    .diph_offsets      = 0x1001DD28,
    .diph_offsets_end  = 0x1001EF8C,
    .voices            = 0x100140E8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x1001FA08, 0x1001FB5C, 0x1001FA44, 0x1001FA54, 0x1001FB64,
        0x1001FB6C, 0x1001FB74, 0x1001FB7C, 0x1001FB84, 0x1001FA6C,
        0x1001FB8C, 0x1001FA7C, 0x1001FB94, 0x1001FA8C, 0x1001FB50,
    },
    .bucket_data = {
        0x1001F9F8, 0x1001FB58, 0x1001FA10, 0x1001FA4C, 0x1001FB60,
        0x1001FB68, 0x1001FB70, 0x1001FB78, 0x1001FB80, 0x1001FA5C,
        0x1001FB88, 0x1001FA74, 0x1001FB90, 0x1001FA84, 0x1001FA94,
    },
    .h = {
        [BST_H_APOS] = 0x1000DF20,
        [BST_H_APOSDOT] = 0x1000DF00,
        [BST_H_CURRENCY] = 0x1000DD30,
        [BST_H_DASH] = 0x1000DD70,
        [BST_H_DASH2] = 0x1000B0B0,
        [BST_H_DEL] = 0x1000DE20,
        [BST_H_DIGIT] = 0x1000DC90,
        [BST_H_DOT] = 0x1000DD20,
        [BST_H_DOTOUT] = 0x1000AF40,
        [BST_H_DOTTED] = 0x10001B30,
        [BST_H_EAT1] = 0x1000DCD0,
        [BST_H_EAT2] = 0x1000E230,
        [BST_H_EAT3] = 0x1000E240,
        [BST_H_EXPONENT] = 0x1000DDD0,
        [BST_H_GROUPS] = 0x10008E20,
        [BST_H_HIGH] = 0x1000DDC0,
        [BST_H_HIGHOUT] = 0x10001A40,
        [BST_H_HIGH2] = 0x1000DE40,
        [BST_H_LETTER] = 0x1000DC70,
        [BST_H_MODE1] = 0x1000DE00,
        [BST_H_MODE2] = 0x1000DE10,
        [BST_H_MONEY] = 0x100029F0,
        [BST_H_NUMBER] = 0x100085A0,
        [BST_H_OPENER] = 0x1000DE50,
        [BST_H_ORDEMIT] = 0x100088C0,
        [BST_H_ORDINAL] = 0x100088C0,
        [BST_H_POSSESS] = 0x1000DF30,
        [BST_H_PUNCT] = 0x1000DD50,
        [BST_H_PUNCTOUT] = 0x1000AB00,
        [BST_H_SEP] = 0x1000E0C0,
        [BST_H_SEPNUM] = 0x10008730,
        [BST_H_SPACE] = 0x1000DCE0,
        [BST_H_TILDE] = 0x1000DEB0,
        [BST_H_WORD] = 0x10001A80,
    },
    .s = {
        [BST_S_AND] = 0x100207E8,
        [BST_S_CENTS] = 0x100207F4,
        [BST_S_DIGITS] = 0x100213E8,
        [BST_S_DOLLARS] = 0x100207E4, [BST_S_DOLLAR] = 0x100207E0,
        [BST_S_GRPSEP] = 0x10020850,
        [BST_S_HUNDRED] = 0x100207F8,
        [BST_S_OH] = 0x10020800,
        [BST_S_ORD_FIFTH] = 0x1002083C,
        [BST_S_ORD_FIRST] = 0x10020840,
        [BST_S_ORD_ND] = 0x10020834,
        [BST_S_ORD_RD] = 0x10020838,
        [BST_S_ORD_ST] = 0x10020830,
        [BST_S_ORD_TH] = 0x10020848,
        [BST_S_ORD_TIETH] = 0x10020844,
        [BST_S_PLURAL] = 0x10020840,
        [BST_S_POINT] = 0x10020804,
        [BST_S_SCALES] = 0x10021520,
        [BST_S_TEENS] = 0x10021438,
        [BST_S_TENS] = 0x10021410,
        [BST_S_ZERO] = 0x100214A8,
    },
};
/* DUT: 191 functions matched, 2878 addresses carried */
const bst_tabmap BST_MAP_2006_DUT = {
    .tail_chars        = { '.', ' ', ' ' },
    .pair_prescan      = 1,
    .no_unrelease      = 1,
    .walk_stop         = 1,
    .place_forward     = 1,
    .prefixes          = 0x10033918,
    .prefix_n          = 32,
    .prefix_max        = 6,
    .syl_weak          = { 0x81, 0x18 },
    .mark_kind         = 5,
    .no_glottal        = 1,
    .num_scale_kind    = 3,
    .no_weak_mark      = 1,
    .num_kind          = 3,
    .num_group_kind    = 3,
    .num_two_kind      = 1,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .vdur_kind     = 1,
    .vdur_slow     = 0x41,
    .trn_kind      = 2,
    .ph_kind       = 4,
    .in_map = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x20,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x82, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
        0x85, 0xC1, 0x83, 0xC3, 0x84, 0xC5, 0xC6, 0xC7, 0x8A, 0x82, 0x88, 0x89, 0xCC, 0xCD, 0x8C, 0x8B,
        0xD0, 0xD1, 0xD2, 0xD3, 0x93, 0xD5, 0x94, 0xD7, 0xD8, 0xD9, 0xDA, 0x96, 0x81, 0xDD, 0xDE, 0xDF,
        0x85, 0xE1, 0x83, 0xE3, 0x84, 0xE5, 0xE6, 0xE7, 0x8A, 0x82, 0x88, 0x89, 0xEC, 0xED, 0x8C, 0x8B,
        0xF0, 0xF1, 0xF2, 0xF3, 0x93, 0xF5, 0x94, 0xF7, 0xF8, 0xF9, 0xFA, 0x96, 0x81, 0xFD, 0xFE, 0xFF,
    },
    .trn_whole     = 1,
    .pair_class = {
        0, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 1, 7, 7, 7, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0,
    },
    .no_suffix     = 1,
    .close_pause   = 1,
    .comma_ends_text = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .code_shift        = 9,
    .chattr            = 0x100337D4,
    .letterattr        = 0x100334D4,
    .casemap           = 0x100336D4,
    .symmap            = 0x100335D4,
    .dict_silent       = 'o',
    .phattr1           = 0x10032FDC,
    .phattr2           = 0x10033064,
    .classtab          = 0x1001FD28,
    .exctab            = 0x1001FD30,
    .basedur           = 0x1001FD08,
    .coefgain          = 0x1001F904,
    .tokstates         = 0x10032878,
    .names             = 0x100330EC,
    .code_medial       = 0x10033998,
    .code_initial      = 0x10033A04,
    .ph_single         = 0x1001F720,
    .ph_pair           = 0x1001F7CC,
    .suffix_ptrs       = 0x100338D4,
    .suffix_n       = 17,
    .suffix_max       = 6,
    .lts_index         = 0x1003687C,
    .dispatch          = 0x1003655C,
    .rules             = 0x10034858,
    .patterns          = 0x10033A70,
    .outputs           = 0x10035BA8,
    .trie_desc         = 0x1002F088,
    .modmap            = 0x1001FD38,
    .modtab            = 0x1001FD54,
    .trans_pitch    = 0x1002EB3C,
    .vowel_dur      = 0x1002EDF4,
    .stress_num        = 0x1002F07C,
    .stress_add        = 0x1002F068,
    .sound_add         = 0x1002EFF4,
    .diph_records      = 0x10017F08,
    .diph_offsets      = 0x1001DCC8,
    .diph_offsets_end  = 0x1001EF2C,
    .voices            = 0x100130E8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x100227E8, 0x10022F14, 0x100238EC, 0x10025C64, 0x10027068,
        0x1002812C, 0x100282F0, 0x10028B78, 0x10029018, 0x10029324,
        0x10029400, 0x1002985C, 0x1002B164, 0x1002C10C, 0x1002E250,
    },
    .bucket_data = {
        0x1001FE20, 0x10022870, 0x10022F30, 0x10023910, 0x10025CD8,
        0x100270A4, 0x10028168, 0x100282FC, 0x10028B98, 0x1002902C,
        0x10029334, 0x10029408, 0x10029870, 0x1002B1BC, 0x1002C140,
    },
    .h = {
        [BST_H_APOS] = 0x1000D1F0,
        [BST_H_APOSDOT] = 0x1000D1D0,
        [BST_H_CURRENCY] = 0x1000D000,
        [BST_H_DASH] = 0x1000D040,
        [BST_H_DASH2] = 0x1000A550,
        [BST_H_DEL] = 0x1000D0F0,
        [BST_H_DIGIT] = 0x1000CF90,
        [BST_H_DOT] = 0x1000CFF0,
        [BST_H_DOTOUT] = 0x1000A400,
        [BST_H_DOTTED] = 0x10001CB0,
        [BST_H_EAT1] = 0x1000CFD0,
        [BST_H_EAT2] = 0x1000D500,
        [BST_H_EAT3] = 0x1000D510,
        [BST_H_EXPONENT] = 0x1000D0A0,
        [BST_H_GROUPS] = 0x10008390,
        [BST_H_HIGH] = 0x1000D090,
        [BST_H_HIGHOUT] = 0x10001BC0,
        [BST_H_HIGH2] = 0x1000D110,
        [BST_H_LETTER] = 0x1000CF70,
        [BST_H_MODE1] = 0x1000D0D0,
        [BST_H_MODE2] = 0x1000D0E0,
        [BST_H_MONEY] = 0x10002C40,
        [BST_H_NUMBER] = 0x10007B20,
        [BST_H_OPENER] = 0x1000D120,
        [BST_H_ORDEMIT] = 0x10001F20,
        [BST_H_ORDINAL] = 0x10001F20,
        [BST_H_POSSESS] = 0x1000D200,
        [BST_H_PUNCT] = 0x1000D020,
        [BST_H_PUNCTOUT] = 0x10009FC0,
        [BST_H_SEP] = 0x1000D390,
        [BST_H_SEPNUM] = 0x10007CB0,
        [BST_H_SPACE] = 0x1000CFE0,
        [BST_H_TILDE] = 0x1000D180,
        [BST_H_WORD] = 0x10001C00,
    },
    .s = {
        [BST_S_AND] = 0x1002EF64,
        [BST_S_CENTS] = 0x1002EF70,
        [BST_S_DIGITS] = 0x10033314,
        [BST_S_DOLLARS] = 0x1002EF60, [BST_S_DOLLAR] = 0x1002EF5C,
        [BST_S_GRPSEP] = 0x1002EFCC,
        [BST_S_HUNDRED] = 0x1002EF74,
        [BST_S_ONE_ALT] = 0x1002EF78,
        [BST_S_OH] = 0x1002EF7C,
        [BST_S_ORD_FIFTH] = 0x1002EFB8,
        [BST_S_ORD_FIRST] = 0x1002EFBC,
        [BST_S_ORD_ND] = 0x1002EFB0,
        [BST_S_ORD_RD] = 0x1002EFB4,
        [BST_S_ORD_ST] = 0x1002EFAC,
        [BST_S_ORD_TH] = 0x1002EFC4,
        [BST_S_ORD_TIETH] = 0x1002EFC0,
        [BST_S_PLURAL] = 0x1002EFBC,
        [BST_S_POINT] = 0x1002EF80,
        [BST_S_SCALES] = 0x1003344C,
        [BST_S_TEENS] = 0x10033364,
        [BST_S_TENS] = 0x1003333C,
        [BST_S_ZERO] = 0x100333D4,
    },
};
/* FRE: 187 functions matched, 2752 addresses carried */
const bst_tabmap BST_MAP_2006_FRE = {
    .tail_chars        = { '.', ' ', ' ' },
    .num_kind          = 4,
    .num_group_kind    = 3,
    .syl_free          = 1,
    .num_two_kind      = 3,
    .pair_prescan      = 1,
    .no_unrelease      = 1,
    .no_glottal        = 1,
    .num_scale_kind    = 3,
    .num_liaison       = 0x59,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .trn_whole     = 1,
    .pair_class = {
        0, 7, 7, 7, 2, 2, 2, 1, 1, 1, 1, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .fall_on_next  = 1,
    .no_suffix     = 1,
    .close_pause   = 1,
    .comma_ends_text = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .in_map = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x40, 0x41, 0x42, 0x73, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x61, 0x62, 0x73, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x6F, 0x8D, 0x8E, 0x8F,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x6F, 0x9D, 0x9E, 0x9F,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
        0x85, 0xC1, 0x83, 0xC3, 0xC4, 0xC5, 0xC6, 0x73, 0x8A, 0x82, 0x88, 0x89, 0xCC, 0xCD, 0x8C, 0x8B,
        0xD0, 0xD1, 0xD2, 0xD3, 0x93, 0xD5, 0xD6, 0xD7, 0xD8, 0x97, 0xDA, 0x96, 0xDC, 0xDD, 0xDE, 0xDF,
        0x85, 0xE1, 0x83, 0xE3, 0xE4, 0xE5, 0xE6, 0x73, 0x8A, 0x82, 0x88, 0x89, 0xEC, 0xED, 0x8C, 0x8B,
        0xF0, 0xF1, 0xF2, 0xF3, 0x93, 0xF5, 0xF6, 0xF7, 0xF8, 0x97, 0xFA, 0x96, 0xFC, 0xFD, 0xFE, 0xFF,
    },
    .in_map2 = { [0x8C] = 0x65, [0x9C] = 0x65 },
    .text_fix_kind = 1,
    .pat_ext       = 1,
    .vdur_kind     = 4,
    .trn_kind      = 4,
    .ph_kind       = 6,
    .run_stop_skip = 0x26,
    .run_before_always = 1,
    .acc_kind      = 1,
    .stress_rule   = 4,
    .punct_s8      = { 5, 3, 9, 1, 9, 3, 9, 5, 9 },
    .punct_s9      = { 0, 0x4C, 0x48, 0, 0x48, 0x4C, 0x48, 0, 0x48 },
    .code_shift        = -8,
    .cmd_code          = 0x6E,
    .chattr            = 0x100203C8,
    .letterattr        = 0x100200C8,
    .casemap           = 0x100202C8,
    .symmap            = 0x100201C8,
    .dict_silent       = 's',
    .phattr1           = 0x1001FC00,
    .phattr2           = 0x1001FC70,
    .classtab          = 0x1001B204,
    .exctab            = 0x1001B20C,
    .basedur           = 0x1001B1E4,
    .coefgain          = 0x1001ADE0,
    .tokstates         = 0x1001F4C8,
    .names             = 0x1001FCE0,
    .code_medial       = 0x10020550,
    .code_initial      = 0x100204E4,
    .ph_single         = 0x1001ABFC,
    .ph_pair           = 0x1001ACA8,
    .suffix_ptrs       = 0x100204E0,
    .suffix_n       = 1,
    .suffix_max       = 0,
    .lts_index         = 0x10023230,
    .dispatch          = 0x10023010,
    .rules             = 0x100214C8,
    .patterns          = 0x100205BC,
    .outputs           = 0x10022A50,
    .trie_desc         = 0x1001BF18,
    .modmap            = 0x1001B214,
    .modtab            = 0x1001B218,
    .trans_pitch    = 0x1001BB14,
    .vowel_dur      = 0x1001BD00,
    .stress_num        = 0x1001BF08,
    .stress_add        = 0x1001BEF4,
    .sound_add         = 0x1001BEA0,
    .diph_records      = 0x100167F8,
    .diph_offsets      = 0x10019EA8,
    .diph_offsets_end  = 0x1001B10C,
    .voices            = 0x100140E8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x1001B228, 0x1001B230, 0x1001B238, 0x1001B240, 0x1001B248,
        0x1001B250, 0x1001B258, 0x1001B260, 0x1001B268, 0x1001B270,
        0x1001B278, 0x1001B280, 0x1001B220, 0x1001B288, 0x1001B290,
    },
    .bucket_data = {
        0x1001B224, 0x1001B22C, 0x1001B234, 0x1001B23C, 0x1001B244,
        0x1001B24C, 0x1001B254, 0x1001B25C, 0x1001B264, 0x1001B26C,
        0x1001B274, 0x1001B27C, 0x1001B21C, 0x1001B284, 0x1001B28C,
    },
    .h = {
        [BST_H_APOS] = 0x1000D7F0,
        [BST_H_APOSDOT] = 0x1000D7D0,
        [BST_H_CURRENCY] = 0x1000D4F0,
        [BST_H_DASH] = 0x1000D530,
        [BST_H_DASHLETTER] = 0x10002330,
        [BST_H_DASH2] = 0x1000AE20,
        [BST_H_DEL] = 0x1000D6F0,
        [BST_H_DIGIT] = 0x1000D480,
        [BST_H_DOT] = 0x1000D4E0,
        [BST_H_DOTOUT] = 0x1000ACD0,
        [BST_H_DOTTED] = 0x10001FC0,
        [BST_H_EAT1] = 0x1000D4C0,
        [BST_H_EAT2] = 0x1000DC60,
        [BST_H_EAT3] = 0x1000DC70,
        [BST_H_EXPONENT] = 0x1000D6A0,
        [BST_H_GROUPS] = 0x100089F0,
        [BST_H_HIGH] = 0x1000D580,
        [BST_H_HIGHOUT] = 0x10001ED0,
        [BST_H_HIGH2] = 0x1000D710,
        [BST_H_LETTER] = 0x1000D460,
        [BST_H_MODE1] = 0x1000D6D0,
        [BST_H_MODE2] = 0x1000D6E0,
        [BST_H_MONEY] = 0x100030F0,
        [BST_H_NUMBER] = 0x10008160,
        [BST_H_OPENER] = 0x1000D720,
        [BST_H_ORDEMIT] = 0x10009710,
        [BST_H_ORDINAL] = 0x1000D590,
        [BST_H_POSSESS] = 0x1000D800,
        [BST_H_PUNCT] = 0x1000D510,
        [BST_H_PUNCTOUT] = 0x1000A890,
        [BST_H_SEP] = 0x1000D990,
        [BST_H_SEPNUM] = 0x100082F0,
        [BST_H_SPACE] = 0x1000D4D0,
        [BST_H_TILDE] = 0x1000D780,
        [BST_H_WORD] = 0x10001F10,
    },
    .s = {
        [BST_S_AND] = 0x1001BE0C,
        [BST_S_CENTS] = 0x1001BE18,
        [BST_S_DIGITS] = 0x1001FF08,
        [BST_S_DOLLARS] = 0x1001BE08, [BST_S_DOLLAR] = 0x1001BE04,
        [BST_S_GRPSEP] = 0x1001BE74,
        [BST_S_HUNDRED] = 0x1001BE1C,
        [BST_S_OH] = 0x1001BE24,
        [BST_S_ORD_FIFTH] = 0x1001BE64,
        [BST_S_ORD_FIRST] = 0x1001BE68,
        [BST_S_ORD_ND] = 0x1001BE5C,
        [BST_S_ORD_RD] = 0x1001BE60,
        [BST_S_ORD_ST] = 0x1001BE58,
        [BST_S_ORD_TH] = 0x1001BE6C,
        [BST_S_ORD_TIETH] = 0x1001BE68,
        [BST_S_PLURAL] = 0x1001BE68,
        [BST_S_POINT] = 0x1001BE28,
        [BST_S_SCALES] = 0x10020040,
        [BST_S_TEENS] = 0x1001FF58,
        [BST_S_TENS] = 0x1001FF30,
        [BST_S_ZERO] = 0x1001FFC8,
    },
};
/* GER: 192 functions matched, 2850 addresses carried */
const bst_tabmap BST_MAP_2006_GER = {
    .tail_chars        = { '.', ' ', ' ' },
    .pair_prescan      = 1,
    .no_unrelease      = 1,
    .walk_stop         = 1,
    .place_forward     = 1,
    .prefixes          = 0x1003E884,
    .prefix_n          = 30,
    .prefix_max        = 8,
    .split_tab         = 0x1002103C,
    .syl_weak          = { 0x80, 0x81 },
    .mark_kind         = 5,
    .no_glottal        = 1,
    .num_scale_kind    = 4,
    .no_weak_mark      = 1,
    .num_kind          = 2,
    .num_spell_fives   = 1,
    .num_group_kind    = 2,
    .num_two_kind      = 1,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .ph_kind       = 5,
    .fall_code     = 0x3C,
    .strip_kind    = 1,
    .suffix_tail   = { [0] = 0x10, [1] = 0x1B, [2] = 0x0C, [3] = 0x0A },
    .in_map = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x63, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x63, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
        0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
        0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0x73,
        0xE0, 0xE1, 0xE2, 0xE3, 0x84, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
        0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0x94, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0x81, 0xFD, 0xFE, 0xFF,
    },
    .in_map2 = { [0x68] = 0x68, [0x48] = 0x68 },
    .trn_kind      = 3,
    .vdur_kind     = 3,
    .trn_whole     = 1,
    .pair_class = {
        0, 7, 2, 7, 2, 7, 2, 2, 2, 2, 1, 1, 1, 1, 1, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 1, 7, 7, 7, 7, 2,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0,
    },
    .close_pause   = 1,
    .comma_ends_text = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .code_shift        = 8,
    .chattr            = 0x1003E738,
    .letterattr        = 0x1003E438,
    .casemap           = 0x1003E638,
    .symmap            = 0x1003E538,
    .dict_silent       = 'n',
    .phattr1           = 0x1003DF40,
    .phattr2           = 0x1003DFC8,
    .classtab          = 0x1002102C,
    .exctab            = 0x10021034,
    .basedur           = 0x1002100C,
    .coefgain          = 0x10020C08,
    .tokstates         = 0x1003D7E0,
    .names             = 0x1003E050,
    .code_medial       = 0x1003E8FC,
    .code_initial      = 0x1003E968,
    .ph_single         = 0x10020A24,
    .ph_pair           = 0x10020AD0,
    .suffix_ptrs       = 0x1003E838,
    .suffix_n       = 19,
    .suffix_max       = 6,
    .lts_index         = 0x1004107C,
    .dispatch          = 0x10040DD4,
    .rules             = 0x1003F520,
    .patterns          = 0x1003E9D4,
    .outputs           = 0x100405C8,
    .trie_desc         = 0x1002CF78,
    .modmap            = 0x10021B28,
    .modtab            = 0x10021B4C,
    .trans_pitch    = 0x1002CA38,
    .vowel_dur      = 0x1002CCE4,
    .stress_num        = 0x1002CF68,
    .stress_add        = 0x1002CF54,
    .sound_add         = 0x1002CEE0,
    .diph_records      = 0x10018F08,
    .diph_offsets      = 0x1001F0B0,
    .diph_offsets_end  = 0x10020314,
    .voices            = 0x100140E8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x10023D94, 0x10024A6C, 0x10025494, 0x10025B10, 0x10026534,
        0x10026E68, 0x10027C90, 0x10028858, 0x100290A4, 0x100296C8,
        0x10029FA4, 0x1002A41C, 0x1002AC2C, 0x1002B1DC, 0x1002C180,
    },
    .bucket_data = {
        0x10021C20, 0x10023E04, 0x10024A98, 0x100254BC, 0x10025B28,
        0x10026558, 0x10026E88, 0x10027CBC, 0x10028884, 0x100290C4,
        0x100296E0, 0x10029FC4, 0x1002A430, 0x1002AC4C, 0x1002B1F4,
    },
    .h = {
        [BST_H_APOS] = 0x1000E350,
        [BST_H_APOSDOT] = 0x1000E330,
        [BST_H_CURRENCY] = 0x1000E160,
        [BST_H_DASH] = 0x1000E1A0,
        [BST_H_DASH2] = 0x1000B620,
        [BST_H_DEL] = 0x1000E250,
        [BST_H_DIGIT] = 0x1000E0F0,
        [BST_H_DOT] = 0x1000E150,
        [BST_H_DOTOUT] = 0x1000B4D0,
        [BST_H_DOTTED] = 0x10001E90,
        [BST_H_EAT1] = 0x1000E130,
        [BST_H_EAT2] = 0x1000E660,
        [BST_H_EAT3] = 0x1000E670,
        [BST_H_EXPONENT] = 0x1000E200,
        [BST_H_GROUPS] = 0x100093E0,
        [BST_H_HIGH] = 0x1000E1F0,
        [BST_H_HIGHOUT] = 0x10001DA0,
        [BST_H_HIGH2] = 0x1000E270,
        [BST_H_LETTER] = 0x1000E0D0,
        [BST_H_MODE1] = 0x1000E230,
        [BST_H_MODE2] = 0x1000E240,
        [BST_H_MONEY] = 0x100031B0,
        [BST_H_NUMBER] = 0x10008B60,
        [BST_H_OPENER] = 0x1000E280,
        [BST_H_ORDEMIT] = 0x10002100,
        [BST_H_ORDINAL] = 0x10002100,
        [BST_H_POSSESS] = 0x1000E360,
        [BST_H_PUNCT] = 0x1000E180,
        [BST_H_PUNCTOUT] = 0x1000B090,
        [BST_H_SEP] = 0x1000E4F0,
        [BST_H_SEPNUM] = 0x10008CF0,
        [BST_H_SPACE] = 0x1000E140,
        [BST_H_TILDE] = 0x1000E2E0,
        [BST_H_WORD] = 0x10001DE0,
    },
    .s = {
        [BST_S_AND] = 0x1002CE50,
        [BST_S_CENTS] = 0x1002CE5C,
        [BST_S_DIGITS] = 0x1003E278,
        [BST_S_DOLLARS] = 0x1002CE4C, [BST_S_DOLLAR] = 0x1002CE48,
        [BST_S_GRPSEP] = 0x1002CEB8,
        [BST_S_HUNDRED] = 0x1002CE60,
        [BST_S_ONE_ALT] = 0x1002CE64,
        [BST_S_ONE_BIG] = 0x1002CE68,
        [BST_S_OH] = 0x1002CE68,
        [BST_S_ORD_FIFTH] = 0x1002CEA4,
        [BST_S_ORD_FIRST] = 0x1002CEA8,
        [BST_S_ORD_ND] = 0x1002CE9C,
        [BST_S_ORD_RD] = 0x1002CEA0,
        [BST_S_ORD_ST] = 0x1002CE98,
        [BST_S_ORD_TH] = 0x1002CEB0,
        [BST_S_ORD_TIETH] = 0x1002CEAC,
        [BST_S_PLURAL] = 0x1002CEA8,
        [BST_S_POINT] = 0x1002CE6C,
        [BST_S_SCALES] = 0x1003E3B0,
        [BST_S_TEENS] = 0x1003E2C8,
        [BST_S_TENS] = 0x1003E2A0,
        [BST_S_ZERO] = 0x1003E338,
    },
};
/* GRE: 190 functions matched, 2861 addresses carried */
const bst_tabmap BST_MAP_2006_GRE = {
    .tail_chars        = { '.', ' ', ' ' },
    .pair_prescan      = 1,
    .no_unrelease      = 1,
    .syl_free          = 1,
    .mark_kind         = 2,
    .mark_78_take      = 1,
    .no_glottal        = 1,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .in_map = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
        0xA0, 0xA1, 0xA0, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0x82, 0x88, 0x69, 0xBB, 0xA2, 0xBD, 0x75, 0x93,
        0xC0, 0x61, 0x76, 0x67, 0x64, 0x65, 0x7A, 0x8A, 0x68, 0x69, 0x6B, 0x6C, 0x6D, 0x6E, 0x63, 0x6F,
        0x70, 0x72, 0xD2, 0x73, 0x74, 0x75, 0x66, 0x78, 0x71, 0x95, 0xDA, 0xDB, 0xA0, 0x82, 0x88, 0x69,
        0xE0, 0x61, 0x76, 0x67, 0x64, 0x65, 0x7A, 0x8A, 0x68, 0x69, 0x6B, 0x6C, 0x6D, 0x6E, 0x63, 0x6F,
        0x70, 0x72, 0x73, 0x73, 0x74, 0x75, 0x66, 0x78, 0x71, 0x95, 0xFA, 0xFB, 0xA2, 0x75, 0x93, 0xFF,
    },
    .vdur_slow     = 0x41,
    .trn_kind      = 2,
    .ph_kind       = 8,
    .stress_kind   = 1,
    .vdur_kind     = 1,
    .trn_whole     = 1,
    .pair_class = {
        0, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 7, 7, 7, 7, 8, 1, 8, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .no_suffix     = 1,
    .close_pause   = 7,
    .comma_ends_text = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .code_shift        = -7,
    .chattr            = 0x1001FE4C,
    .letterattr        = 0x1001FB4C,
    .casemap           = 0x1001FD4C,
    .symmap            = 0x1001FC4C,
    .dict_silent       = 'o',
    .phattr1           = 0x1001F674,
    .phattr2           = 0x1001F6EC,
    .classtab          = 0x1001E128,
    .exctab            = 0x1001E130,
    .basedur           = 0x1001E108,
    .coefgain          = 0x1001DD04,
    .tokstates         = 0x1001EF50,
    .names             = 0x1001F764,
    .code_medial       = 0x1001FFC0,
    .code_initial      = 0x1001FF54,
    .ph_single         = 0x1001DB20,
    .ph_pair           = 0x1001DBCC,
    .suffix_ptrs       = 0x1001FF50,
    .suffix_n       = 1,
    .suffix_max       = 0,
    .lts_index         = 0x10020784,
    .dispatch          = 0x10020658,
    .rules             = 0x100201C0,
    .patterns          = 0x1002002C,
    .outputs           = 0x10020550,
    .trie_desc         = 0x1001EEC8,
    .modmap            = 0x1001E1B8,
    .modtab            = 0x1001E1BC,
    .trans_pitch    = 0x1001EAB8,
    .vowel_dur      = 0x1001ECB0,
    .stress_num        = 0x1001EEB8,
    .stress_add        = 0x1001EEA4,
    .sound_add         = 0x1001EE50,
    .diph_records      = 0x10016F68,
    .diph_offsets      = 0x1001CD28,
    .diph_offsets_end  = 0x1001DF8C,
    .voices            = 0x100130E8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x1001E1CC, 0x1001E1D4, 0x1001E1DC, 0x1001E1E4, 0x1001E1EC,
        0x1001E1F4, 0x1001E1FC, 0x1001E204, 0x1001E20C, 0x1001E214,
        0x1001E21C, 0x1001E224, 0x1001E1C4, 0x1001E22C, 0x1001E234,
    },
    .bucket_data = {
        0x1001E1C8, 0x1001E1D0, 0x1001E1D8, 0x1001E1E0, 0x1001E1E8,
        0x1001E1F0, 0x1001E1F8, 0x1001E200, 0x1001E208, 0x1001E210,
        0x1001E218, 0x1001E220, 0x1001E1C0, 0x1001E228, 0x1001E230,
    },
    .h = {
        [BST_H_APOS] = 0x1000D230,
        [BST_H_APOSDOT] = 0x1000D210,
        [BST_H_CURRENCY] = 0x1000D040,
        [BST_H_DASH] = 0x1000D080,
        [BST_H_DASH2] = 0x1000A580,
        [BST_H_DEL] = 0x1000D130,
        [BST_H_DIGIT] = 0x1000CFD0,
        [BST_H_DOT] = 0x1000D030,
        [BST_H_DOTOUT] = 0x1000A430,
        [BST_H_DOTTED] = 0x10001D50,
        [BST_H_EAT1] = 0x1000D010,
        [BST_H_EAT2] = 0x1000D540,
        [BST_H_EAT3] = 0x1000D550,
        [BST_H_EXPONENT] = 0x1000D0E0,
        [BST_H_GROUPS] = 0x10008450,
        [BST_H_HIGH] = 0x1000D0D0,
        [BST_H_HIGHOUT] = 0x10001C60,
        [BST_H_HIGH2] = 0x1000D150,
        [BST_H_LETTER] = 0x1000CFB0,
        [BST_H_MODE1] = 0x1000D110,
        [BST_H_MODE2] = 0x1000D120,
        [BST_H_MONEY] = 0x10002CE0,
        [BST_H_NUMBER] = 0x10007BE0,
        [BST_H_OPENER] = 0x1000D160,
        [BST_H_ORDEMIT] = 0x10001FC0,
        [BST_H_ORDINAL] = 0x10001FC0,
        [BST_H_POSSESS] = 0x1000D240,
        [BST_H_PUNCT] = 0x1000D060,
        [BST_H_PUNCTOUT] = 0x10009FF0,
        [BST_H_SEP] = 0x1000D3D0,
        [BST_H_SEPNUM] = 0x10007D70,
        [BST_H_SPACE] = 0x1000D020,
        [BST_H_TILDE] = 0x1000D1C0,
        [BST_H_WORD] = 0x10001CA0,
    },
    .s = {
        [BST_S_AND] = 0x1001EDC0,
        [BST_S_CENTS] = 0x1001EDCC,
        [BST_S_DIGITS] = 0x1001F98C,
        [BST_S_DOLLARS] = 0x1001EDBC, [BST_S_DOLLAR] = 0x1001EDB8,
        [BST_S_GRPSEP] = 0x1001EE28,
        [BST_S_HUNDRED] = 0x1001EDD0,
        [BST_S_OH] = 0x1001EDD8,
        [BST_S_ORD_FIFTH] = 0x1001EE14,
        [BST_S_ORD_FIRST] = 0x1001EE18,
        [BST_S_ORD_ND] = 0x1001EE0C,
        [BST_S_ORD_RD] = 0x1001EE10,
        [BST_S_ORD_ST] = 0x1001EE08,
        [BST_S_ORD_TH] = 0x1001EE20,
        [BST_S_ORD_TIETH] = 0x1001EE1C,
        [BST_S_PLURAL] = 0x1001EE18,
        [BST_S_POINT] = 0x1001EDDC,
        [BST_S_SCALES] = 0x1001FAC4,
        [BST_S_TEENS] = 0x1001F9DC,
        [BST_S_TENS] = 0x1001F9B4,
        [BST_S_ZERO] = 0x1001FA4C,
    },
};
/* HEB: 190 functions matched, 2842 addresses carried */
const bst_tabmap BST_MAP_2006_HEB = {
    .tail_chars        = { '.', ' ', ' ' },
    .num_group_kind    = 4,
    .num_kind          = 1,
    .syl_free          = 1,
    .mark_kind         = 2,
    .num_two_kind      = 2,
    .pair_prescan      = 1,
    .no_unrelease      = 1,
    .no_glottal        = 1,
    .acc_code11        = 0x3F,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .ph_kind       = 7,
    .stress_kind   = 1,
    .trn_kind      = 2,
    .vdur_slow     = 0x41,
    .vdur_kind     = 1,
    .trn_whole     = 1,
    .pair_class = {
        0, 2, 2, 2, 2, 2, 2, 2, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 1, 1, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .no_suffix     = 1,
    .close_pause   = 1,
    .comma_ends_text = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .code_shift        = -13,
    .chattr            = 0x100207B4,
    .letterattr        = 0x100204B4,
    .casemap           = 0x100206B4,
    .symmap            = 0x100205B4,
    .dict_silent       = 'o',
    .phattr1           = 0x1001FFEC,
    .phattr2           = 0x1002005C,
    .classtab          = 0x1001DE0C,
    .exctab            = 0x1001DE14,
    .basedur           = 0x1001DDEC,
    .coefgain          = 0x1001D9E8,
    .tokstates         = 0x1001F8E0,
    .names             = 0x100200CC,
    .code_medial       = 0x10020928,
    .code_initial      = 0x100208BC,
    .ph_single         = 0x1001D804,
    .ph_pair           = 0x1001D8B0,
    .suffix_ptrs       = 0x100208B8,
    .suffix_n       = 1,
    .suffix_max       = 0,
    .lts_index         = 0x100213E0,
    .dispatch          = 0x100212EC,
    .rules             = 0x10020C88,
    .patterns          = 0x10020994,
    .outputs           = 0x10021178,
    .trie_desc         = 0x1001F700,
    .modmap            = 0x1001DE1C,
    .modtab            = 0x1001DE24,
    .trans_pitch    = 0x1001F368,
    .vowel_dur      = 0x1001F518,
    .stress_num        = 0x1001F6F0,
    .stress_add        = 0x1001F6DC,
    .sound_add         = 0x1001F694,
    .diph_records      = 0x10016F68,
    .diph_offsets      = 0x1001CD28,
    .diph_offsets_end  = 0x1001DF8C,
    .voices            = 0x100130E8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x1001DF54, 0x1001EAAC, 0x1001EAB4, 0x1001EABC, 0x1001DF64,
        0x1001EAC4, 0x1001DFA8, 0x1001DFC0, 0x1001EACC, 0x1001DFDC,
        0x1001EAD4, 0x1001EADC, 0x1001EAE4, 0x1001E108, 0x1001EA84,
    },
    .bucket_data = {
        0x1001DE9C, 0x1001EAA8, 0x1001EAB0, 0x1001EAB8, 0x1001DF5C,
        0x1001EAC0, 0x1001DF6C, 0x1001DFB0, 0x1001EAC8, 0x1001DFC8,
        0x1001EAD0, 0x1001EAD8, 0x1001EAE0, 0x1001DFE4, 0x1001E110,
    },
    .h = {
        [BST_H_APOS] = 0x1000D720,
        [BST_H_APOSDOT] = 0x1000D700,
        [BST_H_CURRENCY] = 0x1000D520,
        [BST_H_DASH] = 0x1000D560,
        [BST_H_DASH2] = 0x1000AAA0,
        [BST_H_DEL] = 0x1000D620,
        [BST_H_DIGIT] = 0x1000D4B0,
        [BST_H_DOT] = 0x1000D510,
        [BST_H_DOTOUT] = 0x1000A950,
        [BST_H_DOTTED] = 0x10001B70,
        [BST_H_EAT1] = 0x1000D4F0,
        [BST_H_EAT2] = 0x1000DA30,
        [BST_H_EAT3] = 0x1000DA40,
        [BST_H_EXPONENT] = 0x1000D5D0,
        [BST_H_GROUPS] = 0x10008970,
        [BST_H_HIGH] = 0x1000D5B0,
        [BST_H_HIGHOUT] = 0x10001A80,
        [BST_H_HIGH2] = 0x1000D640,
        [BST_H_LETTER] = 0x1000D490,
        [BST_H_MODE1] = 0x1000D600,
        [BST_H_MODE2] = 0x1000D610,
        [BST_H_MONEY] = 0x10002A90,
        [BST_H_NUMBER] = 0x10008100,
        [BST_H_OPENER] = 0x1000D650,
        [BST_H_ORDEMIT] = 0x1000D5C0,
        [BST_H_ORDINAL] = 0x1000D5C0,
        [BST_H_POSSESS] = 0x1000D730,
        [BST_H_PUNCT] = 0x1000D540,
        [BST_H_PUNCTOUT] = 0x1000A510,
        [BST_H_SEP] = 0x1000D8C0,
        [BST_H_SEPNUM] = 0x10008290,
        [BST_H_SPACE] = 0x1000D500,
        [BST_H_TILDE] = 0x1000D6B0,
        [BST_H_WORD] = 0x10001AC0,
    },
    .s = {
        [BST_S_AND] = 0x1001F604,
        [BST_S_CENTS] = 0x1001F610,
        [BST_S_DIGITS] = 0x100202F4,
        [BST_S_DOLLARS] = 0x1001F600, [BST_S_DOLLAR] = 0x1001F5FC,
        [BST_S_GRPSEP] = 0x1001F66C,
        [BST_S_HUNDRED] = 0x1001F614,
        [BST_S_OH] = 0x1001F61C,
        [BST_S_ORD_FIFTH] = 0x1001F658,
        [BST_S_ORD_FIRST] = 0x1001F65C,
        [BST_S_ORD_ND] = 0x1001F650,
        [BST_S_ORD_RD] = 0x1001F654,
        [BST_S_ORD_ST] = 0x1001F64C,
        [BST_S_ORD_TH] = 0x1001F664,
        [BST_S_ORD_TIETH] = 0x1001F660,
        [BST_S_PLURAL] = 0x1001F65C,
        [BST_S_POINT] = 0x1001F620,
        [BST_S_SCALES] = 0x1002042C,
        [BST_S_TEENS] = 0x10020344,
        [BST_S_TENS] = 0x1002031C,
        [BST_S_ZERO] = 0x100203B4,
    },
};
/* ITA: 189 functions matched, 2859 addresses carried */
const bst_tabmap BST_MAP_2006_ITA = {
    .tail_chars        = { '.', ' ', ' ' },
    .squash            = { 'z', 'r' },
    .num_kind          = 1,
    .num_group_kind    = 3,
    .syl_free          = 1,
    .mark_emph         = 0x33,
    .no_weak_mark      = 1,
    .mark_kind         = 2,
    .num_scale_kind    = 1,
    .pair_prescan      = 1,
    .no_unrelease      = 1,
    .no_glottal        = 1,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .in_map = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
        0x85, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0x8A, 0x82, 0x88, 0xCB, 0x8D, 0xCD, 0xCE, 0xCF,
        0xD0, 0xD1, 0x95, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0x97, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
        0x85, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0x8A, 0x82, 0x88, 0xEB, 0x8D, 0xED, 0xEE, 0xEF,
        0xF0, 0xF1, 0x95, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0x97, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
    },
    .vdur_kind     = 2,
    .trn_kind      = 2,
    .trn_thin      = 2,
    .ph_kind       = 2,
    .stress_rule   = 1,
    .pair_class = {
        0, 7, 7, 7, 2, 2, 2, 7, 7, 7, 7, 1, 1, 1, 1, 1,
        7, 7, 7, 7, 7, 1, 1, 1, 7, 7, 7, 7, 6, 6, 6, 6,
        6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .no_suffix     = 1,
    .close_pause   = 1,
    .comma_ends_text = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .code_shift        = -12,
    .chattr            = 0x10026B20,
    .letterattr        = 0x10026820,
    .casemap           = 0x10026A20,
    .symmap            = 0x10026920,
    .dict_silent       = 'o',
    .phattr1           = 0x10026328,
    .phattr2           = 0x1002639C,
    .classtab          = 0x10017D3C,
    .exctab            = 0x10017D44,
    .basedur           = 0x10017D1C,
    .coefgain          = 0x10017918,
    .tokstates         = 0x10025C18,
    .names             = 0x10026410,
    .code_medial       = 0x10026C94,
    .code_initial      = 0x10026C28,
    .ph_single         = 0x10017734,
    .ph_pair           = 0x100177E0,
    .suffix_ptrs       = 0x10026C24,
    .suffix_n       = 1,
    .suffix_max       = 0,
    .lts_index         = 0x10029CB0,
    .dispatch          = 0x10029A68,
    .rules             = 0x10027D60,
    .patterns          = 0x10026D00,
    .outputs           = 0x10029090,
    .trie_desc         = 0x1001F968,
    .modmap            = 0x10017D4C,
    .modtab            = 0x10017D5C,
    .trans_pitch    = 0x1001F5BC,
    .vowel_dur      = 0x1001F778,
    .stress_num        = 0x1001F95C,
    .stress_add        = 0x1001F948,
    .sound_add         = 0x1001F8FC,
    .diph_records      = 0x10014538,
    .diph_offsets      = 0x10016C48,
    .diph_offsets_end  = 0x10017EAC,
    .voices            = 0x100130E8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x100190EC, 0x10019D68, 0x1001A65C, 0x1001B164, 0x1001BAC0,
        0x1001C540, 0x1001CB3C, 0x1001D0C0, 0x1001D7D8, 0x1001DCF8,
        0x1001E004, 0x1001E398, 0x1001E8E0, 0x1001EC44, 0x1001ED34,
    },
    .bucket_data = {
        0x10017DE8, 0x10019140, 0x10019DA0, 0x1001A684, 0x1001B198,
        0x1001BAEC, 0x1001C570, 0x1001CB58, 0x1001D0DC, 0x1001D7F8,
        0x1001DD14, 0x1001E018, 0x1001E3AC, 0x1001E8FC, 0x1001EC58,
    },
    .h = {
        [BST_H_APOS] = 0x1000D500,
        [BST_H_APOSDOT] = 0x1000D4E0,
        [BST_H_CURRENCY] = 0x1000D310,
        [BST_H_DASH] = 0x1000D350,
        [BST_H_DASH2] = 0x1000A6E0,
        [BST_H_DEL] = 0x1000D400,
        [BST_H_DIGIT] = 0x1000D2A0,
        [BST_H_DOT] = 0x1000D300,
        [BST_H_DOTOUT] = 0x1000A590,
        [BST_H_DOTTED] = 0x10001C60,
        [BST_H_EAT1] = 0x1000D2E0,
        [BST_H_EAT2] = 0x1000D810,
        [BST_H_EAT3] = 0x1000D820,
        [BST_H_EXPONENT] = 0x1000D3B0,
        [BST_H_GROUPS] = 0x10008440,
        [BST_H_HIGH] = 0x1000D3A0,
        [BST_H_HIGHOUT] = 0x10001B70,
        [BST_H_HIGH2] = 0x1000D420,
        [BST_H_LETTER] = 0x1000D280,
        [BST_H_MODE1] = 0x1000D3E0,
        [BST_H_MODE2] = 0x1000D3F0,
        [BST_H_MONEY] = 0x10002C30,
        [BST_H_NUMBER] = 0x10007BD0,
        [BST_H_OPENER] = 0x1000D430,
        [BST_H_ORDEMIT] = 0x10001ED0,
        [BST_H_ORDINAL] = 0x10001ED0,
        [BST_H_POSSESS] = 0x1000D510,
        [BST_H_PUNCT] = 0x1000D330,
        [BST_H_PUNCTOUT] = 0x1000A150,
        [BST_H_SEP] = 0x1000D6A0,
        [BST_H_SEPNUM] = 0x10007D60,
        [BST_H_SPACE] = 0x1000D2F0,
        [BST_H_TILDE] = 0x1000D490,
        [BST_H_WORD] = 0x10001BB0,
    },
    .s = {
        [BST_S_AND] = 0x1001F86C,
        [BST_S_CENTS] = 0x1001F878,
        [BST_S_DIGITS] = 0x10026638,
        [BST_S_DOLLARS] = 0x1001F868, [BST_S_DOLLAR] = 0x1001F864,
        [BST_S_GRPSEP] = 0x1001F8D4,
        [BST_S_HUNDRED] = 0x1001F87C,
        [BST_S_OH] = 0x1001F884,
        [BST_S_ORD_FIFTH] = 0x1001F8C0,
        [BST_S_ORD_FIRST] = 0x1001F8C4,
        [BST_S_ORD_ND] = 0x1001F8B8,
        [BST_S_ORD_RD] = 0x1001F8BC,
        [BST_S_ORD_ST] = 0x1001F8B4,
        [BST_S_ORD_TH] = 0x1001F8CC,
        [BST_S_ORD_TIETH] = 0x1001F8C8,
        [BST_S_PLURAL] = 0x1001F8C4,
        [BST_S_POINT] = 0x1001F888,
        [BST_S_SCALES] = 0x10026798,
        [BST_S_SCALE_PL] = 0x1001F8C0,
        [BST_S_TEENS] = 0x10026688,
        [BST_S_TENS] = 0x10026660,
        [BST_S_ZERO] = 0x100266F8,
    },
};
/* JPN: 182 functions matched, 2642 addresses carried */
const bst_tabmap BST_MAP_2006_JPN = {
    .tail_chars        = { '.', ' ', ' ' },
    .num_silent        = 1,
    .pair_prescan      = 1,
    .syl_free          = 1,
    .mark_emph         = 0x33,
    .mark_kind         = 3,
    .no_unrelease      = 1,
    .no_glottal        = 1,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .vdur_kind     = 5,
    .trn_kind      = 5,
    .ph_kind       = 9,
    .acc_kind      = 3,
    .long_ku       = 1,
    .join_words    = 1,
    .stress_kind   = 2,
    .trn_whole     = 1,
    .pair_class = {
        0, 7, 7, 7, 2, 2, 2, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        1, 1, 1, 1, 7, 3, 7, 6, 6, 1, 1, 1, 6, 6, 6, 6,
        6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .fall_on_next  = 1,
    .no_suffix     = 1,
    .close_pause   = 7,
    .comma_ends_text = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_none = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .hdr_shape     = 0x48,
    .code_shift        = -14,
    .chattr            = 0x1001B600,
    .letterattr        = 0x1001B300,
    .casemap           = 0x1001B500,
    .symmap            = 0x1001B400,
    .dict_silent       = 'o',
    .phattr1           = 0x1001AE10,
    .phattr2           = 0x1001AE80,
    .classtab          = 0x10017458,
    .exctab            = 0x10017460,
    .basedur           = 0x10017438,
    .coefgain          = 0x10017034,
    .tokstates         = 0x1001A708,
    .names             = 0x1001AEF0,
    .code_medial       = 0x1001B774,
    .code_initial      = 0x1001B708,
    .ph_single         = 0x10016E50,
    .ph_pair           = 0x10016EFC,
    .suffix_ptrs       = 0x1001B704,
    .suffix_n       = 1,
    .suffix_max       = 0,
    .lts_index         = 0x1001BDD8,
    .dispatch          = 0x1001BCF4,
    .rules             = 0x1001B970,
    .patterns          = 0x1001B7E0,
    .outputs           = 0x1001BC38,
    .trie_desc         = 0x1001A4A0,
    .modmap            = 0x10017738,
    .modtab            = 0x1001773C,
    .trans_pitch    = 0x1001A118,
    .vowel_dur      = 0x1001A2BC,
    .stress_num        = 0x1001A490,
    .stress_add        = 0x1001A47C,
    .sound_add         = 0x1001A434,
    .diph_records      = 0x10014540,
    .diph_offsets      = 0x10016480,
    .diph_offsets_end  = 0x100176E4,
    .voices            = 0x100130F0,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 2,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x1001794C, 0x10017E48, 0x10018308, 0x10018784, 0x10018AC4,
        0x10018CB0, 0x10018F58, 0x100191E0, 0x10019404, 0x100195E8,
        0x100196D8, 0x10019778, 0x1001981C, 0x10019870, 0x10019890,
    },
    .bucket_data = {
        0x100177C0, 0x10017958, 0x10017E64, 0x10018324, 0x100187A0,
        0x10018AD8, 0x10018CC0, 0x10018F68, 0x100191F0, 0x10019414,
        0x100195F8, 0x100196E4, 0x10019780, 0x10019824, 0x10019878,
    },
    .h = {
        [BST_H_APOS] = 0x1000D2D0,
        [BST_H_APOSDOT] = 0x1000D2B0,
        [BST_H_CURRENCY] = 0x1000D0D0,
        [BST_H_DASH] = 0x1000D110,
        [BST_H_DASH2] = 0x1000A6B0,
        [BST_H_DEL] = 0x1000D1D0,
        [BST_H_DIGIT] = 0x1000D060,
        [BST_H_DOT] = 0x1000D0C0,
        [BST_H_DOTOUT] = 0x1000A560,
        [BST_H_DOTTED] = 0x10001D50,
        [BST_H_EAT1] = 0x1000D0A0,
        [BST_H_EAT2] = 0x1000D5E0,
        [BST_H_EAT3] = 0x1000D5F0,
        [BST_H_EXPONENT] = 0x1000D180,
        [BST_H_GROUPS] = 0x10008580,
        [BST_H_HIGH] = 0x1000D160,
        [BST_H_HIGHOUT] = 0x10001C60,
        [BST_H_HIGH2] = 0x1000D1F0,
        [BST_H_LETTER] = 0x1000D040,
        [BST_H_MODE1] = 0x1000D1B0,
        [BST_H_MODE2] = 0x1000D1C0,
        [BST_H_MONEY] = 0x10002CE0,
        [BST_H_NUMBER] = 0x10007D10,
        [BST_H_OPENER] = 0x1000D200,
        [BST_H_ORDEMIT] = 0x1000D170,
        [BST_H_ORDINAL] = 0x1000D170,
        [BST_H_POSSESS] = 0x1000D2E0,
        [BST_H_PUNCT] = 0x1000D0F0,
        [BST_H_PUNCTOUT] = 0x1000A120,
        [BST_H_SEP] = 0x1000D470,
        [BST_H_SEPNUM] = 0x10007EA0,
        [BST_H_SPACE] = 0x1000D0B0,
        [BST_H_TILDE] = 0x1000D260,
        [BST_H_WORD] = 0x10001CA0,
    },
    .s = {
        [BST_S_AND] = 0x1001A3A4,
        [BST_S_CENTS] = 0x1001A3B0,
        [BST_S_DIGITS] = 0x1001B118,
        [BST_S_DOLLARS] = 0x1001A3A0, [BST_S_DOLLAR] = 0x1001A39C,
        [BST_S_GRPSEP] = 0x1001A40C,
        [BST_S_HUNDRED] = 0x1001A3B4,
        [BST_S_OH] = 0x1001A3BC,
        [BST_S_ORD_FIFTH] = 0x1001A3F8,
        [BST_S_ORD_FIRST] = 0x1001A3FC,
        [BST_S_ORD_ND] = 0x1001A3F0,
        [BST_S_ORD_RD] = 0x1001A3F4,
        [BST_S_ORD_ST] = 0x1001A3EC,
        [BST_S_ORD_TH] = 0x1001A404,
        [BST_S_ORD_TIETH] = 0x1001A400,
        [BST_S_PLURAL] = 0x1001A3FC,
        [BST_S_POINT] = 0x1001A3C0,
        [BST_S_SCALES] = 0x1001B250,
        [BST_S_TEENS] = 0x1001B168,
        [BST_S_TENS] = 0x1001B140,
        [BST_S_ZERO] = 0x1001B1D8,
    },
};
/* POL: 183 functions matched, 2739 addresses carried */
const bst_tabmap BST_MAP_2006_POL = {
    .tail_chars        = { '.', ' ', ' ' },
    .pair_prescan      = 1,
    .no_unrelease      = 1,
    .syl_free          = 1,
    .mark_emph         = 0x33,
    .no_glottal        = 1,
    .no_edge_reduce    = 1,
    .num_one_digit     = 1,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .init_level    = 2,
    .vdur_kind     = 1,
    .vdur_slow     = 0x41,
    .trn_kind      = 2,
    .stress_rule   = 3,
    .pat_bit = {
        [0x21] = 0x40,
        [0x23] = 0x01,
        [0x26] = 0x08,
        [0x2B] = 0x04,
        [0x2E] = 0x02,
        [0x3A] = 0x10,
        [0x5E] = 0x20,
    },

    .trn_whole     = 1,
    .pair_class = {
        0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
        1, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 1, 1, 1, 1, 7, 6, 6, 6,
        6, 6, 6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .no_suffix     = 1,
    .close_pause   = 1,
    .comma_ends_text = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .code_shift        = 6,
    .chattr            = 0x10026954,
    .letterattr        = 0x10026654,
    .casemap           = 0x10026854,
    .symmap            = 0x10026754,
    .dict_silent       = 'o',
    .phattr1           = 0x10022C28,
    .phattr2           = 0x10022CAC,
    .classtab          = 0x10025874,
    .exctab            = 0x1002587C,
    .basedur           = 0x10025854,
    .coefgain          = 0x10025450,
    .tokstates         = 0x10026108,
    /* The build reads q as k, either case, at dll_pol.dll 0x1000e4bc. */
    .in_map = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x6B, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
        0x70, 0x6B, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
        0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
        0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
        0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
        0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
    },
    .names             = 0x10022D30,
    .code_medial       = 0x10026B68,
    .code_initial      = 0x10026AFC,
    .ph_single         = 0x1001ECB0,
    .ph_pair           = 0x1001ED5C,
    .suffix_ptrs       = 0x00000000,
    .suffix_n       = 0,
    .suffix_max       = 0,
    .lts_index         = 0x10023CC0,
    .dispatch          = 0x10023BF8,
    .rules             = 0x10023468,
    .patterns          = 0x1002315C,
    .outputs           = 0x10023A50,
    .trie_desc         = 0x10021728,
    .modmap            = 0x1001F0A0,
    .modtab            = 0x1001F0B8,
    .trans_pitch    = 0x10021214,
    .vowel_dur      = 0x100214A8,
    .stress_num        = 0x1002171C,
    .stress_add        = 0x10021708,
    .sound_add         = 0x10021698,
    .diph_records      = 0x10017F08,
    .diph_offsets      = 0x1001D4F8,
    .diph_offsets_end  = 0x1001E75C,
    .voices            = 0x100130E8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x1001F74C, 0x1001F9D4, 0x1001FCBC, 0x1001FE1C, 0x1002001C,
        0x100202F4, 0x10020460, 0x100205A0, 0x10020634, 0x1002083C,
        0x10020860, 0x100209EC, 0x10020C30, 0x10020DD4, 0x10021204,
    },
    .bucket_data = {
        0x1001F18C, 0x1001F764, 0x1001F9E0, 0x1001FCC8, 0x1001FE24,
        0x10020028, 0x10020300, 0x10020468, 0x100205A8, 0x1002063C,
        0x10020848, 0x10020868, 0x100209F4, 0x10020C3C, 0x10020DDC,
    },
    .h = {
        [BST_H_APOS] = 0x1000D030,
        [BST_H_APOSDOT] = 0x1000D010,
        [BST_H_CURRENCY] = 0x1000CE30,
        [BST_H_DASH] = 0x1000CE70,
        [BST_H_DASH2] = 0x1000A390,
        [BST_H_DEL] = 0x1000CF30,
        [BST_H_DIGIT] = 0x1000CDC0,
        [BST_H_DOT] = 0x1000CE20,
        [BST_H_DOTOUT] = 0x1000A240,
        [BST_H_DOTTED] = 0x10001B30,
        [BST_H_EAT1] = 0x1000CE00,
        [BST_H_EAT2] = 0x1000D340,
        [BST_H_EAT3] = 0x1000D350,
        [BST_H_EXPONENT] = 0x1000CEE0,
        [BST_H_GROUPS] = 0x10008390,
        [BST_H_HIGH] = 0x1000CEC0,
        [BST_H_HIGHOUT] = 0x10001A40,
        [BST_H_HIGH2] = 0x1000CF50,
        [BST_H_LETTER] = 0x1000CDA0,
        [BST_H_MODE1] = 0x1000CF10,
        [BST_H_MODE2] = 0x1000CF20,
        [BST_H_MONEY] = 0x10002980,
        [BST_H_NUMBER] = 0x10007B20,
        [BST_H_ONEDIGIT] = 0x10007F70,
        [BST_H_OPENER] = 0x1000CF60,
        [BST_H_ORDEMIT] = 0x1000CED0,
        [BST_H_ORDINAL] = 0x1000CED0,
        [BST_H_POSSESS] = 0x1000D040,
        [BST_H_PUNCT] = 0x1000CE50,
        [BST_H_PUNCTOUT] = 0x10009E00,
        [BST_H_SEP] = 0x1000D1D0,
        [BST_H_SEPNUM] = 0x10007CB0,
        [BST_H_SPACE] = 0x1000CE10,
        [BST_H_TILDE] = 0x1000CFC0,
        [BST_H_WORD] = 0x10001A80,
    },
    .s = {
        [BST_S_AND] = 0x10021608,
        [BST_S_CENTS] = 0x10021614,
        [BST_S_DIGITS] = 0x10022F5C,
        [BST_S_DOLLARS] = 0x10021604, [BST_S_DOLLAR] = 0x10021600,
        [BST_S_GRPSEP] = 0x10021670,
        [BST_S_HUNDRED] = 0x10021618,
        [BST_S_OH] = 0x10021620,
        [BST_S_ORD_FIFTH] = 0x1002165C,
        [BST_S_ORD_FIRST] = 0x10021660,
        [BST_S_ORD_ND] = 0x10021654,
        [BST_S_ORD_RD] = 0x10021658,
        [BST_S_ORD_ST] = 0x10021650,
        [BST_S_ORD_TH] = 0x10021668,
        [BST_S_ORD_TIETH] = 0x10021664,
        [BST_S_PLURAL] = 0x10021660,
        [BST_S_POINT] = 0x10021624,
        [BST_S_SCALES] = 0x10023094,
        [BST_S_TEENS] = 0x10022FAC,
        [BST_S_TENS] = 0x10022F84,
        [BST_S_ZERO] = 0x1002301C,
    },
};
/* POR: 188 functions matched, 2863 addresses carried */
const bst_tabmap BST_MAP_2006_POR = {
    .tail_chars        = { '.', ' ', ' ' },
    .num_kind          = 1,
    .num_group_kind    = 1,
    .syl_free          = 1,
    .mark_kind         = 2,
    .mark_78_take      = 1,
    .num_two_kind      = 2,
    .pair_prescan      = 1,
    .no_unrelease      = 1,
    .no_glottal        = 1,
    .num_scale_kind    = 2,
    .num_hundred_and   = 1,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .in_map = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
        0x85, 0xA0, 0x83, 0xC3, 0x84, 0xC5, 0xC6, 0x73, 0xC8, 0x82, 0x88, 0xCB, 0xCC, 0xA1, 0xCE, 0xCF,
        0xD0, 0xD1, 0xD2, 0xA2, 0x93, 0xD5, 0x94, 0xD7, 0xD8, 0xD9, 0xA3, 0xDB, 0x81, 0xDD, 0xDE, 0xDF,
        0x85, 0xA0, 0x83, 0xE3, 0x84, 0xE5, 0xE6, 0x73, 0xE8, 0x82, 0x88, 0xEB, 0xEC, 0xA1, 0xEE, 0xEF,
        0xF0, 0xF1, 0xF2, 0xA2, 0x93, 0xF5, 0x94, 0xF7, 0xF8, 0xF9, 0xA3, 0xFB, 0x81, 0xFD, 0xFE, 0xFF,
    },
    .vdur_flat     = 1,
    .vdur_slow     = 0x41,
    .vdur_kind     = 1,
    .vdur_lo       = 0x20,
    .vdur_hi       = 0x35,
    .trn_kind      = 2,
    .ph_kind       = 3,
    .stress_rule   = 2,
    .trn_whole     = 1,
    .pair_class = {
        0, 2, 2, 2, 2, 2, 2, 7, 7, 1, 1, 1, 7, 7, 7, 7,
        7, 7, 7, 7, 1, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0,
    },
    .no_suffix     = 1,
    .close_pause   = 1,
    .comma_ends_text = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .code_shift        = 8,
    .chattr            = 0x10023CB8,
    .letterattr        = 0x100239B8,
    .casemap           = 0x10023BB8,
    .symmap            = 0x10023AB8,
    .dict_silent       = 'o',
    .phattr1           = 0x10023498,
    .phattr2           = 0x10023520,
    .classtab          = 0x1002009C,
    .exctab            = 0x100200A4,
    .basedur           = 0x1002007C,
    .coefgain          = 0x1001FC78,
    .tokstates         = 0x10022D38,
    .names             = 0x100235A8,
    .code_medial       = 0x10023E38,
    .code_initial      = 0x10023DCC,
    .ph_single         = 0x1001FA94,
    .ph_pair           = 0x1001FB40,
    .suffix_ptrs       = 0x10023DC8,
    .suffix_n       = 1,
    .suffix_max       = 0,
    .lts_index         = 0x10025F80,
    .dispatch          = 0x10025DB4,
    .rules             = 0x100248D0,
    .patterns          = 0x10023EA4,
    .outputs           = 0x10025870,
    .trie_desc         = 0x100222C8,
    .modmap            = 0x100200AC,
    .modtab            = 0x100200BC,
    .trans_pitch    = 0x10021D88,
    .vowel_dur      = 0x10022034,
    .stress_num        = 0x100222B8,
    .stress_add        = 0x100222A4,
    .sound_add         = 0x10022230,
    .diph_records      = 0x100177A8,
    .diph_offsets      = 0x1001E120,
    .diph_offsets_end  = 0x1001F384,
    .voices            = 0x100140F8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x10020308, 0x10020434, 0x10020634, 0x100206C8, 0x10020A08,
        0x10020BA4, 0x10020CD0, 0x10020DB4, 0x10020E50, 0x10020FDC,
        0x100210B4, 0x10021160, 0x10021274, 0x100213EC, 0x10021500,
    },
    .bucket_data = {
        0x1002018C, 0x10020314, 0x10020440, 0x10020640, 0x100206D0,
        0x10020A1C, 0x10020BB0, 0x10020CDC, 0x10020DBC, 0x10020E58,
        0x10020FE8, 0x100210BC, 0x10021168, 0x1002127C, 0x100213F8,
    },
    .h = {
        [BST_H_APOS] = 0x1000D190,
        [BST_H_APOSDOT] = 0x1000D170,
        [BST_H_CURRENCY] = 0x1000CFA0,
        [BST_H_DASH] = 0x1000CFE0,
        [BST_H_DASH2] = 0x1000A4C0,
        [BST_H_DEL] = 0x1000D090,
        [BST_H_DIGIT] = 0x1000CF30,
        [BST_H_DOT] = 0x1000CF90,
        [BST_H_DOTOUT] = 0x1000A370,
        [BST_H_DOTTED] = 0x10001BE0,
        [BST_H_EAT1] = 0x1000CF70,
        [BST_H_EAT2] = 0x1000D4A0,
        [BST_H_EAT3] = 0x1000D4B0,
        [BST_H_EXPONENT] = 0x1000D040,
        [BST_H_GROUPS] = 0x100082C0,
        [BST_H_HIGH] = 0x1000D030,
        [BST_H_HIGHOUT] = 0x10001AF0,
        [BST_H_HIGH2] = 0x1000D0B0,
        [BST_H_LETTER] = 0x1000CF10,
        [BST_H_MODE1] = 0x1000D070,
        [BST_H_MODE2] = 0x1000D080,
        [BST_H_MONEY] = 0x10002B10,
        [BST_H_NUMBER] = 0x10007A50,
        [BST_H_OPENER] = 0x1000D0C0,
        [BST_H_ORDEMIT] = 0x10001E50,
        [BST_H_ORDINAL] = 0x10001E50,
        [BST_H_POSSESS] = 0x1000D1A0,
        [BST_H_PUNCT] = 0x1000CFC0,
        [BST_H_PUNCTOUT] = 0x10009F30,
        [BST_H_SEP] = 0x1000D330,
        [BST_H_SEPNUM] = 0x10007BE0,
        [BST_H_SPACE] = 0x1000CF80,
        [BST_H_TILDE] = 0x1000D120,
        [BST_H_WORD] = 0x10001B30,
    },
    .s = {
        [BST_S_AND] = 0x100221A0,
        [BST_S_CENTS] = 0x100221AC,
        [BST_S_DIGITS] = 0x100237D0,
        [BST_S_DOLLARS] = 0x1002219C, [BST_S_DOLLAR] = 0x10022198,
        [BST_S_GRPSEP] = 0x10022208,
        [BST_S_HUNDRED] = 0x100221B0,
        [BST_S_OH] = 0x100221B8,
        [BST_S_ORD_FIFTH] = 0x100221F4,
        [BST_S_ORD_FIRST] = 0x100221F8,
        [BST_S_ORD_ND] = 0x100221EC,
        [BST_S_ORD_RD] = 0x100221F0,
        [BST_S_ORD_ST] = 0x100221E8,
        [BST_S_ORD_TH] = 0x10022200,
        [BST_S_ORD_TIETH] = 0x100221FC,
        [BST_S_PLURAL] = 0x100221F8,
        [BST_S_POINT] = 0x100221BC,
        [BST_S_SCALES] = 0x10023930,
        [BST_S_HUNDREDS] = 0x10023848,
        [BST_S_TEENS] = 0x10023820,
        [BST_S_TENS] = 0x100237F8,
        [BST_S_ZERO] = 0x10023890,
    },
};
/* RUS: 100 functions matched, 920 addresses carried */
const bst_tabmap BST_MAP_2006_RUS = {
    .pair_prescan      = 1,
    .no_unrelease      = 1,
    .syl_free          = 1,
    .mark_emph         = 0x33,
    .mark_kind         = 1,
    .no_glottal        = 1,
    .num_spell_fives   = 1,
    .num_kind          = 1,
    .num_two_kind      = 2,
    .num_group_kind    = 1,
    .run_stop_kind     = 1,
    .pair_class = {
        0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
        1, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 1, 1, 1, 1, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .acc_shape_keep    = 1,
    .fall_code_last    = 0x3B,
    .acc_fin           = 0x42,
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .pat_bit = {
        [0x21] = 0x40,
        [0x23] = 0x01,
        [0x26] = 0x08,
        [0x2B] = 0x04,
        [0x2E] = 0x02,
        [0x3A] = 0x10,
        [0x5E] = 0x20,
    },
    .pat_ext           = 1,
    .dict_lead_max     = 4,
    .lts_lit_char      = 0xEB,
    .lts_lit_code      = 0x2E,
    .init_level        = 2,
    .ph_kind           = 10,
    .stress_kind       = 3,
    .code_shift        = 4,
    .cmd_code          = 0x80,
    .lts_index         = 0x1007E68C,
    .dispatch          = 0x1007E5AC,
    .rules             = 0x1007DAC0,
    .outputs           = 0x1007E338,
    .trie_desc         = 0x10078810,
    .patterns          = 0x1007D5E8,
    .trans_pitch       = 0x10078324,
    .vowel_dur         = 0x100785A0,
    .stress_num        = 0x10078804,
    .stress_add        = 0x100787F0,
    .sound_add         = 0x10078784,
    .stress_shift      = 5,
    .vdur_kind         = 1,
    .vdur_slow         = 0x41,
    .trn_kind          = 2,
    .h = {
        [BST_H_APOS] = 0x1000EDB0,
        [BST_H_APOSDOT] = 0x1000ED90,
        [BST_H_CURRENCY] = 0x1000EB90,
        [BST_H_DASH] = 0x1000EBD0,
        [BST_H_DASH2] = 0x1000BC70,
        [BST_H_DEL] = 0x1000ECB0,
        [BST_H_DIGIT] = 0x1000EAE0,
        [BST_H_DOT] = 0x1000EB80,
        [BST_H_DOTOUT] = 0x1000BAF0,
        [BST_H_DOTTED] = 0x100015B0,
        [BST_H_EAT1] = 0x1000EB20,
        [BST_H_EAT2] = 0x1000D130,
        [BST_H_EAT3] = 0x1000D150,
        [BST_H_EXPONENT] = 0x1000EC30,
        [BST_H_GROUPS] = 0x10009450,
        [BST_H_HIGH] = 0x1000EC20,
        [BST_H_HIGHOUT] = 0x100014B0,
        [BST_H_HIGH2] = 0x1000ECD0,
        [BST_H_LETTER] = 0x1000EAC0,
        [BST_H_MODE1] = 0x1000EC70,
        [BST_H_MODE2] = 0x1000EC90,
        [BST_H_MONEY] = 0x10004230,
        [BST_H_NUMBER] = 0x10008B40,
        [BST_H_OPENER] = 0x1000ECE0,
        [BST_H_ORDEMIT] = 0x10008EB0,
        [BST_H_ORDINAL] = 0x10008EB0,
        [BST_H_POSSESS] = 0x1000EDC0,
        [BST_H_PUNCT] = 0x1000EBB0,
        [BST_H_PUNCTOUT] = 0x1000B680,
        [BST_H_SEP] = 0x1000EF50,
        [BST_H_SEPNUM] = 0x10008CF0,
        [BST_H_SPACE] = 0x1000EB40,
        [BST_H_TILDE] = 0x1000ED40,
        [BST_H_WORD] = 0x100014F0,
    },
    .in_map = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA5, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xB0, 0xB0, 0xB2, 0xB2, 0xB5, 0xB5, 0xB6, 0xB7, 0xA5, 0xB9, 0xB9, 0xBB, 0xBC, 0xBD, 0xBE, 0xBE,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    },
    .trn_whole     = 1,
    .no_suffix     = 1,
    .close_pause   = 1,
    .hdr_shape     = 0x52,
    .comma_ends_text = 2,
    .close_paren_comma = 1,
    .lone_no_words = 1,
    .speak_now_none = 2,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .chattr            = 0x100262D0,
    .letterattr        = 0x100259A4,
    .casemap           = 0x100261D0,
    .symmap            = 0x10025AA4,
    .dict_silent       = 'o',
    .phattr1           = 0x10078FE8,
    .phattr2           = 0x1007906C,
    .classtab          = 0x100261AC,
    .exctab            = 0x100261B4,
    .basedur           = 0x1002618C,
    .coefgain          = 0x10025D88,
    .tokstates         = 0x10078898,
    .names             = 0x100790F0,
    .code_medial       = 0x10079574,
    .code_initial      = 0x10079508,
    .ph_single         = 0x10025BA4,
    .ph_pair           = 0x10025C50,
    .suffix_ptrs       = 0x00000000,
    .suffix_n       = 0,
    .suffix_max       = 0,
    .modmap            = 0x100263D0,
    .modtab            = 0x10080064,
    .diph_records      = 0x10019F08,
    .diph_offsets      = 0x10024318,
    .diph_offsets_end  = 0x100258A4,
    .voices            = 0x100150E8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x1002E3DC, 0x1003A628, 0x10041AD4, 0x10047A18, 0x1004FA58,
        0x1005447C, 0x10059748, 0x1005D784, 0x10061674, 0x10064C40,
        0x10068A24, 0x1006BB24, 0x1006E594, 0x10071210, 0x1007795C,
    },
    .bucket_data = {
        0x100263E8, 0x1002E584, 0x1003A894, 0x10041C50, 0x10047B4C,
        0x1004FBCC, 0x1005456C, 0x10059854, 0x1005D84C, 0x10061734,
        0x10064CE8, 0x10068AE8, 0x1006BBC0, 0x1006E61C, 0x100712A0,
    },
        .s = {
        [BST_S_DIGITS] = 0x10079318,
        [BST_S_SCALES] = 0x10079478,
        [BST_S_TEENS] = 0x10079368,
        [BST_S_TENS] = 0x10079340,
        [BST_S_ZERO] = 0x100793D8,
        [BST_S_HUNDREDS] = 0x10079390,
    },
};
/* not placed: lts_index dispatch rules outputs trie_desc trans_pitch vowel_dur stress_num stress_add sound_add diph_records diph_offsets voices h.BST_H_LETTER h.BST_H_DIGIT h.BST_H_EAT1 h.BST_H_SPACE h.BST_H_DOT h.BST_H_CURRENCY h.BST_H_PUNCT h.BST_H_DASH h.BST_H_EXPONENT h.BST_H_MODE1 h.BST_H_MODE2 h.BST_H_DEL h.BST_H_OPENER h.BST_H_TILDE h.BST_H_APOSDOT h.BST_H_APOS h.BST_H_POSSESS h.BST_H_EAT2 h.BST_H_EAT3 h.BST_H_WORD h.BST_H_NUMBER h.BST_H_DOTTED h.BST_H_SEP h.BST_H_GROUPS h.BST_H_SEPNUM h.BST_H_MONEY h.BST_H_DASH2 h.BST_H_ORDINAL h.BST_H_ORDEMIT h.BST_H_PUNCTOUT h.BST_H_DOTOUT s.BST_S_POINT s.BST_S_DOLLARS s.BST_S_AND s.BST_S_CENTS s.BST_S_ORD_ST s.BST_S_ORD_ND s.BST_S_ORD_RD s.BST_S_ORD_TIETH s.BST_S_ORD_FIFTH s.BST_S_ORD_FIRST s.BST_S_ORD_TH s.BST_S_GRPSEP s.BST_S_PLURAL s.BST_S_OH s.BST_S_HUNDRED bucket_index bucket_data code_shift cmd_code */
/* SPA: 189 functions matched, 2873 addresses carried */
const bst_tabmap BST_MAP_2006_SPA = {
    .tail_chars        = { '.', ' ', ' ' },
    .trn_lone          = 0x16,
    .num_two_kind      = 2,
    .num_kind          = 1,
    .syl_free          = 1,
    .mark_emph         = 0x33,
    .no_weak_mark      = 1,
    .mark_kind         = 2,
    .num_group_kind    = 1,
    .pair_prescan      = 1,
    .no_unrelease      = 1,
    .no_glottal        = 1,
    .num_scale_kind    = 2,
    .punct_s8 = { [1] = 1 },
    .open_plain    = 1,
    .trn_dur_wide  = 1,
    .ph_kind       = 1,
    .hdr_shape     = 0x52,
    .trn_kind      = 1,
    .vdur_kind     = 1,
    .trn_whole     = 1,
    .pair_class = {
        0, 7, 7, 7, 2, 7, 2, 7, 2, 7, 1, 1, 1, 1, 7, 7,
        7, 7, 7, 7, 1, 1, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .fall_on_next  = 1,
    .no_suffix     = 1,
    .stress_rule   = 1,
    .stress_keep   = { 0x0B, 0x0F },
    .close_pause   = 1,
    .comma_ends_text = 1,
    .close_paren_comma = 1,
    .lone_no_words = 1, .brace_ends_text = 1,
    .speak_now_inline = 1,
    .contour_round = 1,
    .dur_frac_shift = 1,
    .possessive_is = 1,
    .sepnum_first_three = 1,
    .code_shift        = -13,
    .chattr            = 0x1001A88C,
    .letterattr        = 0x1001A58C,
    .casemap           = 0x1001A78C,
    .symmap            = 0x1001A68C,
    .dict_silent       = 'o',
    .phattr1           = 0x1001A09C,
    .phattr2           = 0x1001A10C,
    .classtab          = 0x10017B1C,
    .exctab            = 0x10017B24,
    .basedur           = 0x10017AFC,
    .coefgain          = 0x100176F8,
    .tokstates         = 0x10019990,
    .names             = 0x1001A17C,
    .code_medial       = 0x1001AA00,
    .code_initial      = 0x1001A994,
    .ph_single         = 0x10017514,
    .ph_pair           = 0x100175C0,
    .suffix_ptrs       = 0x1001A990,
    .suffix_n       = 1,
    .suffix_max       = 0,
    .lts_index         = 0x1001B2C4,
    .dispatch          = 0x1001B198,
    .rules             = 0x1001AC50,
    .patterns          = 0x1001AA6C,
    .outputs           = 0x1001B068,
    .trie_desc         = 0x10018A20,
    .modmap            = 0x10017B2C,
    .modtab            = 0x10017B34,
    .trans_pitch    = 0x10018688,
    .vowel_dur      = 0x10018838,
    .stress_num        = 0x10018A10,
    .stress_add        = 0x100189FC,
    .sound_add         = 0x100189B4,
    .diph_records      = 0x100143A8,
    .diph_offsets      = 0x10016AB8,
    .diph_offsets_end  = 0x10017D1C,
    .voices            = 0x100130E8,
    .tok_stride        = 12,
    .tok_state_w       = 4,
    .tok_handler_off   = 4,
    .tok_handler_w     = 4,
    .tok_next_off      = 8,
    .tok_next_w        = 4,
    .lts_index_stride  = 4,
    .rule_stride       = 8,
    .rule_prio_w       = 2,
    .trie_st_off       = 4,
    .trie_li_off       = 8,
    .trie_base_off     = 12,
    .trie_max_off      = 14,
    .trie_po_off       = 16,
    .silence_f0        = 209,
    .unvoiced_dur      = 104,
    .unvoiced_reps     = 1,
    .min_period        = 62,
    .interp_round_mask = 7,
    .coef_round_mask   = 6,
    .class_shift       = 13,
    .long_silence_f0   = 216,
    .exc_two_way       = 1,
    .unvoiced_chunk    = 1,
    .no_breath_break   = 1,
    .no_closing_phrase = 1,
    .dur_mult          = 53,
    .nearest_round     = 1,
    .pitch_rate        = 10400,
    .slope_mult        = 167,
    .inton_dur_mult    = 887,
    .inton_slope_shift = 1,
    .stress_shift      = 5,
    .voice_stride      = 4,
    .trn_round         = 1,
    .ph_single_n       = 84,
    .ph_pair_n         = 154,
    .code_lo           = 268439552,
    .code_hi           = 268521472,
    .bucket_index = {
        0x10017BFC, 0x10017C14, 0x10017C24, 0x10017E04, 0x10017C50,
        0x10017C7C, 0x10017CAC, 0x10017CD0, 0x10017D0C, 0x10017D20,
        0x10017D40, 0x10017D68, 0x10017D84, 0x10017DC4, 0x10017DF8,
    },
    .bucket_data = {
        0x10017BA8, 0x10017C04, 0x10017C1C, 0x10017E00, 0x10017C2C,
        0x10017C58, 0x10017C84, 0x10017CB4, 0x10017CD8, 0x10017D14,
        0x10017D28, 0x10017D48, 0x10017D70, 0x10017D8C, 0x10017DCC,
    },
    .h = {
        [BST_H_APOS] = 0x1000D7A0,
        [BST_H_APOSDOT] = 0x1000D780,
        [BST_H_CURRENCY] = 0x1000D5B0,
        [BST_H_DASH] = 0x1000D5F0,
        [BST_H_DASH2] = 0x1000AA10,
        [BST_H_DEL] = 0x1000D6A0,
        [BST_H_DIGIT] = 0x1000D540,
        [BST_H_DOT] = 0x1000D5A0,
        [BST_H_DOTOUT] = 0x1000A8C0,
        [BST_H_DOTTED] = 0x10001F00,
        [BST_H_EAT1] = 0x1000D580,
        [BST_H_EAT2] = 0x1000DAB0,
        [BST_H_EAT3] = 0x1000DAC0,
        [BST_H_EXPONENT] = 0x1000D650,
        [BST_H_GROUPS] = 0x100087D0,
        [BST_H_HIGH] = 0x1000D640,
        [BST_H_HIGHOUT] = 0x10001E10,
        [BST_H_HIGH2] = 0x1000D6C0,
        [BST_H_LETTER] = 0x1000D520,
        [BST_H_MODE1] = 0x1000D680,
        [BST_H_MODE2] = 0x1000D690,
        [BST_H_MONEY] = 0x10002E80,
        [BST_H_NUMBER] = 0x10007F60,
        [BST_H_OPENER] = 0x1000D6D0,
        [BST_H_ORDEMIT] = 0x10002170,
        [BST_H_ORDINAL] = 0x10002170,
        [BST_H_POSSESS] = 0x1000D7B0,
        [BST_H_PUNCT] = 0x1000D5D0,
        [BST_H_PUNCTOUT] = 0x1000A480,
        [BST_H_SEP] = 0x1000D940,
        [BST_H_SEPNUM] = 0x100080F0,
        [BST_H_SPACE] = 0x1000D590,
        [BST_H_TILDE] = 0x1000D730,
        [BST_H_WORD] = 0x10001E50,
    },
    .s = {
        [BST_S_AND] = 0x10018924,
        [BST_S_CENTS] = 0x10018930,
        [BST_S_DIGITS] = 0x1001A3A4,
        [BST_S_DOLLARS] = 0x10018920, [BST_S_DOLLAR] = 0x1001891C,
        [BST_S_GRPSEP] = 0x1001898C,
        [BST_S_HUNDRED] = 0x10018934,
        [BST_S_OH] = 0x1001893C,
        [BST_S_ORD_FIFTH] = 0x10018978,
        [BST_S_ORD_FIRST] = 0x1001897C,
        [BST_S_ORD_ND] = 0x10018970,
        [BST_S_ORD_RD] = 0x10018974,
        [BST_S_ORD_ST] = 0x1001896C,
        [BST_S_ORD_TH] = 0x10018984,
        [BST_S_ORD_TIETH] = 0x10018980,
        [BST_S_PLURAL] = 0x1001897C,
        [BST_S_POINT] = 0x10018940,
        [BST_S_SCALES] = 0x1001A504,
        [BST_S_HUNDREDS] = 0x1001A41C,
        [BST_S_TEENS] = 0x1001A3F4,
        [BST_S_TENS] = 0x1001A3CC,
        [BST_S_ZERO] = 0x1001A464,
    },
};

/* A directory by name, for the tools that take one on the command line. */
const bst_tabmap *bst_map_named(const char *name) {
    static const struct { const char *name; const bst_tabmap *map; } M[] = {
        { "1995", &BST_MAP_1995 },
        { "ENG", &BST_MAP_1998_ENG }, { "DUT", &BST_MAP_1998_DUT },
        { "FRN", &BST_MAP_1998_FRN }, { "GRM", &BST_MAP_1998_GRM },
        { "ITL", &BST_MAP_1998_ITL }, { "SPN", &BST_MAP_1998_SPN },
        { "2006ENG", &BST_MAP_2006_ENG }, { "2006ARA", &BST_MAP_2006_ARA },
        { "2006DUT", &BST_MAP_2006_DUT }, { "2006FRE", &BST_MAP_2006_FRE },
        { "2006GER", &BST_MAP_2006_GER }, { "2006GRE", &BST_MAP_2006_GRE },
        { "2006HEB", &BST_MAP_2006_HEB }, { "2006ITA", &BST_MAP_2006_ITA },
        { "2006JPN", &BST_MAP_2006_JPN }, { "2006POL", &BST_MAP_2006_POL },
        { "2006POR", &BST_MAP_2006_POR }, { "2006RUS", &BST_MAP_2006_RUS },
        { "2006SPA", &BST_MAP_2006_SPA },
    };
    for (size_t i = 0; i < sizeof M / sizeof M[0]; i++)
        if (!strcmp(M[i].name, name)) return M[i].map;
    return NULL;
}

/* Names every entry the directory has not been given, so a build's map can be
   read for what is missing rather than tried and puzzled over. */
int bst_map_gaps(const bst_tabmap *m, const char **names, int max) {
    static const struct { const char *name; size_t off; } F[] = {
        { "chattr", offsetof(bst_tabmap, chattr) },
        { "letterattr", offsetof(bst_tabmap, letterattr) },
        { "casemap", offsetof(bst_tabmap, casemap) },
        { "symmap", offsetof(bst_tabmap, symmap) },
        { "phattr1", offsetof(bst_tabmap, phattr1) },
        { "phattr2", offsetof(bst_tabmap, phattr2) },
        { "classtab", offsetof(bst_tabmap, classtab) },
        { "exctab", offsetof(bst_tabmap, exctab) },
        { "basedur", offsetof(bst_tabmap, basedur) },
        { "coefgain", offsetof(bst_tabmap, coefgain) },
        { "tokstates", offsetof(bst_tabmap, tokstates) },
        { "names", offsetof(bst_tabmap, names) },
        { "code_medial", offsetof(bst_tabmap, code_medial) },
        { "code_initial", offsetof(bst_tabmap, code_initial) },
        { "ph_single", offsetof(bst_tabmap, ph_single) },
        { "ph_single_max", offsetof(bst_tabmap, ph_single_max) },
        { "ph_pair", offsetof(bst_tabmap, ph_pair) },
        { "ph_pair_max", offsetof(bst_tabmap, ph_pair_max) },
        { "suffix_ptrs", offsetof(bst_tabmap, suffix_ptrs) },
        { "lts_index", offsetof(bst_tabmap, lts_index) },
        { "dispatch", offsetof(bst_tabmap, dispatch) },
        { "rules", offsetof(bst_tabmap, rules) },
        { "patterns", offsetof(bst_tabmap, patterns) },
        { "outputs", offsetof(bst_tabmap, outputs) },
        { "trie_desc", offsetof(bst_tabmap, trie_desc) },
        { "modmap", offsetof(bst_tabmap, modmap) },
        { "modtab", offsetof(bst_tabmap, modtab) },
        { "trans_pitch", offsetof(bst_tabmap, trans_pitch) },
        { "vowel_dur", offsetof(bst_tabmap, vowel_dur) },
        { "stress_num", offsetof(bst_tabmap, stress_num) },
        { "stress_add", offsetof(bst_tabmap, stress_add) },
        { "sound_add", offsetof(bst_tabmap, sound_add) },
        { "diph_records", offsetof(bst_tabmap, diph_records) },
        { "diph_offsets", offsetof(bst_tabmap, diph_offsets) },
        { "voices", offsetof(bst_tabmap, voices) },
    };
    int n = 0;
    for (size_t i = 0; i < sizeof F / sizeof F[0]; i++) {
        uint32_t v;
        memcpy(&v, (const char *)m + F[i].off, sizeof v);
        if (!v && n < max) names[n++] = F[i].name;
    }
    for (int i = 0; i < BST_BUCKETS; i++)
        if (!m->bucket_index[i] && n < max) names[n++] = "bucket_index";
    for (int i = 0; i < BST_H_COUNT; i++)
        if (!m->h[i] && n < max) names[n++] = "handler";
    for (int i = 0; i < BST_S_COUNT; i++)
        if (!m->s[i] && n < max) names[n++] = "string";
    return n;
}

#define A_VOWEL   1
#define A_VOICED  2
#define A_CONS    4
#define A_DOUBLE  8

int bst_image_init(bst_image *img, const void *data, size_t len) {
    return bst_image_init_map(img, data, len, &BST_MAP_1995);
}

int bst_image_init_map(bst_image *img, const void *data, size_t len,
                       const bst_tabmap *map) {
    if (!data || len < 0x20000) return -1;
    const uint8_t *d = data;
    img->image = d;
    img->len = len;
    img->nsec = 0;
    img->base = 0;
    img->t = *map;

    uint32_t pe = (uint32_t)(d[0x3C] | (d[0x3D] << 8) | (d[0x3E] << 16) | (d[0x3F] << 24));
    if (pe + 0x100 >= len || d[pe] != 'P' || d[pe + 1] != 'E') return 0;
    int nsec = d[pe + 6] | (d[pe + 7] << 8);
    int optsz = d[pe + 20] | (d[pe + 21] << 8);
    const uint8_t *o = d + pe + 24 + 28;
    img->base = (uint32_t)(o[0] | (o[1] << 8) | (o[2] << 16) | (o[3] << 24));
    if (nsec > BST_SECTIONS) nsec = BST_SECTIONS;
    for (int i = 0; i < nsec; i++) {
        const uint8_t *h = d + pe + 24 + optsz + i * 40 + 8;
        uint32_t v[4];
        for (int k = 0; k < 4; k++)
            v[k] = (uint32_t)(h[k * 4] | (h[k * 4 + 1] << 8) |
                              (h[k * 4 + 2] << 16) | (h[k * 4 + 3] << 24));
        img->sec[img->nsec].vsize   = v[0];
        img->sec[img->nsec].va      = v[1];
        img->sec[img->nsec].rawsize = v[2];
        img->sec[img->nsec].raw     = v[3];
        img->nsec++;
    }
    return 0;
}

void (*bst_read_hook)(const bst_image *img, size_t off, size_t need);

const uint8_t *bst_at(const bst_image *img, uint32_t va, size_t need) {
    if (!img->nsec) return NULL;
    uint32_t rva = va - img->base;
    for (int i = 0; i < img->nsec; i++) {
        uint32_t sz = img->sec[i].vsize > img->sec[i].rawsize
                    ? img->sec[i].vsize : img->sec[i].rawsize;
        if (rva < img->sec[i].va || rva >= img->sec[i].va + sz) continue;
        size_t off = img->sec[i].raw + (rva - img->sec[i].va);
        if (off + need > img->len) return NULL;
        if (bst_read_hook) bst_read_hook(img, off, need);
        return img->image + off;
    }
    return NULL;
}

static int attr(const bst_image *img, int c) {
    if (c < 0 || c > 255) return 0;
    return bst_u8(img, img->t.letterattr, c);
}

/* Decides whether a stem ending at i wants its silent e back. */
static int wants_e(const bst_image *img, const char *w, int i) {
    int c = i >= 0 ? (unsigned char)w[i] : 0;
    int p = i >= 1 ? (unsigned char)w[i - 1] : 0;
    if (attr(img, c) & A_CONS) return 0;
    if (!(attr(img, c) & A_VOWEL) || !(attr(img, p) & A_VOWEL)) return 1;
    if (p == 'u' && i >= 2 && (w[i - 2] == 'q' || w[i - 2] == 'g')) return 1;
    return 0;
}

/* The stem check before -ed and -ing come off. Returns the new last index, or
   -1 to refuse the strip. */
static int stem(const bst_image *img, char *w, int i, int vowels) {
    if (i < 0) return -1;
    int c = (unsigned char)w[i];
    int p = i >= 1 ? (unsigned char)w[i - 1] : 0;
    int add_e = 0;

    if (p == c && (attr(img, c) & A_DOUBLE)) return i - 1;

    switch (c) {
    case 'a': case 'e': case 'o': case 'x': case 'y':
        break;
    case 'b':
        if (strchr("aeiouy", p)) add_e = 1;
        else if (p == 'l' || p == 'm' || p == 'r') break;
        else return -1;
        break;
    case 'c': case 'u': case 'v':
        add_e = 1;
        break;
    case 'g':
        if (!(p == 'n' && i >= 2 && (w[i - 2] == 'i' || w[i - 2] == 'o'))) add_e = 1;
        break;
    case 'h':
        if (p == 't') add_e = 1;
        break;
    case 'i':
        w[i] = 'y';
        break;
    case 'l':
        if (p == 'a' || p == 'e') { if (vowels <= 2) add_e = 1; }
        else if (strchr("bcdfghjkmnpqstvxz", p)) return -1;
        else add_e = wants_e(img, w, i - 1);
        break;
    case 'r':
        if (attr(img, p) & A_CONS) return -1;
        if (vowels > 2) {
            if (p == 'a') {
                if ((i >= 2 && w[i - 2] == 'p') ||
                    (i >= 3 && w[i - 2] == 'l' && w[i - 3] == 'c')) add_e = 1;
            } else if (p != 'e' && p != 'o') {
                add_e = wants_e(img, w, i - 1);
            }
        } else add_e = 1;
        break;
    case 's':
        if (p == 's') break;
        if (p == 'u' && i >= 2 && w[i - 2] == 'o') return -1;
        add_e = 1;
        break;
    case 't':
        if (p == 'a') {
            if (!(i >= 2 && (w[i - 2] == 'o' || w[i - 2] == 'e'))) add_e = 1;
        } else if (p == 'e') {
            if (vowels <= 2) add_e = 1;
        } else if (p == 'i') {
            if (!(vowels > 2 && (i < 2 || w[i - 2] != 'v') &&
                  !(i >= 3 && w[i - 2] == 'r' && w[i - 3] == 'w'))) add_e = 1;
        } else add_e = wants_e(img, w, i - 1);
        break;
    case 'w':
        if (attr(img, p) & A_CONS) return -1;
        break;
    case 'z':
        if (attr(img, p) & A_VOWEL) add_e = 1;
        break;
    default:
        if (!wants_e(img, w, i - 1)) return i;
        add_e = 1;
        if (vowels > 2) {
            if (c == 'm') {
                if (p == 'o' && (i < 2 || w[i - 2] != 'c')) add_e = 0;
            } else if (c == 'n') {
                if (p == 'a' || p == 'o') add_e = 0;
                else if (p == 'e' &&
                         !(i >= 3 && w[i - 2] == 'v' && (attr(img, (unsigned char)w[i - 3]) & A_CONS)))
                    add_e = 0;
            } else if (c == 'p' && (p == 'i' || p == 'o')) {
                add_e = 0;
            }
        }
        break;
    }

    if (add_e) {
        w[i + 1] = 'e';
        return i + 1;
    }
    return i;
}

/* ---- the compound splitter ----------------------------------------------

   German is written with its compounds run together, so the build cuts a long
   word where a run of consonants can be read as the end of one part and the
   start of the next. Five tries decide it: which endings and beginnings are
   the build's own, how common a cluster is at the end of a part and at the
   start of one, and which clusters never take a cut at all. The score is the
   two frequencies added, and the highest wins.

   The word arrives as the rules will read it, with every h already written
   ch, so the positions are the ones the caller cuts at. */

struct split_trie {
    uint32_t nodes;
    unsigned limit, suffix, prefix, coda, nosplit, onset;
};

static void split_load(const bst_image *img, struct split_trie *k) {
    uint32_t t = img->t.split_tab;
    k->limit   = (unsigned)bst_u16(img, t, 0);
    k->suffix  = (unsigned)bst_u16(img, t, 1);
    k->prefix  = (unsigned)bst_u16(img, t, 2);
    k->coda    = (unsigned)bst_u16(img, t, 3);
    k->nosplit = (unsigned)bst_u16(img, t, 4);
    k->onset   = (unsigned)bst_u16(img, t, 5);
    k->nodes   = t + 12;
}

static int split_vowel(const bst_image *img, int c) {
    return bst_u8(img, img->t.chattr, c & 0xFF) & 0x10;
}

/* One step down a trie. Returns the node reached, or -1. */
static int split_step(const bst_image *img, const struct split_trie *k,
                      unsigned base, int c) {
    unsigned i = base + (unsigned)bst_u8(img, img->t.symmap, c & 0xFF);
    if ((i & 0xFFFF) > k->limit) return -1;
    if ((int)(int8_t)bst_u8(img, k->nodes, (int)(i * 4)) != c) return -1;
    return (int)i;
}

/* The value the trie holds for s[from..to], or zero if it holds none. */
static int split_value(const bst_image *img, const struct split_trie *k,
                       const char *s, int from, int to, unsigned base) {
    int node = -1;
    for (int i = from; i <= to; i++) {
        unsigned b = node < 0 ? base
                   : (unsigned)bst_u16(img, k->nodes, node * 2 + 1);
        node = split_step(img, k, b, (unsigned char)s[i]);
        if (node < 0) return 0;
    }
    if (node < 0) return 0;
    return bst_u8(img, k->nodes, node * 4 + 1);
}

/* Walks from `from` towards `to` while the trie follows, and answers the
   position just past the longest entry it passed through. */
static int split_longest(const bst_image *img, const struct split_trie *k,
                         const char *s, int from, int to, unsigned base,
                         int dir) {
    int at = from, node = -1;
    for (int i = from; dir > 0 ? i <= to : i >= to; i += dir) {
        unsigned b = node < 0 ? base
                   : (unsigned)bst_u16(img, k->nodes, node * 2 + 1);
        node = split_step(img, k, b, (unsigned char)s[i]);
        if (node < 0) break;
        if (bst_u8(img, k->nodes, node * 4 + 1)) at = i + dir;
    }
    return at;
}

int bst_word_split(const bst_image *img, const char *word, int *cut, int max) {
    if (!img->t.split_tab || !cut || max <= 0) return 0;

    char s[BST_WORD_MAX * 2];
    int n = 0;
    for (; word[n] && n + 2 < (int)sizeof s; n++) {
        int c = (unsigned char)word[n];
        s[n] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    s[n] = 0;
    if (n < 3) return 0;

    struct split_trie k;
    split_load(img, &k);

#define VOW(i) ((i) >= 0 && (i) < n && split_vowel(img, (unsigned char)s[i]))

    /* The build's own prefixes come off the front and its own endings off the
       back, so that neither is cut into. One only counts when the consonants
       beside it could open or close a part on their own. */
    int lo = 0, hi = n - 1;
    for (;;) {
        int at = split_longest(img, &k, s, lo, hi, k.prefix, 1);
        if (at == lo || VOW(at)) break;
        int e = at;
        while (e + 1 < n && !VOW(e + 1)) e++;
        if (e > at && !split_value(img, &k, s, at, e, k.onset)) break;
        if (at >= hi) break;
        lo = at;
    }
    for (;;) {
        int at = split_longest(img, &k, s, hi, lo, k.suffix, -1);
        if (at == hi || VOW(at)) break;
        int e = at;
        while (e > 0 && !VOW(e - 1)) e--;
        if (e < at && !split_value(img, &k, s, e, at, k.coda)) break;
        if (at <= lo) break;
        hi = at;
    }

    /* Every run of two or more consonants between the first vowel and the
       last is a candidate. */
    int cs[10], ce[10], nc = 0;
    int a, z;
    if (VOW(lo)) a = lo + 1;
    else { a = lo + 1; while (a < n && !VOW(a)) a++; a++; }
    if (VOW(hi)) z = hi - 1;
    else { z = hi - 1; while (z > 0 && !VOW(z)) z--; z--; }
    while (a <= z && nc < 10) {
        if (VOW(a) || VOW(a + 1)) { a++; continue; }
        cs[nc] = a;
        a += 2;
        while (a < n && !VOW(a)) a++;
        ce[nc] = a - 1;
        nc++;
        a++;
    }

    /* A candidate is taken where the two halves of the cluster score highest
       together, and only if it leaves three letters on either side. */
    int nsplit = 0, prev = lo - 1;
    for (int i = 0; i < nc && nsplit < 5 && nsplit < max; i++) {
        if (split_value(img, &k, s, cs[i], ce[i], k.nosplit)) continue;
        int at;
        if (ce[i] - cs[i] == 1) at = cs[i];
        else {
            unsigned best = 0;
            at = -1;
            for (int p = cs[i]; p < ce[i]; p++) {
                int c1 = split_value(img, &k, s, cs[i], p, k.coda);
                int c2 = split_value(img, &k, s, p + 1, ce[i], k.onset);
                if (!c1 || !c2) continue;
                unsigned sc = (unsigned)((c1 + c2) & 0xFFFF);
                if (sc > best) { best = sc; at = p; }
            }
            if (!best) continue;
        }
        if (at - prev < 3 || hi - at < 3) continue;
        cut[nsplit++] = at;
        prev = at;
    }
#undef VOW
    return nsplit;
}

void bst_normalise(const bst_image *img, const char *word, bst_word *out) {
    char w[BST_WORD_MAX];
    int n = 0;

    memset(out, 0, sizeof *out);
    for (; word[n] && n < BST_WORD_MAX - 4; n++) {
        char c = word[n];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        w[n] = c == '\'' ? '`' : c;
    }
    w[n] = 0;

    int vowels = 0;
    for (int i = 0; i < n; i++)
        if (strchr("aeiouy", w[i])) vowels++;

    int end = n - 1, flags = 0;

    if (img->t.strip_kind == 1) {
        /* The German set. The stem has to end in a consonant or one of four
           diphthongs, and a few endings before it block the strip. */
        int last = end >= 0 ? (unsigned char)w[end] : 0;
        int take = last == 'e' ? 1
                 : (last == 'm' || last == 'n' || last == 'r' || last == 's') &&
                   end >= 1 && w[end - 1] == 'e' ? 2 : 0;
        if (take && vowels > 1) {
            int i = end - take + 1;          /* the first stripped character */
            int c1 = i >= 1 ? (unsigned char)w[i - 1] : 0;
            int c2 = i >= 2 ? (unsigned char)w[i - 2] : 0;
            int ok = (attr(img, c1) & A_CONS) ||
                     (c2 == 'e' && c1 == 'i') || (c2 == 'a' && c1 == 'u') ||
                     (c2 == 'e' && c1 == 'u') || (c2 == 0x84 && c1 == 'u');
            if (c1 == 'g' && c2 == 'i') ok = 0;
            if (last == 'n' && c1 == 'h' && c2 == 'c') ok = 0;
            if (c1 == 's' && ((attr(img, c2) & A_VOWEL) || c2 == 'r' ||
                              c2 == 'l' || c2 == 'n' || c2 == 'm')) ok = 0;
            if (ok) {
                flags = last == 'e' ? 0x08 : last == 'm' ? 0x10
                      : last == 'n' ? 0x20 : last == 'r' ? 0x40 : 0x80;
                end = i - 1;
            }
        }
    }

    for (; !img->t.no_suffix && !img->t.strip_kind;) {
        int again = 0;
        int c = end >= 0 ? (unsigned char)w[end] : 0;

        if (c == 'd') {
            if (end >= 1 && w[end - 1] == 'e' && vowels > 1 &&
                (end < 2 || w[end - 2] != 'e')) {
                int k = stem(img, w, end - 2, vowels);
                if (k >= 0) { flags |= BST_SUF_ED; end = k; }
            }
        } else if (c == 'g') {
            if (end >= 2 && w[end - 1] == 'n' && w[end - 2] == 'i' && vowels > 1) {
                int k = stem(img, w, end - 3, vowels);
                if (k >= 0) { flags |= BST_SUF_ING; end = k; }
            }
        } else if (c == 's') {
            if (vowels != 0) {
                int prev = end >= 1 ? (unsigned char)w[end - 1] : 0;
                int take = 1, keep = end - 1;
                if (prev == '`') {
                    flags |= BST_SUF_APOS;
                    end -= 2;
                    again = 1;
                    take = 0;
                } else if (prev == 'i' || prev == 's' || prev == 'u') {
                    take = 0;
                } else if (prev == 'a' || prev == 'o' || prev == 'y') {
                    if (vowels == 1) take = 0;
                } else if (prev == 'd' || prev == 'g') {
                    again = 1;
                } else if (prev == 'e') {
                    if (vowels < 2) take = 0;
                    else {
                        int p2 = end >= 2 ? (unsigned char)w[end - 2] : 0;
                        int p3 = end >= 3 ? (unsigned char)w[end - 3] : 0;
                        if (p2 == 'h')      keep = (p3 == 's' || p3 == 'c') ? end - 2 : end - 1;
                        else if (p2 == 'i') { vowels--; again = 1; w[end - 2] = 'y'; keep = end - 2; }
                        else if (p2 == 'j' || p2 == 'o' || p2 == 'x') keep = end - 2;
                        else if (p2 == 's') {
                            keep = end - 1;
                            if (p3 == 'u' && vowels > 2) {
                                keep = end - 2;
                                if (end >= 4 && w[end - 4] == 'o') take = 0;
                            }
                        } else if (p2 == 'z') {
                            keep = (attr(img, p3) & A_CONS) ? end - 2 : end - 1;
                        } else keep = end - 1;
                    }
                }
                if (take) { flags |= BST_SUF_S; end = keep; }
            }
        } else if (c == 'y') {
            if (end >= 1 && w[end - 1] == 'l' && vowels > 1) {
                int p = end >= 2 ? (unsigned char)w[end - 2] : 0;
                int take = 1, keep = end - 2;
                if (p == 'a' || p == 'b' || p == 'o' || p == 'u') take = 0;
                else if (p == 'd' || p == 'g') { vowels--; again = 1; }
                else if (p == 'e') take = vowels >= 3;
                else if (p == 'i') {
                    if (vowels < 3 || (end >= 4 && w[end - 3] == 'r' && w[end - 4] == 'a'))
                        take = 0;
                    else { w[end - 2] = 'y'; out->y_from_i = 1; }
                } else if (p == 'l') {
                    if (vowels < 3) take = 0;
                    else {
                        int b = end >= 3 ? (unsigned char)w[end - 3] : 0;
                        if (b == 'a') {
                            if (end >= 5 && w[end - 4] == 'c' && w[end - 5] == 'i') keep = end - 4;
                        } else if (b != 'u' &&
                                   !(attr(img, end >= 4 ? (unsigned char)w[end - 4] : 0) & A_VOWEL)) {
                            take = 0;
                        }
                    }
                }
                if (take) { flags |= BST_SUF_LY; end = keep; }
            }
        }

        if (!again) break;
    }

    out->buf[0] = '_';
    int k = 0;
    for (; k <= end && k + 1 < BST_WORD_MAX - 3; k++) out->buf[k + 1] = w[k];
    out->buf[k + 1] = '_';
    out->buf[k + 2] = '_';
    out->buf[k + 3] = 0;
    out->len = k + 3;
    out->flags = flags;
    out->vowels = vowels;
}

/* ---- 16-bit modules ------------------------------------------------------

   An NE file keeps its segments unrelocated, and every place a pointer will go
   holds instead the offset of the next place to fix up. Nothing in the data
   means anything until that has been walked, so the library lays the module
   out the way the loader would -- one segment to a 64K window, addressed as
   (segment << 16) | offset -- and applies the fixups. An internal relocation
   then writes exactly that address form, which is why a pointer read out of
   the result needs no further translation. */

#define NE_WINDOWS 21
#define NE_WINDOW  0x10000u

static uint16_t ne16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t ne32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int bst_image_init_ne(bst_image *img, const void *data, size_t len,
                      const bst_tabmap *map) {
    const uint8_t *d = data;
    if (!d || len < 0x100) return -1;
    uint32_t ne = ne32(d + 0x3C);
    if (ne + 0x40 > len || d[ne] != 'N' || d[ne + 1] != 'E') return -1;

    int nseg = ne16(d + ne + 0x1C);
    uint32_t seg_off = ne16(d + ne + 0x22);
    int shift = ne16(d + ne + 0x32);
    if (!shift) shift = 9;
    if (nseg < 1 || nseg > BST_SECTIONS) return -1;

    uint8_t *flat = calloc(NE_WINDOWS, NE_WINDOW);
    if (!flat) return -1;

    memset(img, 0, sizeof *img);
    img->own = flat;
    img->image = flat;
    img->len = (size_t)NE_WINDOWS * NE_WINDOW;
    img->base = 0;
    img->t = *map;

    for (int i = 0; i < nseg; i++) {
        const uint8_t *s = d + ne + seg_off + i * 8;
        uint32_t sector = ne16(s), slen = ne16(s + 2), alloc = ne16(s + 6);
        if (!slen) slen = 0x10000;
        if (!alloc || alloc < slen) alloc = slen;
        uint32_t win = (uint32_t)(i + 1) * NE_WINDOW;
        if (sector) {
            uint32_t at = (uint32_t)sector << shift;
            uint32_t n = slen;
            if (at + n > len) n = (uint32_t)len - at;
            memcpy(flat + win, d + at, n);
        }
        img->sec[i].va = win;
        img->sec[i].vsize = alloc;
        img->sec[i].raw = win;
        img->sec[i].rawsize = alloc;
        img->nsec++;
    }

    /* The fixups. Only internal references matter: nothing the tables hold
       points at the host. */
    for (int i = 0; i < nseg; i++) {
        const uint8_t *s = d + ne + seg_off + i * 8;
        uint32_t sector = ne16(s), slen = ne16(s + 2), sflags = ne16(s + 4);
        if (!slen) slen = 0x10000;
        if (!(sflags & 0x100) || !sector) continue;
        uint32_t p = ((uint32_t)sector << shift) + slen;
        if (p + 2 > len) continue;
        int nrel = ne16(d + p);
        uint32_t win = (uint32_t)(i + 1) * NE_WINDOW;
        for (int k = 0; k < nrel; k++) {
            if (p + 2 + (uint32_t)(k + 1) * 8 > len) break;
            const uint8_t *r = d + p + 2 + k * 8;
            int at = r[0] & 0x0F, rt = r[1];
            uint32_t off = ne16(r + 2), a = ne16(r + 4), b = ne16(r + 6);
            if ((rt & 3) != 0) continue;
            uint32_t value;
            if (a == 0xFF) continue;          /* a movable entry, not a table */
            if (a < 1 || a > (uint32_t)nseg) continue;
            value = (a << 16) | (b & 0xFFFF);
            uint32_t cur = off;
            for (int guard = 0; guard < 4096; guard++) {
                if (cur == 0xFFFF || cur + 2 > NE_WINDOW) break;
                uint16_t next = (uint16_t)ne16(flat + win + cur);
                switch (at) {
                case 0:
                    flat[win + cur] = (uint8_t)value;
                    break;
                case 2:
                    flat[win + cur] = (uint8_t)(value >> 16);
                    flat[win + cur + 1] = (uint8_t)(value >> 24);
                    break;
                case 3:
                    flat[win + cur] = (uint8_t)value;
                    flat[win + cur + 1] = (uint8_t)(value >> 8);
                    if (cur + 4 <= NE_WINDOW) {
                        flat[win + cur + 2] = (uint8_t)(value >> 16);
                        flat[win + cur + 3] = (uint8_t)(value >> 24);
                    }
                    break;
                default:
                    flat[win + cur] = (uint8_t)value;
                    flat[win + cur + 1] = (uint8_t)(value >> 8);
                    break;
                }
                if (rt & 4) break;
                if (at == 0) break;
                cur = next;
            }
        }
    }
    return 0;
}

int bst_image_init_lifted(bst_image *img, const bst_lifted *d,
                          const bst_tabmap *map) {
    if (!d || !map || d->nsec > BST_SECTIONS) return -1;
    uint8_t *flat = calloc(d->size ? d->size : 1, 1);
    if (!flat) return -1;

    memset(img, 0, sizeof *img);
    img->own = flat;
    img->image = flat;
    img->len = d->size;
    img->base = d->base;
    img->nsec = d->nsec;
    for (int i = 0; i < d->nsec; i++) img->sec[i] = d->sec[i];
    img->t = *map;

    for (int i = 0; i < d->nchunk; i++) {
        const bst_chunk *c = &d->chunk[i];
        if ((size_t)c->off + c->len > d->size) continue;
        memcpy(flat + c->off, c->bytes, c->len);
    }
    return 0;
}

void bst_image_free(bst_image *img) {
    if (!img) return;
    free(img->own);
    img->own = NULL;
    img->image = NULL;
}
