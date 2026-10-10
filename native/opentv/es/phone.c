/*
 * Load a phoneme's voice parameters into the track set.
 *
 * Everything here is a table read.  `g_10057ce8` points at a selector that
 * turns a phoneme letter into a small id -- 0 to 43 for the letters the
 * engine uses -- and that id indexes eight byte tables of 48 entries laid
 * out 0x30 apart from 0x10057e28, one per track group, each scaled the way
 * that track wants it: x4 for track 9, x8 + 500 for 10, x16 for 11 and 12,
 * x2 for 13 to 15, and raw for track 0.  The same eight tables are read a
 * second time at the end for the *following* phoneme, into trk_55c.
 *
 * Three id thresholds gate the rest: below 14 it fills s3_468 and trk_540,
 * at or above 23 it fills tracks 1 to 8 from eight more tables reached
 * through pointer variables (and below 23 uses six fixed values instead),
 * and at or above 39 it derives track 16's level rather than using 250.
 *
 * Tracks 18 to 21 and part of track 12 come from the voice rather than the
 * phoneme, indexed by stage_ctx[3].voice.
 *
 * Five letters -- H J Q V W -- have 255 in the selector, which the original
 * sign-extends to -1 and then indexes every table with.  None of them is a
 * phoneme the engine produces; they are not in the twenty-three lang/spa/engine/cluster.c
 * lists nor among the five vowels.
 */
#include "es_engine.h"

/* @0x10058618 */
extern const uint8_t g_10058618[0x200];
/* the phoneme letter -> id selector, reached through a pointer variable */
/* @0x10057ce8 */
extern const tv_ref g_10057ce8p;

/* the eight per-track-group tables, 0x30 apart */
/* @0x10057e28 */
extern const uint8_t g_10057e28[48];
/* @0x10057e58 */
extern const uint8_t g_10057e58[48];
/* @0x10057e88 */
extern const uint8_t g_10057e88[48];
/* @0x10057eb8 */
extern const uint8_t g_10057eb8[48];
/* @0x10057ee8 */
extern const uint8_t g_10057ee8[48];
/* @0x10057f18 */
extern const uint8_t g_10057f18[48];
/* @0x10057f48 */
extern const uint8_t g_10057f48[48];
/* @0x10057f78 */
extern const uint8_t g_10057f78[48];

/* tracks 1 to 8, eight tables reached through pointer variables 0x20 apart */
/* @0x100580f8 */
extern const tv_ref g_100580f8;
/* @0x10058118 */
extern const tv_ref g_10058118;
/* @0x10058138 */
extern const tv_ref g_10058138;
/* @0x10058158 */
extern const tv_ref g_10058158;
/* @0x10058178 */
extern const tv_ref g_10058178;
/* @0x10058198 */
extern const tv_ref g_10058198;
/* @0x100581b8 */
extern const tv_ref g_100581b8;
/* @0x100581d8 */
extern const tv_ref g_100581d8;
/* track 16's, likewise */
/* @0x10058430 */
extern const tv_ref g_10058430;

/* the low-id extras */
/* @0x100580d0 */
extern const uint8_t g_100580d0[48];
/* @0x10058058 */
extern const uint8_t g_10058058[16];
/* @0x10058068 */
extern const uint8_t g_10058068[16];
/* @0x10058078 */
extern const uint8_t g_10058078[16];

/* two int32 tables indexed by the id, and one by the pair of s3_448/s3_44c */
/* @0x10058088 */
extern const int32_t g_10058088[14];
/* @0x10057fa8 */
extern const int32_t g_10057fa8[48];
/* @0x10058200 */
extern const int32_t g_10058200[16];
/* the shape every track starts from */
/* @0x10058410 */
extern const uint8_t g_10058410[22];

/* per-voice, indexed by stage_ctx[3].voice */
/* @0x1004c8a0 */
extern const int32_t g_1004c8a0[];
/* @0x1004c7e8 */
extern const uint8_t g_1004c7e8[16];
/* @0x1004c7f8 */
extern const uint8_t g_1004c7f8[16];
/* @0x1004c808 */
extern const uint8_t g_1004c808[16];
/* @0x1004c818 */
extern const uint8_t g_1004c818[16];

static int32_t ph_cls0(uint8_t v)
{
    return (int32_t)(int16_t)(int8_t)v;
}

static int32_t ph_cls100(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x100);
}

static int32_t ph_cls80(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x80);
}

static int32_t ph_cls180(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x180);
}

/* @0x1001b440 */
void TV_THISCALL Stage3_LoadPhone(Engine *self)
{
    const uint8_t *sel = TV_REF(uint8_t, g_10057ce8p);
    Node *ctl = self->stage_ctx[3].ctl;
    Node *cur = self->stage_ctx[3].cur;
    int32_t voice = self->stage_ctx[3].voice;
    /* the current phoneme's id is taken unsigned, the control one signed */
    int32_t id_cur = sel[ph_cls0(cur->value)];
    int32_t id = (int8_t)sel[ph_cls0(ctl->value)];
    uint8_t f;
    int32_t i;

    self->s3_450 = id_cur >= 0xe ? self->s3_454 : g_10058088[id_cur];
    self->s3_448 = self->s3_44c;
    self->s3_478 = 0x33;
    self->s3_454 = g_10057fa8[id];
    self->s3_480 = 0x39;

    f = g_10058618[ph_cls100(ctl->value)];
    if (f & 2)
        self->s3_44c = 0;
    else if (f & 1)
        self->s3_44c = 3;
    else
        self->s3_44c = (g_10058618[ph_cls0(ctl->value)] & 2) ? 1 : 2;

    self->trk_param[9][6] = (int32_t)g_10057e28[id] << 2;
    self->trk_param[10][6] = ((int32_t)g_10057e58[id] << 3) + 0x1f4;
    self->trk_param[11][6] = (int32_t)g_10057e88[id] << 4;
    self->trk_param[12][6] = ((int32_t)g_10057eb8[id] << 4) +
                             es_v_c8a0(voice);
    self->trk_param[13][6] = (int32_t)g_10057ee8[id] * 2;
    self->trk_param[14][6] = (int32_t)g_10057f18[id] * 2;
    self->trk_param[15][6] = (int32_t)g_10057f48[id] * 2;
    self->trk_param[0][6] = g_10057f78[id];
    self->trk_param[17][6] = (int32_t)ctl->b15 * 2;
    self->trk_param[18][6] = es_v_p18(voice);
    self->trk_param[19][6] = es_v_p19(voice);
    self->trk_param[20][6] = es_v_p20(voice);
    self->trk_param[21][6] = es_v_p21(voice);

    if (id >= 0x17) {
        self->trk_param[1][6] = TV_REF(uint8_t, g_100580f8)[id];
        self->trk_param[2][6] = TV_REF(uint8_t, g_10058118)[id];
        self->trk_param[3][6] = TV_REF(uint8_t, g_10058138)[id];
        self->trk_param[4][6] = TV_REF(uint8_t, g_10058158)[id];
        self->trk_param[5][6] = TV_REF(uint8_t, g_10058178)[id];
        self->trk_param[6][6] = TV_REF(uint8_t, g_10058198)[id];
        self->trk_param[7][6] = TV_REF(uint8_t, g_100581b8)[id];
        self->trk_param[8][6] = TV_REF(uint8_t, g_100581d8)[id];
    } else {
        self->trk_param[1][6] = 0;
        self->trk_param[2][6] = 0;
        self->trk_param[3][6] = 0x3c;
        self->trk_param[4][6] = 0x3c;
        self->trk_param[5][6] = 0x3c;
        self->trk_param[6][6] = 0x3c;
        self->trk_param[7][6] = 0x3c;
        self->trk_param[8][6] = 0;
    }

    if (id < 0xe) {
        self->s3_468 = g_100580d0[id];
        self->trk_540[0] = (int32_t)g_10058058[id] << 2;
        self->trk_540[1] = ((int32_t)g_10058068[id] << 3) + 0x1f4;
        self->trk_540[2] = (int32_t)g_10058078[id] << 4;
    }

    if (id >= 0x27)
        self->trk_param[16][6] = (int32_t)TV_REF(uint8_t, g_10058430)[id] * 4 + 0xc0;
    else
        self->trk_param[16][6] = 0xfa;

    for (i = 0; i < 22; i++) {
        self->trk_param[i][2] = g_10058410[i];
        self->trk_param[i][1] = 0;
        self->trk_param[i][0] = 7;
        self->trk_param[i][3] = (int32_t)self->stage_ctx[3].ctl->arg;
        self->trk_4e8[i] = g_10058200[self->s3_448 * 4 + self->s3_44c];
    }

    self->trk_param[21][0] = 4;
    self->trk_param[20][0] = 4;

    /* and the same eight tables again for the following phoneme */
    {
        int32_t nid = (int8_t)TV_REF(uint8_t, g_10057ce8p)[
            ph_cls0(self->stage_ctx[3].scan->value)];

        self->trk_55c[9] = (int32_t)g_10057e28[nid] << 2;
        self->trk_55c[10] = ((int32_t)g_10057e58[nid] << 3) + 0x1f4;
        self->trk_55c[11] = (int32_t)g_10057e88[nid] << 4;
        if (nid >= 0x17) {
            self->trk_55c[13] = (int32_t)g_10057ee8[nid] * 2;
            self->trk_55c[14] = (int32_t)g_10057f18[nid] * 2;
            self->trk_55c[15] = (int32_t)g_10057f48[nid] * 2;
        }
    }
}

/*
 * Which of the two phonemes are flagged, as two bits.
 *
 * Bit 1 is set when the current phoneme's class flag has 2, bit 0 when the
 * following one does, so the result is 0 to 3.  It is cached in s3_8788 and
 * also returned; the two helpers below branch on it, and the bit for each
 * phoneme decides whether that phoneme's half of the work runs.
 *
 * The original writes this as four separate stores with the two tests
 * repeated in each arm rather than as an or of two bits.
 */
/* @0x10017550 */
int32_t TV_THISCALL Stage3_BlendMode(Engine *self)
{
    int cur_f = (g_10058618[ph_cls100(self->stage_ctx[3].cur->value)] & 2) != 0;
    int scan_f = (g_10058618[ph_cls100(self->stage_ctx[3].scan->value)] & 2) != 0;

    self->s3_8788 = (cur_f ? 2 : 0) | (scan_f ? 1 : 0);
    return self->s3_8788;
}

/*
 * Set tracks 9, 10 and 11 from one of two triples, or from their average.
 *
 * `mode` is what Stage3_BlendMode returned: 3 takes the midpoint of the two
 * triples, 2 the first, 1 the second, and 0 leaves the three tracks alone.
 * The midpoint is an arithmetic shift rather than a divide, so it floors
 * rather than truncating toward zero.
 */
/* @0x10017630 */
void TV_THISCALL Track_SetTriple(Engine *self, int32_t a1, int32_t a2,
                                 int32_t a3, int32_t b1, int32_t b2,
                                 int32_t b3, int32_t mode)
{
    if (mode == 3) {
        self->trk_param[9][6] = (a1 + b1) >> 1;
        self->trk_param[10][6] = (a2 + b2) >> 1;
        self->trk_param[11][6] = (a3 + b3) >> 1;
    } else if (mode == 2) {
        self->trk_param[9][6] = a1;
        self->trk_param[10][6] = a2;
        self->trk_param[11][6] = a3;
    } else if (mode == 1) {
        self->trk_param[9][6] = b1;
        self->trk_param[10][6] = b2;
        self->trk_param[11][6] = b3;
    }
}

/*
 * Put one or both phonemes into a vowel class, 1 to 5.
 *
 * `mode` is Stage3_BlendMode's again: bit 0 classifies the following
 * phoneme into s3_87a8 and bit 1 the current one into s3_87a4, so a phoneme
 * is only classified when it is the flagged one.  A phoneme that matches
 * nothing leaves its field untouched.
 *
 * The two halves group the alphabet differently.  For the following phoneme
 * it is five plain lists -- A a v / E e w / I h i j k / O o u / U m p q t --
 * which read as the five Spanish vowels with their allophones.  For the
 * current one only the first two are lists; the rest go through the class
 * flags, and class 3 is taken before the 'O' 'j' 'q' list is even looked at.
 */
/* @0x100176c0 */
void TV_THISCALL Stage3_VowelClass(Engine *self, int32_t mode)
{
    if (mode == 0)
        return;

    if (mode == 1 || mode == 3) {
        int32_t c = self->stage_ctx[3].scan->value;

        if (c == 'A' || c == 'a' || c == 'v')
            self->s3_87a8 = 1;
        else if (c == 'E' || c == 'e' || c == 'w')
            self->s3_87a8 = 2;
        else if (c == 'I' || c == 'h' || c == 'i' || c == 'j' || c == 'k')
            self->s3_87a8 = 3;
        else if (c == 'O' || c == 'o' || c == 'u')
            self->s3_87a8 = 4;
        else if (c == 'U' || c == 'm' || c == 'p' || c == 'q' || c == 't')
            self->s3_87a8 = 5;
    }

    if (mode == 2 || mode == 3) {
        int32_t c = self->stage_ctx[3].cur->value;

        if (c == 'A' || c == 'h' || c == 'm')
            self->s3_87a4 = 1;
        else if (c == 'E' || c == 'i' || c == 'p')
            self->s3_87a4 = 2;
        else if (g_10058618[ph_cls180((uint8_t)c)] & 0x40)
            self->s3_87a4 = 3;
        else if (c == 'O' || c == 'j' || c == 'q')
            self->s3_87a4 = 4;
        else if (g_10058618[ph_cls80((uint8_t)c)] & 4)
            self->s3_87a4 = 5;
    }
}

/*
 * Fifteen more tables, all reached through pointer variables 0x20 apart:
 * five vowel classes by three tracks.  The class picks the row, the phoneme
 * id picks the entry, and the scaling is the track's own -- x4, x8 + 500,
 * x16 -- for the eighth and last time in this engine.
 */
/* @0x10058454 */
extern const tv_ref g_10058454;
/* @0x10058474 */
extern const tv_ref g_10058474;
/* @0x10058494 */
extern const tv_ref g_10058494;
/* @0x100584b4 */
extern const tv_ref g_100584b4;
/* @0x100584d4 */
extern const tv_ref g_100584d4;
/* @0x100584f4 */
extern const tv_ref g_100584f4;
/* @0x10058514 */
extern const tv_ref g_10058514;
/* @0x10058534 */
extern const tv_ref g_10058534;
/* @0x10058554 */
extern const tv_ref g_10058554;
/* @0x10058574 */
extern const tv_ref g_10058574;
/* @0x10058594 */
extern const tv_ref g_10058594;
/* @0x100585b4 */
extern const tv_ref g_100585b4;
/* @0x100585d4 */
extern const tv_ref g_100585d4;
/* @0x100585f4 */
extern const tv_ref g_100585f4;
/* @0x10058614 */
extern const tv_ref g_10058614;

/*: one triple out of three tables, each scaled for its track */
static void trip_load(int32_t *out, const uint8_t *t0, const uint8_t *t1,
                      const uint8_t *t2, int32_t id)
{
    out[0] = (int32_t)t0[id] << 2;
    out[1] = ((int32_t)t1[id] << 3) + 0x1f4;
    out[2] = (int32_t)t2[id] << 4;
}

/*: the three tables belonging to a vowel class */
static void trip_for(int cls, const uint8_t **t0, const uint8_t **t1,
                     const uint8_t **t2)
{
    switch (cls) {
    case 1: *t0 = TV_REF(uint8_t, g_100584d4); *t1 = TV_REF(uint8_t, g_10058534);
             *t2 = TV_REF(uint8_t, g_100585d4); break;
    case 2: *t0 = TV_REF(uint8_t, g_10058494); *t1 = TV_REF(uint8_t, g_10058554);
             *t2 = TV_REF(uint8_t, g_100585f4); break;
    case 3: *t0 = TV_REF(uint8_t, g_10058454); *t1 = TV_REF(uint8_t, g_10058574);
             *t2 = TV_REF(uint8_t, g_10058614); break;
    case 4: *t0 = TV_REF(uint8_t, g_100584b4); *t1 = TV_REF(uint8_t, g_10058514);
             *t2 = TV_REF(uint8_t, g_100585b4); break;
    default: *t0 = TV_REF(uint8_t, g_10058474);
             *t1 = TV_REF(uint8_t, g_100584f4);
             *t2 = TV_REF(uint8_t, g_10058594); break;
    }
}

/*
 * Load the two level triples for a transition and hand them to
 * Track_SetTriple.
 *
 * Which triples get filled follows Stage3_BlendMode: bit 1 fills the current
 * phoneme's from its vowel class, bit 0 the following phoneme's from its.
 * Both are indexed by the *control* phoneme's id, not by the phoneme whose
 * class chose the table.
 *
 * Each class also has a tail that fires when the control phoneme is a nasal.
 * The two halves differ there: the current-phoneme half separates 'N' from
 * '~' for classes 4 and 5, and the following-phoneme half treats them alike.
 *
 * One consequence of the ordering is worth stating: whenever the following
 * phoneme's half runs at all, s3_8788 is set to 1 at the end of it.  So mode
 * 3, which filled both triples, hands Track_SetTriple a mode of 1 and the
 * average it could have taken is never used.
 */
/* @0x10016fe0 */
void TV_THISCALL Stage3_LoadTriples(Engine *self)
{
    const uint8_t *sel = TV_REF(uint8_t, g_10057ce8p);
    Node *ctl;
    int32_t c_ctl, id, mode;
    const uint8_t *t0, *t1, *t2;

    self->trk_param[16][0] = 5;
    self->trk_4e8[16] = 0x7ffe;
    self->trk_param[0][0] = 4;

    mode = Stage3_BlendMode(self);
    self->s3_8788 = mode;
    if (mode == 0)
        return;
    Stage3_VowelClass(self, mode);

    ctl = self->stage_ctx[3].ctl;
    c_ctl = ctl->value;
    id = (int8_t)sel[ph_cls0((uint8_t)c_ctl)];

    if (mode == 2) {
        int cls = self->s3_87a4;

        if ((uint32_t)(cls - 1) <= 4u) {
            trip_for(cls, &t0, &t1, &t2);
            trip_load(self->s3_cur_trip, t0, t1, t2, id);
            switch (cls) {
            case 2:
                if (c_ctl == 'N' || c_ctl == '~')
                    self->trk_param[13][6] = 0xa0;
                break;
            case 3:
                if (c_ctl == 'M' || c_ctl == 'N' || c_ctl == '~')
                    self->trk_param[13][6] = 0xfa;
                break;
            case 4:
                if (c_ctl == 'N') {
                    self->trk_param[12][6] = 0xe10;
                    self->trk_param[13][6] = 0x64;
                    self->trk_param[14][6] = 0x12c;
                    self->trk_param[15][6] = 0x46;
                } else if (c_ctl == '~') {
                    self->trk_param[12][6] = 0xe10;
                    self->trk_param[13][6] = 0xc8;
                    self->trk_param[14][6] = 0x12c;
                    self->trk_param[15][6] = 0xfa;
                }
                break;
            case 5:
                if (c_ctl == 'N' || c_ctl == '~') {
                    self->trk_param[13][6] = c_ctl == 'N' ? 0x46 : 0xfa;
                    self->trk_param[14][6] = 0x12c;
                    self->trk_param[15][6] = 0x12c;
                }
                break;
            default:
                break;
            }
        }
    }

    if (mode == 1 || mode == 3) {
        int cls = self->s3_87a8;

        if ((uint32_t)(cls - 1) <= 4u) {
            trip_for(cls, &t0, &t1, &t2);
            trip_load(self->s3_next_trip, t0, t1, t2, id);
            switch (cls) {
            case 2:
                if (c_ctl == 'N' || c_ctl == '~')
                    self->trk_param[13][6] = 0xa0;
                break;
            case 3:
                if (c_ctl == 'M' || c_ctl == 'N' || c_ctl == '~')
                    self->trk_param[13][6] = 0xfa;
                break;
            case 4:
                if (c_ctl == 'N' || c_ctl == '~') {
                    self->trk_param[12][6] = 0xe10;
                    self->trk_param[13][6] = 0x64;
                    self->trk_param[14][6] = 0x12c;
                    self->trk_param[15][6] = 0x46;
                }
                break;
            case 5:
                if (c_ctl == 'N' || c_ctl == '~') {
                    self->trk_param[13][6] = 0x46;
                    self->trk_param[14][6] = 0x12c;
                    self->trk_param[15][6] = 0x12c;
                }
                break;
            default:
                break;
            }
        }
        self->s3_8788 = 1;
    }

    Track_SetTriple(self, self->s3_cur_trip[0], self->s3_cur_trip[1],
                    self->s3_cur_trip[2], self->s3_next_trip[0],
                    self->s3_next_trip[1], self->s3_next_trip[2],
                    self->s3_8788);
}

/*: the two corrections that recur across the arms below. */
static void trip_fix_d(Engine *self)
{
    self->trk_param[1][6] = 0x38;
    self->trk_param[5][6] = 0x3c;
    self->trk_param[6][6] = 0x41;
    self->trk_param[7][6] = 0x39;
}

static void trip_fix_g(Engine *self)
{
    self->trk_param[4][6] = 0x34;
    self->trk_param[6][6] = 0x41;
}

/*
 * Stage3_LoadTriples' sibling, and the differences are the point.
 *
 * The skeleton is the same -- the same fifteen class tables, the same two
 * triples, the same call to Track_SetTriple at the end -- but three things
 * differ:
 *
 *   - the current phoneme's half runs for mode 3 as well as mode 2, so when
 *     both phonemes are flagged both triples are filled from their own
 *     classes rather than one of them being skipped;
 *   - inside that half the per-class corrections are skipped when the mode
 *     is 3, since the following phoneme's half will make its own;
 *   - s3_8788 is left alone, so Track_SetTriple gets the real mode.  This is
 *     the one caller that reaches its midpoint branch: in
 *     Stage3_LoadTriples the mode is forced to 1 first.
 *
 * The prologue does two things of its own: a blend weight of 0x7350 on
 * tracks 9 to 11 when the control phoneme is flagged 0x40 and the current
 * one flagged 2, and a blend shape on tracks 9 to 16 of 9, 7 or 5 depending
 * on how many of the two phonemes are flagged 1 -- with track 9 dropped to
 * 1 outright after a trill.
 */
/* @0x10015b30 */
void TV_THISCALL Stage3_BlendTriples(Engine *self)
{
    const uint8_t *sel = TV_REF(uint8_t, g_10057ce8p);
    Node *ctl = self->stage_ctx[3].ctl;
    int32_t c_ctl = ctl->value;
    int32_t c_cur = self->stage_ctx[3].cur->value;
    int32_t shape, id, mode;
    const uint8_t *t0, *t1, *t2;
    int32_t i;

    if ((g_10058618[ph_cls100((uint8_t)c_ctl)] & 0x40) &&
        (g_10058618[ph_cls0((uint8_t)c_cur)] & 2)) {
        self->trk_4e8[11] = 0x7350;
        self->trk_4e8[10] = 0x7350;
        self->trk_4e8[9] = 0x7350;
    }

    shape = 9;
    if (g_10058618[ph_cls180((uint8_t)c_ctl)] & 1) {
        shape = 7;
        if (g_10058618[ph_cls180((uint8_t)c_cur)] & 1)
            shape = 5;
    }
    for (i = 0; i < 8; i++)
        self->trk_param[9 + i][2] = shape;
    if (self->stage_ctx[3].ctl->value == 'R')
        self->trk_param[9][2] = 1;
    if (self->stage_ctx[3].ctl->value == 'd' &&
        self->stage_ctx[3].scan->value == 'L')
        trip_fix_d(self);

    mode = Stage3_BlendMode(self);
    self->s3_8788 = mode;
    if (mode == 0)
        return;
    Stage3_VowelClass(self, mode);

    c_ctl = self->stage_ctx[3].ctl->value;
    id = (int8_t)sel[ph_cls0((uint8_t)c_ctl)];

    if (mode == 2 || mode == 3) {
        int cls = self->s3_87a4;

        if ((uint32_t)(cls - 1) <= 4u) {
            trip_for(cls, &t0, &t1, &t2);
            trip_load(self->s3_cur_trip, t0, t1, t2, id);
            /* mode 3 leaves the corrections to the other half */
            if (mode != 3) {
                if (cls == 1 && c_ctl == 'g') {
                    self->trk_param[0][6] = 0x28;
                    self->trk_param[3][6] = 0x2b;
                    self->trk_param[4][6] = 0x34;
                    self->trk_param[5][6] = 0x2e;
                    self->trk_param[7][6] = 0x24;
                } else if (cls == 4) {
                    if (c_ctl == 'g')
                        trip_fix_g(self);
                    if (c_ctl == 'L')
                        self->trk_param[0][6] = 0x36;
                } else if (cls == 5) {
                    if (c_ctl == 'g')
                        trip_fix_g(self);
                    if (c_ctl == 'd') {
                        self->trk_param[4][6] = 0x38;
                        self->trk_param[6][6] = 0x30;
                        self->trk_param[7][6] = 0x24;
                    }
                }
            }
        }
    }

    if (mode == 1 || mode == 3) {
        int cls = self->s3_87a8;

        if ((uint32_t)(cls - 1) <= 4u) {
            trip_for(cls, &t0, &t1, &t2);
            trip_load(self->s3_next_trip, t0, t1, t2, id);
            switch (cls) {
            case 1:
                if (c_ctl == 'g') {
                    self->trk_param[0][6] = 0x28;
                    self->trk_param[3][6] = 0x2b;
                    self->trk_param[4][6] = 0x34;
                    self->trk_param[5][6] = 0x2e;
                    self->trk_param[7][6] = 0x24;
                } else if (c_ctl == 'b') {
                    self->trk_param[8][6] = 0x2d;
                }
                if (c_ctl == 'd')
                    trip_fix_d(self);
                break;
            case 2:
            case 3:
                if (c_ctl == 'd')
                    trip_fix_d(self);
                break;
            case 4:
                if (c_ctl == 'L')
                    self->trk_param[0][6] = 0x36;
                else if (c_ctl == 'b')
                    self->trk_param[8][6] = 0x2d;
                if (c_ctl == 'g')
                    trip_fix_g(self);
                if (c_ctl == 'd')
                    trip_fix_d(self);
                break;
            default:
                if (c_ctl == 'd')
                    trip_fix_d(self);
                if (c_ctl == 'g')
                    trip_fix_g(self);
                break;
            }
        }
    }

    Track_SetTriple(self, self->s3_cur_trip[0], self->s3_cur_trip[1],
                    self->s3_cur_trip[2], self->s3_next_trip[0],
                    self->s3_next_trip[1], self->s3_next_trip[2], mode);
}

/*: the two corrections that recur across sub_10016460's ten arms. */
static void stop_fix_t(Engine *self)
{
    self->trk_param[4][6] = 0x24;
    self->trk_param[5][6] = 0x3a;
    self->trk_param[6][6] = 0x30;
    self->trk_param[7][6] = 0x2f;
}

static void stop_fix_tail(Engine *self)
{
    self->trk_param[13][6] = 0x50;
    self->trk_param[14][6] = 0x64;
    self->trk_param[15][6] = 0x8c;
}

/*
 * A stop closure, and then the triples for whatever follows it.
 *
 * The prologue is what makes this one different from its two siblings.  The
 * control phoneme names a stop, and each stop has a closure length: 6 for
 * C, 3 for K, 2 for T, 1 for P B and D, 1 or 2 for G depending on whether a
 * U follows, and for Y the whole of track 1's remaining duration when a
 * vowel follows.  Anything else gets 0.
 *
 * Track 1 is then blended back over its buffered run, and the difference
 * between its duration and that closure length is filled with zero -- so the
 * closure is silence, and the time it does not use is handed back by
 * advancing the cursor.  C does the same to track 2.
 *
 * The rest is the same shape as Stage3_LoadTriples, with its own per-class
 * corrections and, like it, the mode forced to 1 once the following
 * phoneme's half has run.
 */
/* @0x10016460 */
void TV_THISCALL Stage3_StopClosure(Engine *self)
{
    const uint8_t *sel = TV_REF(uint8_t, g_10057ce8p);
    int32_t c_ctl = self->stage_ctx[3].ctl->value;
    int32_t c_scan = self->stage_ctx[3].scan->value;
    int32_t closure, id, mode, wr;
    const uint8_t *t0, *t1, *t2;

    self->s3_474 = self->trk_param[1][3];

    switch ((int8_t)c_ctl) {
    case 'C': closure = 6; break;
    case 'P': closure = 1; break;
    case 'T': closure = 2; break;
    case 'K': closure = 3; break;
    case 'B': closure = 1; break;
    case 'D': closure = 1; break;
    case 'G': closure = c_scan == 'U' ? 2 : 1; break;
    case 'Y':
        closure = Is_Vowel((uint8_t)c_scan) ? self->trk_param[1][3] : 0;
        break;
    default: closure = 0; break;
    }
    self->s3_460 = closure;

    if (c_ctl == 'P' && c_scan == 'r')
        self->trk_param[5][6] = 0x32;

    wr = self->trk_wr[1];
    Track_BlendDelta(self, self->trk_buf[1], 1, wr, 3,
                     wr - self->trk_rd[1], -0x14);

    if (self->s3_460 < self->trk_param[1][3]) {
        int32_t pad = self->trk_param[1][3] - self->s3_460;

        self->s3_474 = pad;
        Track_Fill(self->trk_buf[1], self->trk_wr[1], pad, 0);
        self->trk_wr[1] += self->s3_474;
    }
    self->trk_490[1] = 0;
    self->trk_param[1][3] = self->s3_460;

    if (self->stage_ctx[3].ctl->value == 'C') {
        Track_Fill(self->trk_buf[2], self->trk_wr[2], self->s3_474, 0);
        self->trk_wr[2] += self->s3_474;
        self->trk_param[2][3] = self->s3_460;
        self->trk_param[2][0] = 5;
        self->trk_490[2] = 0;
    }
    self->trk_param[1][0] = 4;

    mode = Stage3_BlendMode(self);
    self->s3_8788 = mode;
    if (mode == 0)
        return;
    Stage3_VowelClass(self, mode);

    c_ctl = self->stage_ctx[3].ctl->value;
    id = (int8_t)sel[ph_cls0((uint8_t)c_ctl)];

    if (mode == 2) {
        int cls = self->s3_87a4;

        if ((uint32_t)(cls - 1) <= 4u) {
            trip_for(cls, &t0, &t1, &t2);
            trip_load(self->s3_cur_trip, t0, t1, t2, id);
            if (cls == 1) {
                if (g_10058618[ph_cls100((uint8_t)c_ctl)] & 0x80) {
                    self->trk_param[3][6] = 0x2b;
                    self->trk_param[5][6] = 0x2e;
                    self->trk_param[7][6] = 0x24;
                } else if (c_ctl == 'B') {
                    self->trk_param[8][6] = 0x32;
                } else if (c_ctl == 'D') {
                    self->trk_param[6][6] = 0x3b;
                    self->trk_param[7][6] = 0x37;
                } else if (c_ctl == 'C') {
                    self->trk_param[14][6] = 0x96;
                    self->trk_param[15][6] = 0xfa;
                } else if (c_ctl == 'P') {
                    self->trk_param[5][6] = 0x32;
                }
            } else if (cls == 4 || cls == 5) {
                if (c_ctl == 'T') {
                    stop_fix_t(self);
                } else if (c_ctl == 'D') {
                    self->trk_param[4][6] = 0x38;
                    self->trk_param[6][6] = 0x30;
                    self->trk_param[7][6] = 0x24;
                } else if (c_ctl == 'C') {
                    if (cls == 4) {
                        self->trk_param[3][6] = 0x1e;
                        self->trk_param[4][6] = 0x48;
                    } else {
                        self->trk_param[4][6] = 0x3b;
                        self->trk_param[5][6] = 0x38;
                    }
                    self->trk_param[14][6] = 0x64;
                    self->trk_param[15][6] = 0xf0;
                } else if (g_10058618[ph_cls100((uint8_t)c_ctl)] & 0x80) {
                    self->trk_param[3][6] = c_ctl == 'G' ? 0x50 : 0x48;
                    self->trk_param[4][6] = 0x4b;
                    self->trk_param[5][6] = 0x28;
                    self->trk_param[6][6] = 0x48;
                    self->trk_param[7][6] = 0x32;
                    stop_fix_tail(self);
                }
            }
        }
    }

    if (mode == 1 || mode == 3) {
        int cls = self->s3_87a8;

        if ((uint32_t)(cls - 1) <= 4u) {
            trip_for(cls, &t0, &t1, &t2);
            trip_load(self->s3_next_trip, t0, t1, t2, id);
            switch (cls) {
            case 1:
                if (g_10058618[ph_cls100((uint8_t)c_ctl)] & 0x80) {
                    self->trk_param[3][6] = 0x2b;
                    self->trk_param[5][6] = 0x2e;
                    self->trk_param[7][6] = 0x24;
                } else if (c_ctl == 'B') {
                    self->trk_param[8][6] = 0x32;
                } else if (c_ctl == 'D') {
                    self->s3_next_trip[1] = 0x5f4;
                    /* written twice; the 0x30 never survives */
                    self->trk_param[0][6] = 0x30;
                    self->trk_param[6][6] = 0x41;
                    self->trk_param[7][6] = 0x46;
                    self->trk_param[0][6] = 0x28;
                } else if (c_ctl == 'C') {
                    self->trk_param[14][6] = 0x96;
                    self->trk_param[15][6] = 0xfa;
                } else if (c_ctl == 'P') {
                    self->trk_param[5][6] = 0x32;
                }
                break;
            case 2:
                if (c_ctl == 'D') {
                    self->s3_next_trip[1] = 0x6a4;
                    self->trk_param[6][6] = 0x41;
                    self->trk_param[7][6] = 0x46;
                }
                break;
            case 3:
                if (c_ctl == 'D') {
                    self->s3_next_trip[1] = 0x7bc;
                    self->s3_next_trip[2] = 0xb10;
                    self->trk_param[6][6] = 0x41;
                    self->trk_param[7][6] = 0x46;
                    self->trk_param[0][6] = 0x2a;
                }
                break;
            case 4:
                if (c_ctl == 'T') {
                    stop_fix_t(self);
                } else if (c_ctl == 'D') {
                    self->s3_next_trip[1] = 0x5ec;
                    self->trk_param[4][6] = 0x30;
                    self->trk_param[5][6] = 0x3c;
                    self->trk_param[6][6] = 0x41;
                    self->trk_param[7][6] = 0x46;
                } else if (c_ctl == 'C') {
                    self->trk_param[3][6] = 0x1e;
                    self->trk_param[4][6] = 0x48;
                    self->trk_param[14][6] = 0x64;
                    self->trk_param[15][6] = 0xf0;
                } else if (g_10058618[ph_cls100((uint8_t)c_ctl)] & 0x80) {
                    /* the original compares against G here and never uses
                     * the result */
                    self->trk_param[3][6] = 0x2d;
                    self->trk_param[4][6] = 0x28;
                    self->trk_param[6][6] = 0x28;
                    self->trk_param[5][6] = 0x1e;
                    self->trk_param[7][6] = 0x2d;
                    stop_fix_tail(self);
                }
                break;
            default:
                if (c_ctl == 'T') {
                    stop_fix_t(self);
                } else if (c_ctl == 'D') {
                    self->trk_param[3][6] = 0;
                    self->trk_param[4][6] = 0x30;
                    self->trk_param[5][6] = 0x3c;
                    self->trk_param[6][6] = 0x41;
                    self->trk_param[7][6] = 0x46;
                    self->trk_param[0][6] = 0x28;
                    self->s3_next_trip[0] = 0x134;
                    self->s3_next_trip[1] = 0x514;
                    self->s3_next_trip[2] = 0x8a0;
                    self->trk_param[12][6] = 0xc80;
                    self->trk_param[13][6] = 0x50;
                    self->trk_param[15][6] = 0x64;
                } else if (c_ctl == 'C') {
                    self->trk_param[4][6] = 0x3b;
                    self->trk_param[5][6] = 0x38;
                    self->trk_param[14][6] = 0x64;
                    self->trk_param[15][6] = 0xf0;
                } else if (g_10058618[ph_cls100((uint8_t)c_ctl)] & 0x80) {
                    if (c_ctl == 'G') {
                        self->trk_param[3][6] = 0x2d;
                        self->trk_param[5][6] = 0x1e;
                        self->trk_param[7][6] = 0x23;
                        self->trk_param[4][6] = 0x28;
                        self->trk_param[6][6] = 0x28;
                    } else {
                        self->trk_param[4][6] = 0x4b;
                        self->trk_param[5][6] = 0x28;
                        self->trk_param[7][6] = 0x32;
                        self->trk_param[3][6] = 0x48;
                        self->trk_param[6][6] = 0x48;
                    }
                    stop_fix_tail(self);
                }
                break;
            }
        }
        self->s3_8788 = 1;
    }

    Track_SetTriple(self, self->s3_cur_trip[0], self->s3_cur_trip[1],
                    self->s3_cur_trip[2], self->s3_next_trip[0],
                    self->s3_next_trip[1], self->s3_next_trip[2],
                    self->s3_8788);
}
