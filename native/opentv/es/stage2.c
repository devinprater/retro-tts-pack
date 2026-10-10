/*
 * Stage 2 helpers.
 *
 * Stage 2 is where prosody is decided -- stress, phrase shape, the pitch
 * contour -- and most of its rules are questions about what lies on either
 * side of the phoneme in hand.  Stage2_Scan is how it asks them: walk a
 * number of words forwards or boundaries backwards, and say whether the
 * first mask matched before the second one stopped the search.
 *
 * The two masks are signed, and the sign is not part of the value: a
 * negative mask means the same test inverted.  Modes 1 and 2 replace the
 * phoneme-attribute test with a look at the node's own stress bits, which
 * is why the same function answers both "is there a vowel before the next
 * boundary" and "is the next word stressed".
 */
#include "es_engine.h"

/*
 * OpenTV: the pitch ceiling.  This engine clamps a node to 0x32..0xc8 and stores
 * half of it in a byte; the 1997 English engine, whose Stage2_Contour is this
 * function's counterpart, clamps to 0x32..0x1f4 and stores half of that.  Both
 * DLLs carry the same voice table, and two of its ten voices -- Carlos at 203
 * and Josefa at 208 -- are above 0xc8, so their whole contour sits on the
 * ceiling and they come out monotone.  The byte always had the room: 0x1f4 is
 * exactly what half of it can hold, which is why English uses that number.
 *
 * With the extension off this is 0xc8 and the engine is the 1995 one, bug and
 * all, which is what the corpus checks.
 */
int tv_es_ext_rate = 0;

/*
 * OpenTV: the speaking rate above the table.
 *
 * Engine_SetSpeed turns words per minute into a row with (wpm - 46) >> 3 and
 * hands that to three tables -- and they are not the same length.  The pause
 * percentage has 26 rows, the phoneme percentage 24, and the extra-pause
 * pattern 16, while the index reaches 44 at 400 wpm.  So the original reads off
 * the end of all three, and how badly depends on what happens to follow them:
 * at 254 wpm this engine comes out *slower* than at 253, and by 300 it reads a
 * phoneme percentage that leaves nothing to say at all -- a whole sentence in a
 * tenth of a second.  That is the original's behaviour, reproduced exactly with
 * the extension off, and the corpus checks it at 260 and 400.
 *
 * The 1997 engine has the same fault and OpenTV already fixes it there; this is
 * the same fix, which had simply never been ported.  Each table is held inside
 * its own length, and past the last phoneme row the durations are scaled down
 * instead, because the table has no faster rows to offer.  The ramp runs to 40%
 * by row 44, the figure English uses, and the existing floor of 2 still stops a
 * phoneme vanishing.
 */
#define ES_RATE_PAUSE_ROWS    26
#define ES_RATE_PHONE_ROWS    24
#define ES_PAUSE_PATTERN_ROWS 16
#define ES_RATE_ROW_MAX       44        /* (400 - 46) >> 3, as English */

/* Which row to actually read, given how long that table is. */
static int32_t es_rate_row(int32_t i, int32_t rows)
{
    if (!tv_es_ext_rate)
        return i;
    if (i < 0)
        return 0;
    return i >= rows ? rows - 1 : i;
}

/* How much to shorten a phoneme by, past the last row the table has.  100 for
 * every row the original had, so nothing it could say changes. */
static int32_t es_rate_scale(int32_t i, int32_t v)
{
    int32_t pct;

    if (!tv_es_ext_rate || i < ES_RATE_PHONE_ROWS)
        return v;
    if (i > ES_RATE_ROW_MAX)
        i = ES_RATE_ROW_MAX;
    pct = 100 - (i - (ES_RATE_PHONE_ROWS - 1)) * 60 /
                (ES_RATE_ROW_MAX - (ES_RATE_PHONE_ROWS - 1));
    return v * pct / 100;
}

int tv_es_ext_pitch = 0;

static int32_t es_pitch_max(void)
{
    return tv_es_ext_pitch ? 0x1f4 : 0xc8;
}

/*
 * OpenTV: the contour's excursion, against the pitch it sits on.
 *
 * Stage2_Contour builds its contour by adding fixed amounts to the base pitch
 * -- a declining term that starts at 0x32, and a dozen nudges of 5, 7, 0xf and
 * 0x1e -- so the excursion is a number of hertz and not a ratio.  At a voice's
 * own pitch that is what it was tuned to be.  Move the pitch up and those same
 * few hertz become a smaller and smaller musical interval: the voice keeps its
 * timbre and its speed and loses its intonation.  Measured over one sentence,
 * the 10th-to-90th percentile of F0 falls from 5.8 semitones at pitch 160 to
 * 2.7 at 400, which is the flattening this fixes.
 *
 * The 1997 English engine does not have the problem, because it does not build
 * its contour this way: its own Stage2_Contour takes the excursion as
 * Synth_MulQ15(st->pitch / 3, g_voice_pitch_scale[voice]) -- a third of the
 * pitch -- so the interval it spans is the same wherever the voice is put, and
 * only the 0x1f4 clamp ever takes the top off it.  That is the idea borrowed
 * here, applied to the excursion this engine has already worked out rather
 * than replacing the rules that produce it.
 *
 * The reference is one constant for every voice, and it has to be: neither
 * engine's contour depends on which voice is speaking.  Nothing in this
 * function reads st->voice, and English's g_voice_pitch_scale is 32766 for all
 * ten, so at a given pitch every voice gets the same contour and they sound
 * uniformly inflected.  Scaling against each voice's own pitch instead -- which
 * this did at first -- makes a high-pitched voice quieter in its inflection
 * than a low one at the same setting, and moves its centre as well, because the
 * declining term is a large offset and not a symmetric excursion.  Josefa and
 * Carlos, the two highest, came out flattest and a fifth flat.
 *
 * TV_CONTOUR_REF is 85 because that is where the two engines already agree.
 * Rendered at pitch 85 the 1995 engine's fixed contour and the 1997 engine's
 * proportional one put the median F0 within a hertz of each other, 126.1
 * against 126.8; above and below they diverge, because only one of them scales.
 * Scaling from that crossover keeps them together across the range -- at 300
 * this engine reaches a median of 425.7 where English reaches 447.4, against
 * 333.3 unscaled.  So the number is measured rather than chosen, and it is also
 * why a voice at pitch 85 is byte-identical with the extension on or off.
 *
 * It also explains an oddity in the data: g_voice_pitch_scale sits in CGRM_ES
 * with the English values and is read by nothing, because the 1995 engine has
 * no proportional excursion for it to scale.  See docs/VOICES.md.
 */
int tv_es_ext_contour = 0;


/* The pitch at which this engine's fixed contour amounts already match the
 * 1997 engine's proportional ones; see the note above for the measurement. */
#define TV_CONTOUR_REF 85

static int32_t es_contour_scale(int32_t v, int32_t pitch, int32_t voice)
{
    int32_t pct;

    if (tv_es_ext_contour && pitch > 0 && pitch != TV_CONTOUR_REF)
        v = pitch + (v - pitch) * pitch / TV_CONTOUR_REF;

    /*
     * OpenTV: and how much of its contour this voice asks for.  IntonLevel is a
     * percentage of the *excursion*, so it scales the distance from the pitch
     * and leaves the pitch itself alone -- which is why this needs none of the
     * half-a-difference lift the English engine's does: there the narrowing is
     * about a different reference and moves the centre, here it cannot.  A stock
     * voice asks for 100 and nothing happens.
     */
    pct = es_v_inton(voice);
    if (pct != 100 && pitch > 0)
        v = pitch + (v - pitch) * pct / 100;
    return v;
}

/* @0x10058618 */
extern const uint8_t g_10058618[0x200];

/*: the three ways this file indexes the flag table; see lang/spa/engine/adjust.c. */
static int32_t s2_cls0(uint8_t v)
{
    return (int32_t)(int16_t)(int8_t)v;
}

static int32_t s2_cls80(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x80);
}

static int32_t s2_cls100(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x100);
}

static int32_t s2_cls180(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x180);
}

/* @0x1001aa60 */
uint8_t TV_THISCALL Stage2_Scan(Engine *self, int32_t dir, int32_t count,
                                int32_t mask1, int32_t mask2, int32_t mode)
{
    Node *n = self->stage_ctx[2].ctl;
    uint8_t result = 0;
    uint32_t st;
    int32_t kind, v;

    if (count <= 0)
        return 0;
    for (;;) {
        if ((uint8_t)dir == 1) {
            n = Node_NextWord(self, n);
            if (n == NULL)
                break;
        } else {
            n = Node_PrevBoundary(self, n);
            if (n == NULL)
                break;
        }

        if ((uint8_t)mode != 1) {
            if (Phone_TestMask(n, mask1, 0)) {
                result = 1;
                break;
            }
        } else {
            kind = 0;
            st = n->flags & 0x18u;
            if (st == 0x18) {
                kind = 1;
            } else {
                v = mask1 < 0 ? -mask1 : mask1;
                if (v == 2 && st == 0x10)
                    kind = 2;
            }
            if ((kind != 0 && mask1 > 0) || (kind == 0 && mask1 < 0)) {
                result = 1;
                break;
            }
        }

        if ((uint8_t)mode != 2) {
            if (Phone_TestMask(n, mask2, -1))
                break;
        } else {
            kind = 0;
            st = n->flags & 0x18u;
            if (st == 0x18) {
                kind = 1;
            } else {
                v = mask2 < 0 ? -mask2 : mask2;
                if (v == 2 && st == 0x10)
                    kind = 2;
            }
            if ((kind != 0 && mask2 < 0) || (kind == 0 && mask2 > 0)) {
                result = 0;
                break;
            }
        }

        count--;
        if (count <= 0)
            break;
    }
    return result;
}

/*
 * Clear the state stage 2 keeps for one utterance.
 *
 * Stage2_Run calls it rather than Stage2_Reset, so this is the per-run half:
 * eighteen fields cleared and four given defaults -- 0x14, 0x1e, 0x32 and 4.
 * The order is the original's, which is neither by address nor by name.
 */
/* @0x1001a9e0 */
void TV_THISCALL Stage2_ClearRun(Engine *self)
{
    self->s2_87d1 = 0;
    self->s2_87fb = 0;
    self->s2_87fa = 0;
    self->s2_87ec = NULL;
    self->s2_87bc = 0;
    self->s2_87f8 = 0;
    self->s2_87f0 = NULL;
    self->s2_87d0 = 0;
    self->s2_3c0 = 0;
    self->s2_8800 = 0;
    self->s2_87c8 = 0x14;
    self->s2_3c9 = 0;
    self->s2_3c8 = 0;
    self->s2_8804 = 0;
    self->s2_87c0 = 0;
    self->s2_87cc = 0x1e;
    self->s2_87e4 = 0x32;
    self->s2_3d0 = 4;
}

/*
 * Mark a node and up to three more behind it with flag 0x20.
 *
 * Each step back is a Node_PrevBoundary, and each one has to pass before the
 * next is taken: the first two need attribute 0x20 of row 1, the third needs
 * that and attribute 2 of row 0 to be clear, and the fourth has to be an 'S'.
 * A '~' stops it after the first.
 */
/* @0x10019a00 */
void TV_THISCALL Stage2_MarkBack(Engine *self, Node *n)
{
    n->flags |= 0x20u;
    n = Node_PrevBoundary(self, n);
    if (!Phone_TestMask(n, 0x120, 0) || n->value == '~')
        return;

    n->flags |= 0x20u;
    n = Node_PrevBoundary(self, n);
    if (!Phone_TestMask(n, 0x120, 0))
        return;
    if (!Phone_TestMask(n, 2, -1))
        return;

    n->flags |= 0x20u;
    n = Node_PrevBoundary(self, n);
    if (n == NULL || n->value != 'S')
        return;
    n->flags |= 0x20u;
}

/*
 * A stress mark, folded into the phoneme it belongs to.
 *
 * '1', '2' and '"' are not phonemes; they are levels, and they are carried as
 * nodes of their own until here.  The node goes away and its level -- 2, 1 or
 * 3 respectively -- is written into flag bits 3 and 4 of the node *before*
 * it, which Engine_NodeFree hands back, so long as that is a phoneme node and
 * a vowel.  A mark with nothing in front of it becomes a space instead of
 * going away.
 *
 * Returns whatever node the caller should carry on from.
 */
/* @0x10019a80 */
Node *TV_THISCALL Stage2_ApplyStress(Engine *self, Node *n)
{
    uint8_t c = n->value;
    int32_t level;

    if (c != '1' && c != '2' && c != '"')
        return n;
    if (Engine_StagePrev(self, n) == NULL) {
        n->value = ' ';
        return n;
    }
    n = Engine_NodeFree(self, n, 0);
    if (NODE_TYPE(n) != 3)
        return n;
    if (!Phone_TestMask(n, 1, 0))
        return n;
    level = c == '"' ? 3 : (c == '1' ? 2 : 1);
    n->flags = (n->flags & ~0x18u) | ((uint32_t)level << 3);
    return n;
}

/*
 * Execute the control nodes at the head of stage 2's window.
 *
 * Each one is handed to Engine_RunControl and then stepped over, and the
 * three window pointers plus s2_87d4 are moved off it as it goes.  It stops
 * at the first phoneme node and at a 'C', which stage 2 handles itself.
 *
 * One command is read here rather than left to Engine_RunControl: while the
 * scan and the cursor are still together, an 'r' or a 'v' control node puts
 * the low byte of its argument into s2_87fc.
 */
/* @0x1001a7e0 */
void TV_THISCALL Stage2_RunControls(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    Node *n = st->ctl;

    while (n != NULL) {
        uint32_t type = n->flags & 7;

        if (type == 3)
            return;
        if (type == 0 && n->value == 'C')
            return;
        if (st->scan == st->cur && type == 0 &&
            (n->value == 'r' || n->value == 'v'))
            self->s2_87fc = (int32_t)(n->arg & 0xff);

        Engine_RunControl(self);
        st->ctl = Engine_StageNext(self, n);
        if (self->s2_87d4 == n) {
            self->s2_87d8 = 0;
            self->s2_87d4 = st->ctl;
        }
        if (st->scan == n)
            st->scan = st->ctl;
        if (st->cur == n)
            st->cur = st->ctl;
        n = st->ctl;
    }
}

/*
 * Everything stage 2 is about to want to know about where it is.
 *
 * Three nodes -- the boundary behind the control node, the next word and the
 * one after that -- and their values, then eleven single bits of the control
 * node's class flags unpacked into a byte each.  Written once per step so the
 * arms that follow can test a byte instead of indexing the table again.
 *
 * The bits are kept as the table's own values rather than as 0 and 1, so
 * s2_3d6 holds 0x40 or 0, and only s2_3db -- which comes from the node's
 * flags rather than the table -- is shifted down to 0 or 1.
 */
/* @0x10019bd0 */
void TV_THISCALL Stage2_Cache(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    Node *ctl = st->ctl;
    Node *back, *fwd, *fwd2;
    const uint8_t *row0, *row100;
    uint8_t c;

    back = Node_PrevBoundary(self, ctl);
    self->s2_881c = back;
    fwd = Node_NextWord(self, ctl);
    self->s2_8820 = fwd;

    self->s2_3e1 = ctl->value;
    self->s2_882c = back != NULL ? back->value : 0;
    self->s2_882d = fwd != NULL ? fwd->value : 0;

    fwd2 = Node_NextWord(self, fwd);
    self->s2_8824 = fwd2;
    self->s2_8830 = fwd2 != NULL ? (int32_t)(int8_t)fwd2->value : 0;

    c = self->s2_3e1;
    row0 = &g_10058618[s2_cls0(c)];
    row100 = &g_10058618[s2_cls100(c)];
    self->s2_3d5 = (uint8_t)(g_10058618[s2_cls80(c)] & 8);
    self->s2_3d6 = (uint8_t)(*row0 & 0x40);
    self->s2_3d7 = (uint8_t)(*row0 & 0x10);
    self->s2_3d8 = (uint8_t)(*row0 & 0x80);
    self->s2_3d9 = (uint8_t)(*row0 & 2);
    self->s2_3da = (uint8_t)(*row100 & 1);
    self->s2_3dc = (uint8_t)(*row0 & 1);
    self->s2_3dd = (uint8_t)(*row100 & 2);
    self->s2_3db = (uint8_t)((ctl->flags & 0x20) >> 5);
    self->s2_3de = (uint8_t)(g_10058618[s2_cls0(self->s2_882c)] & 1);
    self->s2_3e0 = (uint8_t)(g_10058618[s2_cls0(self->s2_882d)] & 1);
}

/*
 * Count the vowels either side of a node -- the syllables, in other words.
 *
 * Backward is the simple half: step back to s2_87f0, counting every node whose
 * 0x100-block flag has 2.  Forward is the same count with two extra rules.  It
 * stops at a '.', ',', '?' or '&'; and when two vowels come together it steps
 * over one of them, so a diphthong counts once.  Which of the two it steps
 * over is settled by the
 * 0x80 block's flag 2 and by flag 0x20 on the node: the first vowel is the one
 * dropped if it is marked in the table and the node is not flagged, otherwise
 * the second is -- and if neither is a candidate, both stay.
 */
/* @0x10019490 */
int32_t TV_THISCALL Stage2_CountVowels(Engine *self, Node *from,
                                       uint8_t forward)
{
    int32_t count = 0;
    Node *n;

    if (!forward) {
        if (self->s2_87f0 == from)
            return 0;
        for (;;) {
            Node *p = Engine_StagePrev(self, from);

            if (p == NULL)
                break;
            if (g_10058618[s2_cls100(p->value)] & 2)
                count++;
            from = p;
            if (self->s2_87f0 == p)
                break;
        }
        return count;
    }

    n = Engine_StageNext(self, from);
    if (n == NULL)
        return 0;
    while (n->value != '&') {
        uint8_t c = n->value;

        if (c == '.' || c == ',' || c == '?')
            break;
        if (g_10058618[s2_cls100(c)] & 2) {
            Node *nx;

            count++;
            nx = Engine_StageNext(self, n);
            if (nx != NULL && (g_10058618[s2_cls100(nx->value)] & 2)) {
                int drop_first = (g_10058618[s2_cls80(c)] & 2) &&
                                 !(n->flags & 0x20);

                if (drop_first ||
                    ((g_10058618[s2_cls80(nx->value)] & 2) &&
                     !(nx->flags & 0x20)))
                    n = nx;
            }
        }
        n = Engine_StageNext(self, n);
        if (n == NULL)
            break;
    }
    return count;
}

/*
 * What kind of word boundary this is, as three cached values.
 *
 * s2_3c8 and s2_3c9 come from looking ahead: nine nodes for row 1's attribute
 * 0x40, and failing that ten nodes for row 0's attribute 8.  s2_3d0 is a
 * four-way class of the next word, read off its first phoneme's attributes --
 * 3, 1, 2 or, when none of them matches, the 4 it starts at.  A 'q' in
 * s2_8828 raises flag 0x40 on the control node before any of that.
 *
 * The next word is looked one further ahead when its first phoneme has
 * attribute 2 but the word after it does not and has 0x80; then the original
 * word is used after all, which makes the whole test a way of spending two
 * lookups to arrive back where it started.
 */
/* @0x100195c0 */
void TV_THISCALL Stage2_Classify(Engine *self)
{
    Node *n = self->s2_8828;

    if (n != NULL && n->value == 'q')
        self->stage_ctx[2].ctl->flags |= 0x40u;

    if (self->s2_3dc == 0)
        return;

    self->s2_3c8 = 0;
    self->s2_3c9 = 0;
    if (Stage2_Scan(self, 1, 9, 0x140, -1, 0)) {
        self->s2_3c8 = 1;
        self->s2_3c9 = 1;
    } else if (Stage2_Scan(self, 1, 0xa, 8, -1, 0)) {
        self->s2_3c8 = 1;
    }

    n = self->s2_8820;
    self->s2_3d0 = 4;
    if (Phone_TestMask(n, 2, 0)) {
        n = Node_NextWord(self, n);
        if (Phone_TestMask(n, 2, -1) && Phone_TestMask(n, 0x80, 0))
            n = self->s2_8820;
    }

    if (Phone_TestMask(n, 4, 0)) {
        if (Phone_TestMask(n, 0x40, 0) || Phone_TestMask(n, 0x108, 0))
            self->s2_3d0 = 3;
        return;
    }
    if (Phone_TestMask(n, 0x40, 0)) {
        self->s2_3d0 = 1;
        return;
    }
    if (Phone_TestMask(n, 0x20, 0))
        self->s2_3d0 = 2;
}

/*
 * The node's length and level, latched on one call and applied on the next.
 *
 * With `apply` clear this reads the control node's arg and b15 into s2_380 and
 * s2_8810, sign-extending the low byte of each, and works out s2_378 -- bit 0
 * for a non-zero length, bit 1 for a non-zero level, bit 2 from flag 0x80 on
 * the node -- keeping the previous s2_378 in s2_37c.  A space longer than 0x32
 * is cut to 0x32 and that is all it does.
 *
 * With `apply` set it writes them back, but only to the same node with the
 * same value it latched.  Bit 2 decides between adding to what is there and
 * replacing it: added, the length is clamped to 2..0x3c and the level to
 * 0x19..0x64; replaced, a level of exactly 1 becomes 0.
 */
/* @0x1001ab90 */
void TV_THISCALL Stage2_Adjust(Engine *self, int32_t apply)
{
    Node *n = self->stage_ctx[2].ctl;
    int32_t v, flags;

    if (apply == 0) {
        int32_t level = (int32_t)n->b15;
        int32_t len = (int32_t)n->arg;
        uint8_t c = n->value;

        self->s2_8814 = n;
        self->s2_8810 = level;
        self->s2_380 = len;
        self->s2_880c = c;
        if (level & 0x80)
            self->s2_8810 = level | (int32_t)0xffffff00;
        if (len & 0x80)
            self->s2_380 = len | (int32_t)0xffffff00;

        self->s2_37c = self->s2_378;
        self->s2_378 = 0;
        if (n->flags & 0x80u)
            self->s2_378 = 4;
        if (self->s2_380 != 0)
            self->s2_378 |= 1;
        if (self->s2_8810 != 0)
            self->s2_378 |= 2;
        if (c == ' ' && self->s2_380 > 0x32)
            self->s2_380 = 0x32;
        return;
    }

    if (n->b15 == 0 && n->value == ' ')
        n->b15 = 0x32;
    if (self->s2_8814 != n || self->s2_880c != n->value)
        return;

    flags = self->s2_378;
    if (flags & 1) {
        if (flags & 4) {
            v = (int32_t)n->arg + self->s2_380;
            self->s2_380 = v;
            if (v < 2)
                self->s2_380 = 2;
            else if (v > 0x3c)
                self->s2_380 = 0x3c;
        }
        n->arg = (uint32_t)self->s2_380;
    }

    flags = self->s2_378;
    if (!(flags & 2))
        return;
    if (flags & 4) {
        v = (int32_t)n->b15 + self->s2_8810;
        self->s2_8810 = v;
        if (v < 0x19)
            self->s2_8810 = 0x19;
        else if (v > 0x64)
            self->s2_8810 = 0x64;
    } else if (self->s2_8810 == 1) {
        self->s2_8810 = 0;
    }
    n->b15 = (uint8_t)self->s2_8810;
}

/*
 * Two words that meet on the same phoneme: drop one of them.
 *
 * The control node is the last phoneme of a word and s2_882d is the first of
 * the next.  When they are the same character the next word's is removed, and
 * when they are not but s2_8830 -- the first phoneme of the word after that --
 * is the same, that one is removed instead, so "una a alta" loses a phoneme
 * across two boundaries rather than one.
 *
 * Which of the several removals runs depends on the phoneme: N sets the
 * control node's length to 1 and takes the node out, R refuses outright, E
 * refuses when what precedes it is marked in the 0x80 block, and everything
 * else turns on s2_3dd -- the 0x100 block's attribute 2 -- with the marked
 * case carrying flag 0x20 back onto the control node first.
 *
 * Returns whether anything was removed.
 */
/* @0x10019d10 */
uint8_t TV_THISCALL Stage2_Elide(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    Node *ctl;
    uint8_t c, nx;

    if (self->s2_8820 == NULL)
        return 0;
    ctl = st->ctl;
    c = ctl->value;
    if (!(g_10058618[s2_cls0(c)] & 0x80) || c == ' ')
        return 0;

    nx = self->s2_882d;
    c = self->s2_3e1;
    if (nx != c) {
        if (!(g_10058618[s2_cls0(nx)] & 8))
            return 0;
        if ((int32_t)(int8_t)c != self->s2_8830)
            return 0;

        /* the phoneme two words along */
        if (c == 'N' || c == 'L') {
            st->ctl->arg = 1;
            Engine_NodeFree(self, self->s2_8824, 1);
            self->s2_87dc--;
            return 1;
        }
        if (self->s2_3dd != 0)
            return 0;
        Engine_NodeFree(self, self->s2_8824, 1);
        self->s2_87dc--;
        return 1;
    }

    /* the next word's first phoneme is the same character */
    if (c == 'N') {
        st->ctl->arg = 1;
        self->s2_8820 = Engine_NodeFree(self, self->s2_8820, 1);
        self->s2_87dc--;
        return 1;
    }
    if (c == 'R')
        return 0;
    if (c == 'E' &&
        (g_10058618[s2_cls80(st->ctl->prev->value)] & 0x20))
        return 0;

    if (self->s2_3dd == 0) {
        Engine_NodeFree(self, self->s2_8820, 1);
        self->s2_87dc--;
        /* and then walks on from the node it has just freed, which follows
         * the free list rather than the work list.  Written as the original
         * has it; Node_NextWord is the same function either way, so the two
         * builds agree. */
        self->s2_8820 = Node_NextWord(self, self->s2_8820);
        return 1;
    }

    if (self->s2_8820->flags & 0x20u)
        st->ctl->flags |= 0x20u;
    st->ctl->arg = 1;
    self->s2_8820 = Engine_NodeFree(self, self->s2_8820, 1);
    self->s2_87dc--;
    return 1;
}

/*
 * How high this phoneme is: the pitch contour, in the control node's b15.
 *
 * English's Stage2_Contour is the same function, and b15 is what stage 3 turns
 * into track 17 as b15 * 2, which is the pitch the synthesiser runs at.
 *
 * It starts from a running value in s2_87e4 that decays to 85% of itself at
 * every word boundary, adds the stage's pitch, and then nudges it up and down
 * on a dozen tests -- what is within three nodes, within eight, within twenty,
 * whether the node is stressed, whether a full stop is coming.  The result is
 * clamped and halved, so b15 lands in 0x19..0x64, the same range Stage2_Adjust
 * clamps it to -- or in 0x19..0xfa with the pitch ceiling lifted, which is what
 * es_pitch_max above is for.  A zero pitch zeroes it instead, and bit 0x800 of
 * the stage's p_34 replaces it with the pitch outright.
 *
 * cur_bac of 1 forces the level to zero and 2 replaces it with the pitch, both
 * before the halving.
 *
 * s2_87c8 is a step size, which the two scans at the top halve and which is
 * reset to 6 once it has been spent.  s2_8800 counts how many stresses are
 * near, and s2_3c0 is whether one is near at all.
 */
/* @0x10019710 */
void TV_THISCALL Stage2_Contour(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    Node *ctl = st->ctl;
    int32_t v, pitch, flags;
    uint8_t c, bit4;

    if (self->s2_3de != 0)
        /* the original multiplies with lea and then divides unsigned */
        self->s2_87e4 =
            (int32_t)((uint32_t)self->s2_87e4 * 85u / 100u);

    if (self->s2_3d8 == 0)
        return;

    c = self->s2_3e1;
    v = self->s2_87e4 + st->pitch;
    if (g_10058618[s2_cls80(c)] & 1) {
        self->s2_87c8 = 0xa;
        self->s2_8800 = 0;
    }

    if (self->s2_87d0 != 0) {
        if (Stage2_Scan(self, 1, 0x5a, 1, -0x101, 1)) {
            self->s2_87d0 = 0;
            self->s2_87c8 >>= 1;
        }
        if (Stage2_Scan(self, 0, 0x5a, 1, -0x101, 1)) {
            self->s2_87d0 = 0;
            self->s2_87c8 >>= 1;
        }
    }

    if (Stage2_Scan(self, 1, 3, 2, -0x80, 1)) {
        self->s2_3c0 = 1;
        self->s2_8800++;
        if ((ctl->flags & 0x18) == 0x18)
            self->s2_8800++;
    }

    flags = (int32_t)(ctl->flags & 0x18);
    if (flags == 0x10 || flags == 0x18) {
        if (Stage2_Scan(self, 1, 0x14, 0x140, -2, 2) &&
            (self->s2_8800 >= 2 ||
             Stage2_Scan(self, 1, 0x14, 0x380, -2, 2)))
            self->s2_3c0 = 0;

        if (self->s2_3c0 != 0) {
            v += self->s2_87c8;
        } else {
            Node *n = self->s2_8828;

            if (n != NULL && n->value != ' ' &&
                ((self->s2_37c & 4) || !(self->s2_37c & 2)) &&
                !(st->p_34 & 0x800) && st->pitch != 0) {
                uint8_t step = (uint8_t)(self->s2_87c8 >> 1);

                if (self->s2_8800 > 1)
                    n->b15 = (uint8_t)(n->b15 + step);
                else
                    n->b15 = (uint8_t)(n->b15 - step);
            }
        }
        self->s2_87c8 = 6;
    }

    if (self->s2_3c0 != 0 && self->s2_3d8 != 0)
        v += self->s2_87cc;
    if (Stage2_Scan(self, 1, 3, 1, -0x80, 1))
        v += 0xf;

    bit4 = (uint8_t)(g_10058618[s2_cls0(c)] & 4);
    if (bit4) {
        v -= 5;
        if (self->s2_3d9 == 0)
            v -= 5;
    }
    if ((g_10058618[s2_cls80(c)] & 2) && (st->ctl->flags & 0x20))
        v += 7;

    if (bit4 && Stage2_Scan(self, 1, 8, 0x101, -1, 0)) {
        Node *n = ctl;

        v -= 0xf;
        while (!Phone_TestMask(n, 0x101, 0))
            n = Engine_StageNext(self, n);
        if (n->value != '.') {
            v += 0x1e;
            if (self->s2_3dc == 0)
                v += 5;
        }
    }

    pitch = st->pitch;
    v = es_contour_scale(v, pitch, st->voice);
    if (pitch == 0)
        v = 0;
    else if (v > es_pitch_max())
        v = es_pitch_max();
    else if (v < 0x32)
        v = 0x32;
    if (st->p_34 & 0x800)
        v = pitch;

    if (self->cur_bac == 1) {
        ctl->b15 = 0;
        return;
    }
    if (self->cur_bac == 2)
        v = pitch;
    ctl->b15 = (uint8_t)(v >> 1);
}

/*
 * Two of the same phoneme across a boundary, merged the other way round.
 *
 * Where Stage2_Elide removes the next word's copy, this removes the one
 * *behind* -- s2_881c, the boundary node -- and gives the control node its
 * length (the two added, capped at 0x37) and its level.  It only runs for a
 * phoneme marked 0x20 in the 0x80 block whose control node is not already
 * flagged 0x40.  Either way the cache is rebuilt on the way out, because the
 * node behind may be gone.
 */
/* @0x10018a10 */
void TV_THISCALL Stage2_MergeBack(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    Node *back = self->s2_881c;
    uint8_t c = self->s2_3e1;

    if (self->s2_882c == c && c != ' ' && !(st->ctl->flags & 0x40u) &&
        (g_10058618[s2_cls80(c)] & 0x20)) {
        Node *ctl = st->ctl;
        int32_t len = (int32_t)back->arg + (int32_t)ctl->arg;

        if (len >= 0x37)
            len = 0x37;
        ctl->arg = (uint32_t)len;
        if (self->s2_3db != 0)
            st->ctl->flags |= 0x20u;
        st->ctl->b15 = back->b15;
        Engine_NodeFree(self, back, 0);
        self->s2_87b4--;
    }
    Stage2_Cache(self);
}

/* Sixteen bitmasks of sixteen bits, one per speaking rate: which of sixteen
 * consecutive chances gets an extra pause.  The counter that steps through
 * them is a global, so it carries across utterances and across engines. */
/* @0x10048a58 */
extern const uint32_t g_pause_pattern[16];
/* @0x10045894 */
/* One int32, and the pause pattern walks it round a sixteen-step cycle. */
extern int32_t g_pause_step;
/* @0x10049910 */
extern const uint32_t g_bit_mask[8];

/*
 * Walk forward to the end of the word, doing everything that has to happen on
 * the way.
 *
 * The walk runs from stage 2's scan pointer and stops when s2_87d2 goes up,
 * which the phrase enders do: a '.', '?' or ',' phoneme, or a 'C' control
 * node.  It returns the node the caller should take as the word's last, and
 * the several things it does as it passes are:
 *
 *   - a space phoneme with no length gets 7;
 *   - an 'x' control node gets a 'C' put after it;
 *   - 'Q' is removed, or becomes a space when it is first in the window;
 *   - 'q' and 'Q' arm s2_87e8, which raises flag 0x40 on the next phoneme;
 *   - '&' and '%' are counted, in s2_87c0 and s2_8808;
 *   - a '%' after a '&' can get a ',' put in front of it, on the node levels,
 *     and, at nine or under in s2_87fc, a ')' as well -- that one only for one
 *     of sixteen steps of a global counter, masked by the speaking rate.
 *
 * With p_20 not 1 none of the middle happens: the scan pointer itself becomes
 * the result and only the enders are looked for.
 *
 * The empty window is the other half.  When the scan pointer is null and
 * fifteen or more of the window's nodes are not phonemes, a 'C' is appended
 * and the walk starts on that instead.
 */
/* @0x1001a380 */
Node *TV_THISCALL Stage2_ScanWord(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    Node *result = NULL;

    self->s2_87d2 = 0;
    for (;;) {
        Node *n = st->scan;
        Node *nx;
        uint32_t type;
        uint8_t c;
        int brk, amp;

        if (n == NULL) {
            Node *w;

            if (self->s2_87bc < 2)
                break;
            self->s2_87bc = 0;
            for (w = st->first; w != NULL; w = Engine_StageNext(self, w))
                if ((w->flags & 7) != 3)
                    self->s2_87bc++;
            if (self->s2_87bc < 0xf)
                break;
            n = Engine_NodeAlloc(self, st->last, 1, 0, 'C');
            st->scan = n;
            self->s2_87bc = 0;
        }

        if (self->s2_87d4 == NULL)
            self->s2_87d4 = n;

        c = n->value;
        if (c == ' ' && (n->flags & 7) == 3 && n->arg == 0)
            n->arg = 7;
        if ((n->flags & 7) == 0 && c == 'x')
            Engine_NodeAlloc(self, n, 1, 0, 'C');

        type = n->flags & 7;
        if (type == 3 && (c == '.' || c == '?' || c == ',')) {
            brk = 1;
            self->s2_87c0 = 0;
        } else if (type == 0) {
            brk = c == 'C';
        } else {
            brk = 0;
        }

        type = n->flags & 7;
        if (type == 3 && (c == '1' || c == '2' || c == '"')) {
            /* a stress mark: stepped over here, folded in by Stage2_Word */
            st->scan = Engine_StageNext(self, n);
            if (self->s2_87d2 != 0)
                break;
            continue;
        }
        if (type == 3) {
            if (c == 'Q' || c == 'q') {
                self->s2_87e8 = 1;
                if (c == 'Q') {
                    if (Engine_StagePrev(self, n) == NULL) {
                        n->value = ' ';
                    } else {
                        st->scan = Engine_NodeFree(self, n, 1);
                        if (self->s2_87d2 != 0)
                            break;
                        continue;
                    }
                }
            } else if (self->s2_87e8 != 0) {
                n->flags |= 0x40u;
                self->s2_87e8 = 0;
            }
        }

        if (st->p_20 != 1) {
            result = st->scan;
            if (brk)
                self->s2_87d2 = 1;
        } else {
            amp = (n->flags & 7) == 3 && (c == '&' || c == '%');
            if (!amp && !brk) {
                if (st->d14 != NULL)
                    result = self->s2_87ac;
                else if (self->s2_87fa != '%')
                    result = n;
            } else {
                Node *prev = self->s2_87ac;
                uint8_t prevc = self->s2_87fa;

                if (c == '&')
                    self->s2_87c0++;
                if (amp)
                    self->s2_8808++;
                if (brk)
                    self->s2_87d2 = 1;

                self->s2_87fb = prevc;
                self->s2_87b0 = prev;
                self->s2_87fa = c;
                self->s2_87ac = n;

                if (c == '%' && prevc == '&' &&
                    self->s2_87f4 < self->s2_87c0) {
                    int put = 0;

                    if (prev->b15 != 6 && (n->b15 == 3 || n->b15 == 4))
                        put = 1;
                    else if (n->b15 == 5)
                        put = 1;
                    if (put) {
                        Engine_NodeAlloc(self, n, 0, 3, ',');
                        self->s2_87d2 = 1;
                        self->s2_87c0 = 0;
                        self->s2_87fb = 0;
                    }
                }

                if (self->s2_87fc < 9 && c == '%' &&
                    self->s2_87fb == '&') {
                    int32_t step = g_pause_step & 0xf;

                    g_pause_step++;
                    if (g_bit_mask[step] &
                        g_pause_pattern[es_rate_row(st->rate_index,
                                                    ES_PAUSE_PATTERN_ROWS)])
                        Engine_NodeAlloc(self, n, 0, 3, ')');
                }

                if (st->d14 == NULL)
                    result = n;
            }
        }

        if ((n->flags & 7) == 3)
            self->s2_87bc = 0;
        else
            self->s2_87bc++;
        if ((n->flags & 7) == 0 && (n->value == 'r' || n->value == 'v'))
            self->s2_87fc = (int32_t)(n->arg & 0xff);

        nx = Engine_StageNext(self, st->scan);
        st->scan = nx;
        if (self->s2_87d2 != 0)
            break;
    }
    return result;
}

/*
 * Move stage 2's window on by one node, or by one word.
 *
 * Two shapes.  When s2_87d1 says the word just finished ended a phrase,
 * everything the phrase accumulated is cleared and the whole window lands on
 * the next node; a period or a question mark, or a zero p_20, also raises
 * s2_87f9 on the way.
 *
 * Otherwise the control node steps on by one and, when s2_87b4 has caught up
 * with s2_87b8, the cursor is walked forward to the next phrase or word
 * boundary, s2_87b4 counting down as it goes.  s2_1d55 -- the flag
 * Preformat_PutChar raises when the node pool has plenty spare -- decides
 * whether a run that reaches the end of the list takes what it found or gives
 * up and counts one more node instead.
 *
 * Returns whether the cursor moved, which is what Stage2_Run tests.
 */
/* @0x1001a220 */
uint8_t TV_THISCALL Stage2_Advance(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    uint8_t moved = 0;
    Node *nx = Engine_StageNext(self, st->ctl);
    Node *n;
    int32_t left;
    int hit = 0;

    if (self->s2_87d4 == st->ctl && self->s2_87d1 == 1) {
        self->s2_87d1 = 0;
        self->s2_87ec = NULL;
        self->s2_87b4 = 0;
        st->cur = nx;
        st->ctl = nx;
        self->s2_87d4 = nx;
        self->s2_87d8 = 0;
        self->s2_87dc = 0;
        moved = 1;
        if (st->p_20 == 0 || self->s2_3e1 == '.' || self->s2_3e1 == '?')
            self->s2_87f9 = 1;
        goto out;
    }

    left = self->s2_87b4;
    st->ctl = nx;
    self->s2_87dc--;
    if (self->s2_87b8 > left) {
        self->s2_87b4 = left + 1;
        goto out;
    }

    n = st->cur;
    if (n == NULL)
        goto ended;
    for (;;) {
        uint32_t type;

        n = Engine_StageNext(self, n);
        if (n == NULL)
            goto ended;
        if (self->s2_87b8 > left && (self->s2_1d55 == 0 || left < 1))
            goto ended;
        type = n->flags & 7;
        if (type != 4 || n->value == ' ')
            hit = 1;
        if (type != 3 && type != 4)
            continue;
        left--;
        if (hit)
            goto take;
        if (self->s2_1d55 != 0)
            goto ended;
    }

ended:
    /* n is null here when the walk ran off the end, and the original stores
     * it into the cursor all the same */
    if (hit || self->s2_1d55 != 0)
        goto take;
    if (self->s2_87b8 + 0x32 >= self->s2_87b4) {
        self->s2_87b4++;
        goto out;
    }
take:
    st->cur = n;
    self->s2_87b4 = left + 1;
    moved = 1;
out:
    Stage2_RunControls(self);
    return moved;
}

/*
 * One word's phonemes.
 *
 * s2_87ec is the last node of the word, which Stage2_Scan_Word works out when
 * there is not one already, and s2_87d4 is how far along it this has got.
 * Each phoneme goes through Stage2_ApplyStress -- which may remove it and hand
 * back the one before -- and a stressed one is then marked backward.  A 'C'
 * control node, or a '.', ',' or '?', ends the word and raises s2_87d1 so that
 * Stage2_Advance clears the phrase next time.
 *
 * Returns whether the word is done with, which is either that end or
 * s2_87dc having got as far as s2_87e0.
 */
/* @0x1001a6d0 */
uint8_t TV_THISCALL Stage2_Word(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    Node *last = self->s2_87ec;
    Node *at = self->s2_87d4;
    uint8_t done = 0;

    if (at == last || last == NULL || at == NULL) {
        if (at == NULL)
            self->s2_87d8 = 0;
        self->s2_87ec = Stage2_ScanWord(self);
        if (self->s2_87ec == NULL)
            return 0;
    }

    for (;;) {
        uint8_t c;
        uint32_t type;

        if (self->s2_87d8 != 0) {
            at = Engine_StageNext(self, at);
        } else {
            self->s2_87d8 = 1;
            at = self->s2_87d4;
        }

        c = at->value;
        type = at->flags & 7;
        if (type == 0 && c == 'C') {
            done = 1;
            self->s2_87d1 = 1;
            break;
        }
        if (type == 3) {
            Node *n = Stage2_ApplyStress(self, at);

            if (n->flags & 0x18u)
                Stage2_MarkBack(self, n);
            if ((n->flags & 0x18u) == 0x18u)
                self->s2_87d0 = 1;
            if (n != at) {
                at = n;
                if (self->s2_87ec != at)
                    continue;
                break;
            }
            if (st->ctl != at)
                self->s2_87dc++;
            if (c == '.' || c == ',' || c == '?') {
                done = 1;
                self->s2_87d1 = 1;
                break;
            }
        }
        if (self->s2_87e0 <= self->s2_87dc)
            done = 1;
        if (self->s2_87ec != at)
            continue;
        break;
    }

    self->s2_87d4 = at;
    return done;
}

/* How long a pause of one unit is, and how long a phoneme is, both as
 * percentages and both indexed by the speaking rate: 1350% down to 30% for a
 * pause, 300% down to 74% for a phoneme. */
/* @0x10048970 */
extern const int32_t g_rate_pause[ES_RATE_PAUSE_ROWS];
/* @0x100489d8 */
extern const int32_t g_rate_phone[ES_RATE_PHONE_ROWS];
/* Indexed by the syllables in the word, one to four. */
/* @0x10048a38 */
extern const int32_t g_syl_pct[5];
/* 0.8, the factor an e or an i before an N is multiplied by. */
/* @0x10048a50 */
extern const double g_dur_before_n;
/* Ten times this is the phoneme's base length; the same table lang/spa/engine/stage3seg.c
 * reads as a divisor. */
/* @0x10057d50 */
extern const tv_ref g_10057d50;

/*: v * num / 100, the signed divide the original uses throughout. */
static int32_t dur_pct(int32_t v, int32_t num)
{
    return v * num / 100;
}

/*
 * How long this phoneme lasts, in samples, into the control node's arg.
 *
 * A space is a pause: the rate table turns its length into samples, s2_380
 * overrides it outright, and anything over 0xff is broken into extra pause
 * nodes of 0xff each until either the length fits or the node pool runs low.
 *
 * Everything else starts from ten times the per-phoneme base in g_dur_base and
 * is then scaled by a long series of percentages.  A vowel is scaled by the
 * syllable count first and then by two dozen tests on what the word before and
 * the word after start with, each one a percentage between 79 and 180; a
 * consonant skips all of that and takes the six adjustments from 0x100e6
 * onward instead.  `mode` -- 'a' or 'c' -- is a two-way state those tests set
 * and read, and 'a' is only ever reached through a vowel-adjacent case.
 *
 * Then the two halves part company.  For a consonant the scaled value becomes
 * the duration.  For a vowel it is *discarded* and the duration is one of a
 * dozen small constants instead, chosen on the phoneme, on whether the node is
 * stressed, and on what two nodes ahead look like -- so the whole vowel
 * scaling chain above computes a number nothing reads.  That is what the
 * shipped build does; it is written out here because the tests say the two
 * builds have to agree, not because it makes sense.
 *
 * Last, the rate table scales it again, a duration of two is the floor, and an
 * e or an i followed by an N is cut to four fifths.
 */
/* @0x10018aa0 */
void TV_THISCALL Stage2_Duration(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    Node *ctl = st->ctl;
    int32_t arg0 = (int32_t)ctl->arg;
    int32_t v, dur;
    int mode = 'c';
    uint8_t c, nx, clsb, nb;

    if (self->s2_3d8 == 0) {
        ctl->arg = 0;
        goto tail;
    }

    c = self->s2_3e1;
    if (c == ' ') {
        v = (int32_t)((uint32_t)(g_rate_pause[es_rate_row(
                          st->rate_index, ES_RATE_PAUSE_ROWS)] * arg0)
                      / 100u);
        if (v < 1)
            v = 1;
        if (self->s2_380 != 0)
            v = self->s2_380;
        while (v > 0xff && self->free_nodes > 3) {
            Node *pad;

            v -= 0xff;
            pad = Engine_NodeAlloc(self, ctl, 0, 4, ' ');
            pad->arg = 0xff;
            pad->b15 = 0x32;
            self->s2_87b4++;
        }
        if (v >= 0xff)
            v = 0xff;
        ctl->arg = (uint32_t)v;
        ctl->b15 = 0x32;
        goto tail;
    }

    v = (int32_t)TV_REF(uint8_t, g_10057d50)[s2_cls0(c)] * 10;
    ctl->arg = (uint32_t)v;
    /* the result is not used; the call is here because the original makes it */
    Engine_StageNext(self, st->ctl);

    if (!(g_10058618[s2_cls100(c)] & 2))
        goto consonant;

    /* --- the vowel chain ------------------------------------------------- */
    {
        int32_t back = Stage2_CountVowels(self, ctl, 0);
        int32_t fwd = Stage2_CountVowels(self, ctl, 1);
        int32_t syl = back + fwd + 1;

        if (syl >= 4)
            syl = 4;
        if (syl == 1)
            goto vowel_neighbours;
        v = (int32_t)((uint32_t)(g_syl_pct[syl] * (int32_t)ctl->arg) / 100u);
    }

    nx = self->s2_882d;
    if (nx == '&' || nx == '.' || nx == ',' || nx == '?') {
        mode = 'a';
        v = dur_pct(v, (ctl->flags & 0x20u) ? 150 : 91);
    } else if (self->s2_8830 == '&' || self->s2_8830 == '.' ||
               self->s2_8830 == ',' || self->s2_8830 == '?') {
        v = dur_pct(v, (ctl->flags & 0x20u) ? 123 : 94);
    } else {
        int hard = 0;

        if (g_10058618[s2_cls100(nx)] & 2) {
            hard = 1;
        } else if (g_10058618[s2_cls80(nx)] & 0x20) {
            if (g_10058618[(self->s2_8830 | 0x100)] & 2)
                hard = 1;
            else if ((self->s2_8830 == 'L' || self->s2_8830 == 'r' ||
                      self->s2_8830 == 'R') && nx != 'N')
                hard = 1;
        }
        if (!hard) {
            if (!(ctl->flags & 0x20u))
                goto vowel_neighbours;
            v = dur_pct(v, 107);
        } else {
            mode = 'a';
            if (nx == 'r') {
                Node *p = self->s2_8820;

                for (;;) {
                    Node *q = Engine_StageNext(self, p);

                    if (q->value == 'r' || q->value == 'R') {
                        p = Engine_StageNext(self, p);
                        continue;
                    }
                    break;
                }
                if (Engine_StageNext(self, p) == NULL ||
                    !(g_10058618[s2_cls100(
                          Engine_StageNext(self, p)->value)] & 2))
                    mode = 'c';
            }
            if (mode == 'a') {
                v = dur_pct(v, (ctl->flags & 0x20u) ? 109 : 86);
            } else {
                if (!(ctl->flags & 0x20u))
                    goto vowel_neighbours;
                v = dur_pct(v, 107);
            }
        }
    }

vowel_neighbours:
    /* the original tests s2_882d against four characters with && rather than
     * ||, so it can never be all four at once and the branch is dead */
    clsb = g_10058618[s2_cls0(self->s2_882d)];
    if (clsb & 0x40) {
        if (clsb & 2) {
            if ((g_10058618[s2_cls80(c)] & 2) ||
                (g_10058618[s2_cls100(c)] & 0x20))
                v = dur_pct(v, mode == 'a' ? 105 : 94);
            else if (mode == 'c')
                v = dur_pct(v, 88);
        } else if (!(g_10058618[s2_cls100(c)] & 0x20)) {
            v = dur_pct(v, mode == 'a' ? 80 : 114);
        } else if (mode == 'a') {
            v = dur_pct(v, 90);
        }
    }
    if (clsb & 0x20) {
        if (g_10058618[s2_cls80(self->s2_882d)] & 8) {
            if (g_10058618[s2_cls180(c)] & 0x20) {
                if (mode == 'c')
                    v = dur_pct(v, 88);
            } else {
                v = dur_pct(v, mode == 'a' ? 79 : 115);
            }
        } else if (g_10058618[s2_cls100(c)] & 0x20) {
            v = dur_pct(v, mode == 'a' ? 97 : 81);
        } else {
            v = dur_pct(v, mode == 'a' ? 83 : 110);
        }
    }
    if (g_10058618[s2_cls180(self->s2_882d)] & 1) {
        uint8_t f = g_10058618[s2_cls100(self->s2_882d)];

        if (f & 0x40) {
            if (g_10058618[s2_cls80(c)] & 2) {
                if (mode == 'a')
                    v = dur_pct(v, 90);
            } else if (mode == 'c') {
                v = dur_pct(v, 88);
            }
        } else if (f & 4) {
            if ((g_10058618[s2_cls80(c)] & 2) ||
                (g_10058618[s2_cls180(c)] & 0x20))
                v = dur_pct(v, mode == 'a' ? 121 : 115);
            else
                v = dur_pct(v, mode == 'a' ? 107 : 97);
        }
    }
    if (clsb & 0x10) {
        if (g_10058618[s2_cls80(c)] & 2) {
            if (mode == 'a')
                v = dur_pct(v, 90);
        } else {
            v = dur_pct(v, mode == 'a' ? 93 : 97);
        }
    }
    if (arg0 == 1) {
        if (!(ctl->flags & 0x20u))
            v = dur_pct(v, 180);
        else
            v += v;
    }
    if (self->s2_3e1 == 'O' &&
        !(g_10058618[s2_cls0(self->s2_882c)] & 4) &&
        (g_10058618[s2_cls80(self->s2_882c)] & 0x20) &&
        !(g_10058618[s2_cls0(self->s2_882d)] & 4) &&
        (g_10058618[s2_cls80(self->s2_882d)] & 0x20))
        v += 0x14;

consonant:
    c = self->s2_3e1;
    if (!(g_10058618[s2_cls0(c)] & 4) &&
        (g_10058618[s2_cls80(c)] & 0x20)) {
        if (c == 'S') {
            int32_t back = Stage2_CountVowels(self, ctl, 0);
            int32_t fwd = Stage2_CountVowels(self, ctl, 1);
            uint8_t amp = (uint8_t)(g_10058618[
                              s2_cls100(self->s2_882d)] & 2);

            if (amp != 0 ||
                (self->s2_882d == '&' && back + fwd != 1))
                v = dur_pct(v, 92);
            if (amp == 0 && self->s2_882d != '&')
                v = v * 7 / 10;
        } else {
            nb = self->s2_882d;
            if (!(g_10058618[s2_cls100(nb)] & 2) && nb != 'L' &&
                nb != 'R' && nb != 'r')
                v = v * 11 / 10;
            if (nb == 'R' || nb == 'r' || nb == 'L')
                v = v * 9 / 10;
        }
    }
    c = self->s2_3e1;
    if (c == 'N' || c == 'L') {
        if (g_10058618[s2_cls100(self->s2_882d)] & 2) {
            v = dur_pct(v, 56);
        } else {
            int32_t back = Stage2_CountVowels(self, ctl, 0);
            int32_t fwd = Stage2_CountVowels(self, ctl, 1);

            if (back + fwd >= 2)
                v = dur_pct(v, 84);
        }
    }

    dur = (v + 5) / 10;
    c = self->s2_3e1;
    if (g_10058618[s2_cls100(c)] & 2) {
        /* a vowel takes a fixed length and the chain above is thrown away */
        if (g_10058618[s2_cls180(c)] & 0x10) {
            dur = (ctl->flags & 0x20u) ? 0x14 : 0x10;
        } else {
            Node *n = ctl->next;
            uint8_t c1 = n->value;
            int plain = 0;

            if ((g_10058618[s2_cls80(c1)] & 1) || c1 == ' ') {
                plain = 1;
            } else {
                uint8_t c2;

                n = n->next;
                c2 = n->value;
                if (c2 == ' ' || (g_10058618[s2_cls80(c2)] & 1)) {
                    plain = 1;
                } else if (c1 == 'r' && c2 == 'R') {
                    Node *p = n->next;

                    if (p->value == 'R') {
                        p = p->next;
                        if (p->value == 'r') {
                            uint8_t c3;

                            p = p->next;
                            c3 = p->value;
                            if (c3 == ' ' ||
                                (g_10058618[s2_cls80(c3)] & 1))
                                plain = 1;
                        }
                    }
                }
            }
            if (plain) {
                if (ctl->flags & 0x20u)
                    dur = 0x10;
                else
                    dur = (c == 'A' || c == 'O') ? 0xc : 0xa;
            } else if (c == 'A' || c == 'O') {
                dur = (ctl->flags & 0x20u) ? 0xe : 0xa;
            } else {
                dur = (ctl->flags & 0x20u) ? 0xc : 0xa;
            }
        }
    }

    if (c == 'L' && (g_10058618[s2_cls0(ctl->next->value)] & 8)) {
        dur = 0xa;
    } else if (c == 'S') {
        uint8_t c1 = ctl->next->value;

        if ((g_10058618[s2_cls80(c1)] & 1) || c1 == ' ')
            dur += 3;
    }
    if (c == 'N' && (g_10058618[s2_cls100(self->s2_882c)] & 2)) {
        uint8_t c1 = ctl->next->value;

        if (c1 == 'D')
            dur = 9;
        else if (c1 == '&')
            dur = 0xb;
    }
    if (c == 'b')
        dur = 6;
    if (c == 'S' && (g_10058618[s2_cls0(self->s2_882d)] & 8) &&
        self->s2_8830 == 'X')
        dur = 9;

    if (dur >= 0x19)
        dur = 0x19;
    ctl->arg = (uint32_t)dur;
    if (ctl->value != 'r') {
        int32_t k = g_rate_phone[es_rate_row(st->rate_index,
                                             ES_RATE_PHONE_ROWS)] >> 1;

        ctl->arg = (uint32_t)((uint32_t)(k * dur) / 50u);
        ctl->arg = (uint32_t)es_rate_scale(st->rate_index, (int32_t)ctl->arg);
        if (ctl->arg <= 2u)
            ctl->arg = 2;
    }

tail:
    if ((ctl->value == 'e' || ctl->value == 'i') && ctl->next != NULL &&
        ctl->next->value == 'N')
        ctl->arg = (uint32_t)(int32_t)((double)ctl->arg * g_dur_before_n);
}

/*: two vowels become one.
 *
 * The control node takes the new character, the stress bits come across from
 * the node being swallowed, and that node is then removed.  The I and U groups
 * also mark backward from the control node when what they swallow was
 * stressed; the A, E and O groups do not. */
static void s2_merge(Engine *self, Node *ctl, uint8_t newc, int mark)
{
    Node *nx = self->s2_8820;

    ctl->value = newc;
    if (nx->flags & 0x18u)
        ctl->flags = (ctl->flags & ~0x18u) | (nx->flags & 0x18u);
    if (mark && (self->s2_8820->flags & 0x18u))
        Stage2_MarkBack(self, ctl);
    self->s2_8820 = Engine_NodeFree(self, self->s2_8820, 1);
    self->s2_87dc--;
    Stage2_Cache(self);
}

/*: drop the next word's first phoneme and rebuild the cache. */
static void s2_drop_next(Engine *self)
{
    self->s2_8820 = Engine_NodeFree(self, self->s2_8820, 1);
    Stage2_Cache(self);
    self->s2_87dc--;
}

/*
 * The phoneme substitutions, all in one pass over the current node.
 *
 * Five things happen here, in this order:
 *
 *   - Stage2_Elide first, for two words meeting on the same phoneme;
 *   - s2_87f0, where the backward vowel count stops, is moved to or cleared
 *     from this node depending on its class;
 *   - a stop: N before a phoneme flagged 0x80 in the 0x100 block becomes ~;
 *     B (unless S, T or X follows), D, G and Y are lowered to b, d, g and y
 *     when what precedes them is the right kind of consonant; and r after one
 *     gets an R and another r put in front of it, so a tap becomes a trill;
 *   - an I or a U with no stress can lose the vowel after it outright;
 *   - and the fourteen diphthongs.  I A, I E, I O, I U, U A, U E, U I, U O,
 *     A I, A U, E I, E U, O I and O U each become one of the lowercase
 *     letters h i j k m p q t a v e w o u, the second vowel's node going
 *     away.  That is what the lowercase half of the phoneme set is.
 *
 * Every arm that changes anything ends by rebuilding the cache, because the
 * node it changed is one of the three the cache holds.
 */
/* @0x10018270 */
void TV_THISCALL Stage2_Substitute(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    Node *ctl = st->ctl;
    uint8_t c, nb, f80;

    if (self->s2_8820 == NULL)
        return;
    if (Stage2_Elide(self))
        Stage2_Cache(self);

    if (self->s2_87f0 == NULL &&
        ((g_10058618[s2_cls100(self->s2_3e1)] & 2) ||
         (g_10058618[s2_cls80(self->s2_3e1)] & 0x20)))
        self->s2_87f0 = st->ctl;
    if (g_10058618[s2_cls0(self->s2_3e1)] & 8)
        self->s2_87f0 = Engine_StageNext(self, st->ctl);

    c = self->s2_3e1;
    f80 = g_10058618[s2_cls80(c)];
    if (f80 & 1)
        self->s2_87f0 = NULL;

    if (f80 & 0x20) {
        int lower = 0;

        if (c == 'N' && (g_10058618[s2_cls100(self->s2_882d)] & 0x80)) {
            ctl->value = '~';
            Stage2_Cache(self);
            return;
        }
        if (c == 'B') {
            nb = self->s2_882d;
            lower = !(nb == 'S' || nb == 'T' || nb == 'X');
        }
        if (!lower && (c == 'D' || c == 'G' || c == 'Y'))
            lower = 1;
        if (!lower) {
            Node *n;

            if (c != 'r')
                return;
            if (Engine_StagePrev(self, ctl) == NULL)
                return;
            nb = self->s2_882c;
            if (!(g_10058618[s2_cls80(nb)] & 0x20))
                return;
            if (nb == 'S' || nb == 'X' || nb == 'Z' || nb == 'R' ||
                nb == 'N' || nb == 'L')
                return;
            n = Engine_NodeAlloc(self, st->ctl, 0, 3, 'R');
            st->ctl = n;
            self->s2_87dc++;
            n = Engine_NodeAlloc(self, n, 0, 3, 'r');
            st->ctl = n;
            self->s2_87dc++;
            Stage2_Cache(self);
            return;
        }

        nb = self->s2_882c;
        if ((g_10058618[s2_cls80(nb)] & 0x40) || nb == '&' ||
            (g_10058618[s2_cls0(nb)] & 0x10) || nb == ' ' ||
            self->s2_881c == NULL)
            return;
        c = (uint8_t)(c + 0x20);
        ctl->value = c;
        self->s2_3e1 = c;
        Stage2_Cache(self);
        return;
    }

    /* an I two words along, with the word between it a single phoneme */
    if ((g_10058618[s2_cls0(self->s2_882d)] & 8) && self->s2_8830 == 'I') {
        Node *n = Engine_StageNext(self, self->s2_8824);

        n = Engine_StageNext(self, n);
        if (n != NULL &&
            (g_10058618[s2_cls0(
                 Engine_StageNext(self, self->s2_8824)->value)] & 8)) {
            Node *p = Engine_StageNext(self, self->s2_8824);

            p = Engine_StageNext(self, p);
            if (!(g_10058618[s2_cls100(p->value)] & 2))
                s2_drop_next(self);
        }
    }

    c = self->s2_3e1;
    if (c == 'I' && !(ctl->flags & 0x18u)) {
        if ((g_10058618[s2_cls0(self->s2_882d)] & 8) &&
            (g_10058618[(self->s2_8830 | 0x100)] & 2)) {
            s2_drop_next(self);
            self->s2_87f0 = ctl;
            Stage2_Cache(self);
        }
        nb = self->s2_882d;
        if (nb == 'A')
            s2_merge(self, ctl, 'h', 1);
        else if (nb == 'E')
            s2_merge(self, ctl, 'i', 1);
        else if (nb == 'O')
            s2_merge(self, ctl, 'j', 1);
        else if (nb == 'U')
            s2_merge(self, ctl, 'k', 1);
        return;
    }

    if (c == 'U' && !(ctl->flags & 0x18u)) {
        if ((g_10058618[s2_cls0(self->s2_882d)] & 8) &&
            self->s2_87f0 == ctl)
            s2_drop_next(self);
        nb = self->s2_882d;
        if (nb == 'A')
            s2_merge(self, ctl, 'm', 1);
        else if (nb == 'E')
            s2_merge(self, ctl, 'p', 1);
        else if (nb == 'I')
            s2_merge(self, ctl, 'q', 1);
        else if (nb == 'O')
            s2_merge(self, ctl, 't', 1);
        return;
    }

    nb = self->s2_882d;
    if (nb != 'I' && nb != 'U')
        return;
    if (self->s2_8820->flags & 0x18u)
        return;
    if (nb == 'I' && (g_10058618[self->s2_8830] & 8)) {
        Node *n = Engine_StageNext(self, self->s2_8824);

        if (n != NULL &&
            (g_10058618[s2_cls100(
                 Engine_StageNext(self, self->s2_8824)->value)] & 2))
            return;
    }

    c = self->s2_3e1;
    if (c == 'A') {
        if (nb == 'I')
            s2_merge(self, ctl, 'a', 0);
        else if (nb == 'U')
            s2_merge(self, ctl, 'v', 0);
    } else if (c == 'E') {
        if (nb == 'I')
            s2_merge(self, ctl, 'e', 0);
        else if (nb == 'U')
            s2_merge(self, ctl, 'w', 0);
    } else if (c == 'O') {
        if (nb == 'I')
            s2_merge(self, ctl, 'o', 0);
        else if (nb == 'U')
            s2_merge(self, ctl, 'u', 0);
    }
}

/*
 * The pause a punctuation mark is worth, as nodes.
 *
 * The rate table gives a quarter of the pause length for this speaking rate,
 * that is scaled by the mark -- 0x23 for a full stop or a question mark, 0x11
 * for a comma, 7 for anything else -- and the result, times four and capped at
 * 0x12c, is laid down 0x32 at a time as type 4 nodes in front of the control
 * node until it runs out or the node pool drops below three.  A full stop or a
 * question mark short enough to fit in one gets two nodes instead, a third and
 * two thirds, so the pause has a shape.
 *
 * Past rate 0x12, or once s2_1d55 says the pool has plenty spare, none of that
 * happens: two fixed nodes go in, of 5 each or of nothing.  A ')' at rate 6 or
 * above does nothing at all.
 */
/* @0x10019eb0 */
void TV_THISCALL Stage2_Punctuation(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    uint8_t c = self->s2_3e1;
    int32_t rate = st->rate_index;
    int32_t want, len, shape;
    Node *n;

    if (c == ')' && rate >= 6)
        return;

    if (rate >= 0x13 || self->s2_1d55 != 0) {
        int32_t fill = self->s2_1d55 != 0 ? 0 : 5;

        n = Engine_NodeAlloc(self, st->ctl, 0, 4, ' ');
        n->arg = (uint32_t)fill;
        n->b15 = 0x32;
        n = Engine_NodeAlloc(self, st->ctl, 0, 4, ' ');
        n->arg = (uint32_t)fill;
        n->b15 = 0x32;
        Stage2_Cache(self);
        return;
    }

    shape = 0;
    if (c == '.' || c == '?') {
        want = 0x23;
        shape = 1;
    } else {
        want = c == ',' ? 0x11 : 7;
    }
    len = (int32_t)((uint32_t)((g_rate_pause[rate] / 4) * want) / 100u) * 4;
    if (len > 0x12c)
        len = 0x12c;

    while (len >= 1) {
        if (shape && len < 0xff) {
            n = Engine_NodeAlloc(self, st->ctl, 0, 4, ' ');
            shape = 0;
            n->arg = (uint32_t)(len / 3);
            n->b15 = 0x32;
            n = Engine_NodeAlloc(self, st->ctl, 0, 4, ' ');
            n->arg = (uint32_t)(len * 2 / 3);
            n->b15 = 0x32;
        } else {
            n = Engine_NodeAlloc(self, st->ctl, 0, 4, ' ');
            n->arg = (uint32_t)(len < 0x32 ? len : 0x32);
            n->b15 = 0x32;
        }
        len -= 0x32;
        /* the original has an arm here for 0 < len < 1, which no integer is */
        if (self->free_nodes < 3)
            break;
    }
    Stage2_Cache(self);
}

/*
 * One word boundary's worth of work.
 *
 * Two searches first: back from s2_881c to the nearest type 4 node, which goes
 * in s2_8828, and forward from s2_8820 to the nearest phoneme node the table
 * marks 0x80, which goes in s2_8818.  Then the boundary is classified, the
 * level worked out, the duration worked out, and -- for a phoneme the table
 * marks 0x80 -- the node behind is merged in.  A space does none of it.
 */
/* @0x10019b10 */
void TV_THISCALL Stage2_Boundary(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];
    Node *back = self->s2_881c;
    Node *fwd = self->s2_8820;

    if (st->ctl->value == ' ')
        return;

    self->s2_8818 = NULL;
    self->s2_8828 = NULL;
    while (back != NULL) {
        if ((back->flags & 7) == 4) {
            self->s2_8828 = back;
            break;
        }
        back = Node_PrevBoundary(self, back);
    }
    while (fwd != NULL) {
        if ((fwd->flags & 7) == 3 &&
            (g_10058618[s2_cls0(fwd->value)] & 0x80)) {
            self->s2_8818 = fwd;
            break;
        }
        fwd = Node_NextWord(self, fwd);
    }

    Stage2_Classify(self);
    Stage2_Contour(self);
    Stage2_Duration(self);
    if (g_10058618[s2_cls0(self->s2_3e1)] & 0x80)
        Stage2_MergeBack(self);
}

/*
 * Stage 2, one step at a time.
 *
 * Engine_Step calls this with the other four stages.  Each pass round the loop
 * takes one node: a phoneme gets its length and level latched, the
 * substitutions applied, its boundary worked out, the punctuation pause
 * inserted and then the length and level written back -- with the node
 * temporarily retyped to 4 so that Stage2_Adjust treats it as a pause -- and
 * anything else is just stepped over.  The loop ends when Stage2_Advance says
 * the cursor did not move, which is stage 2's way of saying there is nothing
 * more it can do yet.
 *
 * When w_212c is set every phoneme, and a '1' or a '2' for a stressed vowel,
 * also goes into the byte list at s2_trace -- the phoneme trace the host can
 * ask for.
 */
/* @0x1001a070 */
uint8_t TV_THISCALL Stage2_Run(Engine *self)
{
    StageCtx *st = &self->stage_ctx[2];

    if (self->s2_87f9 != 0) {
        self->s2_87f9 = 0;
        Stage2_ClearRun(self);
    }
    if (!Engine_StageBegin(self, st) &&
        self->s2_87e0 > self->s2_87dc && self->s2_87d2 == 0)
        goto done;
    if (st->ctl == NULL)
        return Engine_StageEnd(self);

    Stage2_RunControls(self);
    self->s2_1d55 = 0;
    do {
        if (self->s2_87d1 == 0 && self->s2_87e0 > self->s2_87dc &&
            !Stage2_Word(self))
            goto done;

        if ((st->ctl->flags & 7) == 3) {
            Stage2_Adjust(self, 0);
            Stage2_Cache(self);
            if ((st->p_34 & 0x100) || st->p_1c == 0)
                Stage2_Substitute(self);
            Stage2_Boundary(self);

            if (self->w_212c != 0) {
                Node *ctl = st->ctl;

                ByteList_Add(self->s2_trace, ctl->value);
                ctl = st->ctl;
                if (g_10058618[s2_cls100(ctl->value)] & 2) {
                    uint32_t lv = ctl->flags & 0x18u;

                    if (lv > 8u)
                        ByteList_Add(self->s2_trace, '1');
                    else if (lv == 8u)
                        ByteList_Add(self->s2_trace, '2');
                }
            }

            if (g_10058618[s2_cls80(self->s2_3e1)] & 0x40)
                Stage2_Punctuation(self);

            if (self->s2_3d8 != 0) {
                Node *ctl = st->ctl;

                /* retyped to 4 for the write-back and put back after */
                ctl->flags = (ctl->flags & ~3u) | 4u;
                Stage2_Adjust(self, 1);
                st->ctl->flags &= ~0x18u;
            }
        }
    } while (!Stage2_Advance(self));

done:
    {
        uint8_t r = Engine_StageEnd(self);

        if (st->last == NULL)
            self->s2_8808 = 0;
        return r;
    }
}
