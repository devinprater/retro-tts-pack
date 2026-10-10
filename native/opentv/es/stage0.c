/*
 * Stage 0 helpers.
 *
 * Stage 0 is the letter-to-sound pass: a bytecode interpreter walking the
 * rule table, with a call stack of twenty frames, matching the characters in
 * its window against word lists and character classes.  The interpreter
 * itself is `Stage0_Run`, at the end of the file; the rest is what resets it,
 * how it classifies a character, how it matches a word, how it places the
 * stress and how it hands its work to stage 1.
 */
#include "es_engine.h"

/* The rule bytecode the interpreter walks. */
/* @0x1005fb88 */
extern const uint8_t g_stage0_rules[];

/* Word lists, one list per list number, each a run of stored addresses ending
 * in a zero.  Each string is the word, a NUL, and then the replacement text the
 * rule uses.  Two levels of stored address, so two levels of tv_ref: see
 * src/tv_ref.h, and Lts_MatchAffix in src/engine/stage1.c, which reads
 * English's equivalent the same way. */
/* @0x1005fb18 */
extern const tv_ref g_stage0_words[16];

/* @0x1001427c */
extern const uint8_t g_s0_class_index[16];

/* @0x10013310 */
void TV_THISCALL Stage0_Reset(Engine *self)
{
    self->s0_sp = self->s0_stack;
    self->s0_1b3d = 0;
    self->s0_1c1c = 0;
    self->stage_ctx[0].type_mask = 0x17; /* types 0, 1, 2 and 4 */
    self->s0_ip = g_stage0_rules;
    self->s0_need_test = 1;
    self->s0_pending_ptr = &self->s0_pending;
}

/* Is this character in that class?  The sixteen class numbers map onto six
 * tests through an index table, and the table is byte for byte the English
 * one.  What the tests do is not quite: the letter class here also accepts
 * '~' and '`' alongside the apostrophe, which is what Accent_Split leaves
 * behind when it splits an accented character into a base and a mark. */
/* @0x100141d0 */
uint8_t TV_CDECL Stage0_CharClass(int32_t cls, uint8_t c)
{
    int32_t i = cls - 4;
    if ((uint32_t)i > 15)
        return 0;
    /* The original compares signed throughout, which comes to the same thing
     * for every byte value: a byte of 0x80 or more fails the lower bound
     * signed and the upper bound unsigned.  The exception is the control
     * test, where the sign is the point and the cast is kept. */
    switch (g_s0_class_index[i]) {
    case 0:
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               c == '\'' || c == '~' || c == '`';
    case 1:
        return c >= '0' && c <= '9';
    case 2:
        return c >= 'a' && c <= 'z';
    case 3:
        return c >= 'A' && c <= 'Z';
    case 4:
        return (int8_t)c < 0x20;
    case 5:
        return c == '.';
    default:
        return 0;
    }
}

/* Does the window start with any word from this list?  On a match,
 * s0_word_data is left pointing just past the word's terminator, which is
 * where the rule finds what to say instead. */
/* @0x10014130 */
uint8_t TV_THISCALL Stage0_MatchWord(Engine *self, uint8_t list, uint8_t fold_case)
{
    StageCtx *st = &self->stage_ctx[0];
    const tv_ref *words = TV_REF(tv_ref, g_stage0_words[list]);
    const char *w;
    Node *n;

    for (;;) {
        if (!TV_REF_OK(*words))
            return 0;
        w = TV_REF(char, *words);
        words++;
        n = st->d14;
        for (;;) {
            uint8_t c;
            st->d18 = n;
            n = st->d18;
            c = n->value;
            if (fold_case == 1 && c >= 'A' && c <= 'Z')
                c += 0x20;
            if (NODE_TYPE(n) == 1 && (uint8_t)*w == c) {
                w++;
                if (*w != 0 && st->scan != n) {
                    n = n->next;
                    continue;
                }
            }
            break;
        }
        if (*w == 0 && st->scan == st->d18) {
            self->s0_word_data = (const uint8_t *)(w + 1);
            /* English also raises s0_1c1c for lists 1, 0xd and 0xe here.
             * This engine does not. */
            return 1;
        }
        w++;
    }
}

/* Close the stage: decide how far it got and pass that to stage 1. */
/* @0x10014010 */
uint8_t TV_THISCALL Stage0_Finish(Engine *self, uint8_t done)
{
    StageCtx *st = &self->stage_ctx[0];

    if (done) {
        st->cur = st->ctl;
    } else {
        st->cur = st->first;
        if (st->ctl != st->first) {
            for (;;) {
                Node *n = st->cur;
                uint32_t t = NODE_TYPE(n);
                if (t != 0 && (t != 3 || n->value != ']'))
                    break;
                st->cur = Engine_StageNext(self, n);
                if (st->cur == st->ctl)
                    break;
            }
        }
    }
    if (Engine_StageEnd(self))
        done = 1;
    return done;
}

/*
 * Put one node into stage 0's output.
 *
 * Two places it can go.  While the stage still has a control node it goes in
 * front of that, and while it does not it goes on the end of the work list
 * and becomes stage 0's last node -- and its first, if there was none.  The
 * node just made is reached through Engine_StagePrev in the first case,
 * because Engine_NodeAlloc put it before the cursor rather than after it.
 *
 * `mark` rewrites two of the new node's per-stage flag bits, clearing 8 and
 * setting 0x10.
 */
/* @0x10014090 */
void TV_THISCALL Stage0_Emit(Engine *self, int32_t type, uint8_t value,
                             uint8_t mark)
{
    StageCtx *st = &self->stage_ctx[0];

    if (st->ctl != NULL) {
        Engine_NodeAlloc(self, st->ctl, 0, type, value);
        if (mark != 0) {
            Node *n = Engine_StagePrev(self, st->ctl);

            n->flags = (n->flags & ~8u) | 0x10u;
        }
        return;
    }
    st->last = Engine_NodeAlloc(self, self->work_tail->prev, 1, type, value);
    if (mark != 0)
        st->last->flags = (st->last->flags & ~8u) | 0x10u;
    if (st->first == NULL)
        st->first = st->last;
}

/*
 * The nearest vowel behind a node.
 *
 * Walks back with Engine_StagePrev until a node's value is one of A E I O U
 * and returns it, or gives up at stage 0's control node or at the start of
 * the window.  Only the five uppercase vowels count: stage 0 has not lowered
 * anything yet.
 */
/* @0x10014720 */
Node *TV_THISCALL Stage0_PrevVowel(Engine *self, Node *n)
{
    Node *p = Engine_StagePrev(self, n);

    while (p != NULL) {
        uint8_t c = p->value;

        if (c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U')
            return p;
        if (self->stage_ctx[0].ctl == p)
            return NULL;
        p = Engine_StagePrev(self, p);
    }
    return NULL;
}

/* Seventeen two-letter words that take no stress: EL LA AL MI NI TU SU SI DE
 * EN SO ME TE SE LO LE OS.  The pairs are stored first letter then second. */
/* @0x10048840 */
extern const char g_stress_two[34];
/* The same for three to seven letters, as one run of fixed-width words per
 * length: TV_REF(char, g_stress_words[n]) holds g_stress_len[n] bytes of n-letter words.
 * Entries 0 to 2 are never read -- a one-letter word is left alone and a
 * two-letter one takes the pair table above. */
/* @0x1004885c */
extern const int32_t g_stress_len[12];
/* @0x100577ec */
extern const tv_ref g_stress_words[12];

/*
 * Which vowel of the word carries the stress.
 *
 * The word is stage 0's window, from ctl to scan.  The scan counts its nodes
 * and its vowels and keeps the last three vowels; then:
 *
 *   - one node, or a word in one of the function-word lists, gets nothing;
 *   - a word ending in D L R Z Y J X T C or B is stressed on its last vowel;
 *   - a single vowel takes it, wherever it is;
 *   - otherwise the stress goes on the second-to-last vowel, except that a
 *     diphthong -- two adjacent vowels, one of them I or U -- counts as one,
 *     so the stress moves to whichever of the pair is not I or U, and the
 *     search steps back another vowel when the pair is adjacent.
 *
 * `adverb` asks only about -MENTE: a word of nine nodes or more ending in
 * M-E-N-T-E gets its stress on the E of MENTE and nothing else happens.  With
 * it clear, the same ending is found for a long word and the stress search is
 * then rewound past the four nodes MENTE occupies, so the stem is stressed as
 * a word in its own right.
 */
/* @0x10014310 */
void TV_THISCALL Stage0_Stress(Engine *self, uint8_t adverb)
{
    StageCtx *st = &self->stage_ctx[0];
    Node *n = st->ctl;
    Node *v_last = NULL, *v_mid = NULL, *v_prev = NULL;
    int32_t n_count = 0, n_vowels = 0;
    uint8_t c_last, c_mid, c_prev, c_lastv;

    for (;;) {
        uint8_t c = n->value;

        n_count++;
        if (c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U') {
            n_vowels++;
            v_prev = v_mid;
            v_mid = v_last;
            v_last = n;
        }
        if (st->scan == n)
            break;
        n = Engine_StageNext(self, n);
    }

    c_last = n->value;
    if (n_vowels == 0)
        return;

    if (adverb) {
        Node *p;

        if (n_count < 9 || c_last != 'E' || v_mid == NULL ||
            v_mid->value != 'E')
            return;
        p = Engine_StagePrev(self, n);
        if (p->value != 'T')
            return;
        p = Engine_StagePrev(self, p);
        if (p->value != 'N')
            return;
        p = Engine_StagePrev(self, v_mid);
        if (p->value != 'M')
            return;
        v_mid->flags |= 0x10u;
        return;
    }

    if (n_count == 1)
        return;
    if (n_count == 2) {
        int32_t i;

        for (i = 0; i < 0x22; i += 2)
            if ((uint8_t)g_stress_two[i + 1] == c_last &&
                Engine_StagePrev(self, n)->value ==
                    (uint8_t)g_stress_two[i])
                return;
    } else if (n_count <= 7) {
        const char *words = TV_REF(char, g_stress_words[n_count]);
        int32_t len = g_stress_len[n_count];
        int32_t off;

        for (off = 0; off < len; off += n_count) {
            Node *p;
            int32_t k;

            if ((uint8_t)words[off + n_count - 1] != c_last)
                continue;
            p = st->scan;
            k = n_count - 1;
            for (;;) {
                p = Engine_StagePrev(self, p);
                k--;
                if ((uint8_t)words[k + off] != p->value)
                    break;
                if (k == 0)
                    return;
            }
        }
    } else {
        /* the -MENTE ending: stress the E and rewind past the whole of it */
        Node *p;

        if (c_last != 'E' || v_mid == NULL || v_mid->value != 'E')
            goto general;
        p = Engine_StagePrev(self, n);
        if (p->value != 'T')
            goto general;
        p = Engine_StagePrev(self, p);
        if (p->value != 'N')
            goto general;
        p = Engine_StagePrev(self, v_mid);
        if (p->value != 'M')
            goto general;
        v_mid->flags |= 0x10u;
        p = Engine_StagePrev(self, p);
        c_last = p->value;
        v_last = v_prev;
        n_vowels -= 2;
        v_mid = Stage0_PrevVowel(self, v_prev);
        v_prev = Stage0_PrevVowel(self, v_mid);
    }

general:
    if (c_last == 'D' || c_last == 'L' || c_last == 'R' || c_last == 'Z' ||
        c_last == 'Y' || c_last == 'J' || c_last == 'X' || c_last == 'T' ||
        c_last == 'C' || c_last == 'B') {
        v_last->flags |= 0x10u;
        return;
    }
    if (n_vowels == 1) {
        v_last->flags |= 0x10u;
        return;
    }

    c_prev = v_prev != NULL ? v_prev->value : '0';
    c_mid = v_mid != NULL ? v_mid->value : '0';
    c_lastv = v_last != NULL ? v_last->value : '0';

    if (v_last->prev == v_mid) {
        if (c_mid != 'I' && c_mid != 'U' && c_lastv != 'I' &&
            c_lastv != 'U') {
            v_mid->flags |= 0x10u;
            return;
        }
        if (n_vowels == 2) {
            if (c_mid == 'I' || c_mid == 'U')
                v_last->flags |= 0x10u;
            else
                v_mid->flags |= 0x10u;
            return;
        }
        if (n_vowels == 3) {
            v_prev->flags |= 0x10u;
            return;
        }
        if (v_prev->next == v_mid)
            v_prev = Stage0_PrevVowel(self, v_prev);
        v_mid = v_prev;
        v_prev = Stage0_PrevVowel(self, v_mid);
        c_prev = v_prev != NULL ? v_prev->value : '0';
        c_mid = v_mid != NULL ? v_mid->value : '0';
    }

    if (v_mid->prev != v_prev) {
        v_mid->flags |= 0x10u;
        return;
    }
    if (c_prev == 'I' || c_prev == 'U')
        v_mid->flags |= 0x10u;
    else if (c_mid == 'I' || c_mid == 'U')
        v_prev->flags |= 0x10u;
    else
        v_mid->flags |= 0x10u;
}

/* The characters a rule can ask about by name: everything but C, F, I, N and
 * x, which mean something else to the interpreter. */
/* @0x10014290 */
uint8_t TV_CDECL Stage0_IsPlain(uint8_t c)
{
    static const char SPECIAL[] = "CFINx";
    int i;

    for (i = 0; SPECIAL[i] != 0; i++)
        if ((uint8_t)SPECIAL[i] == c)
            return 0;
    return 1;
}

/* The punctuation a rule's "is this a word character" test accepts. */
/* @0x1005fb78 */
extern const char g_s0_punct[14];
/* Strings the rules emit, addressed by a 16-bit offset from here. */
/* @0x10060840 */
extern const char g_s0_text[];
/* @0x10049910 */
extern const uint32_t g_bit_mask[8];
/* @0x10058618 */
extern const uint8_t g_10058618[0x200];

/*: the flag table index stage 0 uses, which is only ever the unshifted one. */
static int32_t s0_cls0(uint8_t v)
{
    return (int32_t)(int16_t)(int8_t)v;
}

/*
 * The letter-to-sound rule interpreter.
 *
 * One call runs the machine until the window it was given is used up, and the
 * machine is a loop over two halves.  The first, taken when s0_need_test is
 * up, evaluates one *test* opcode -- nineteen of them, asking about the host's
 * flags, the character under the cursor, a word list, one of the interpreter's
 * twelve byte registers -- and the byte after it holds two nibbles: how many
 * *action* bytes follow for a match and how many for a miss.  The one that
 * does not apply is skipped.
 *
 * The second half runs those action bytes: twenty-three opcodes that emit
 * characters, move and remove nodes, set a register, place the stress, call
 * Engine_RunControl, or return from a rule.  A byte with bit 7 set is not an
 * opcode but a jump -- 0x80 for a plain one and 0xc0 for a call, which pushes
 * the return address, the action count and the skip onto a twenty-deep stack.
 *
 * s0_ip walks g_stage0_rules throughout.  The three places that give up --
 * running out of window, running out of list, and the 'return from the
 * outermost rule' opcode -- all hand over with Stage0_Finish.
 */
/* @0x10013360 */
uint8_t TV_THISCALL Stage0_Run(Engine *self)
{
    StageCtx *st = &self->stage_ctx[0];
    uint8_t esc = 0;

    if (!Engine_StageBegin(self, st))
        return 0;
    if (st->d14 == NULL)
        st->d14 = st->ctl;

    for (;;) {
        const uint8_t *ip;
        uint8_t nib;

        if (self->s0_need_test != 0) {
            uint8_t matched = 0;

            ip = self->s0_ip;
            switch (*ip) {
            case 0:
                if (st->p_1c == 1)
                    matched = 1;
                break;
            case 1:
                if (st->p_20 == 0)
                    matched = 1;
                break;
            case 2:
                self->s0_ip = ++ip;
                if (st->p_34 & (int32_t)g_bit_mask[*ip])
                    matched = 1;
                break;
            case 3: {
                uint8_t c = st->scan->value;

                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    c == '\'') {
                    matched = 1;
                } else {
                    int32_t i = 0;

                    while (g_s0_punct[i] != 0) {
                        if ((uint8_t)g_s0_punct[i] == c) {
                            matched = 1;
                            break;
                        }
                        i++;
                    }
                }
                break;
            }
            case 4:
                if (Stage0_CharClass(4, st->scan->value))
                    matched = 1;
                break;
            case 5:
                if (Stage0_CharClass(5, st->scan->value))
                    matched = 1;
                break;
            case 6:
                if (Stage0_CharClass(6, st->scan->value))
                    matched = 1;
                break;
            case 7:
                if (Stage0_CharClass(7, st->scan->value))
                    matched = 1;
                break;
            case 8: {
                /* look ahead up to twenty nodes for one the class accepts */
                int32_t k = 0;

                st->d18 = st->scan;
                for (;;) {
                    Node *n;

                    if (st->last == st->d18)
                        return Stage0_Finish(self, 0);
                    n = st->d18->next;
                    st->d18 = n;
                    if ((n->flags & 7) == 0) {
                        if (Stage0_IsPlain(n->value)) {
                            k++;
                        } else if (Stage0_CharClass(self->s0_ip[1], 'a')) {
                            matched = 1;
                            self->s0_ip++;
                            goto tested;
                        } else {
                            self->s0_ip++;
                            goto tested;
                        }
                    } else if (Stage0_CharClass(self->s0_ip[1], n->value)) {
                        matched = 1;
                        self->s0_ip++;
                        goto tested;
                    } else if (st->d18->value == ' ') {
                        k++;
                    } else {
                        /* the original also asks whether the same byte is
                         * both '~' and '`', which no byte is */
                        self->s0_ip++;
                        goto tested;
                    }
                    if (k >= 0x14) {
                        self->s0_ip++;
                        goto tested;
                    }
                }
            }
            case 9:
                self->s0_ip = ++ip;
                {
                    uint8_t r = self->s0_reg[*ip];

                    self->s0_ip = ++ip;
                    if (*ip == r)
                        matched = 1;
                }
                break;
            case 10:
                self->s0_ip = ++ip;
                {
                    uint8_t r = self->s0_reg[*ip];

                    self->s0_ip = ++ip;
                    if (*ip < r)
                        matched = 1;
                }
                break;
            case 11:
                self->s0_ip = ++ip;
                if ((int32_t)(int8_t)st->scan->value == (int32_t)*ip)
                    matched = 1;
                break;
            case 12:
                self->s0_ip = ++ip;
                matched = Stage0_MatchWord(self, *ip, 1);
                break;
            case 13:
                if (st->last == st->scan)
                    return Stage0_Finish(self, 0);
                st->scan = Engine_StageNext(self, st->scan);
                if (!(st->scan->flags & 7))
                    matched = 1;
                break;
            case 14:
                if (st->scan == NULL)
                    return Stage0_Finish(self, 0);
                if (!(st->scan->flags & 7))
                    matched = 1;
                break;
            case 15:
                matched = 1;
                break;
            case 16:
                if (Stage0_IsPlain(st->scan->value))
                    matched = 1;
                break;
            case 17: {
                const char *p;

                self->s0_ip = ++ip;
                {
                    uint32_t hi = *ip;

                    self->s0_ip = ++ip;
                    p = g_s0_text + ((hi << 8) | *ip);
                }
                if (*p != 0) {
                    uint8_t c = st->scan->value;

                    while ((uint8_t)*p != c) {
                        p++;
                        if (*p == 0)
                            break;
                    }
                    if ((uint8_t)*p == c)
                        matched = 1;
                }
                break;
            }
            case 18:
                self->s0_ip = ++ip;
                matched = Stage0_MatchWord(self, *ip, 0);
                break;
            default:
                break;
            }

        tested:
            self->s0_need_test = 0;
            ip = self->s0_ip + 1;
            self->s0_ip = ip;
            nib = *ip;
            if (matched) {
                self->s0_nact = (uint8_t)(nib >> 4);
                self->s0_skip = (uint8_t)(nib & 0xf);
            } else {
                self->s0_skip = 0;
                self->s0_nact = (uint8_t)(nib & 0xf);
                ip += nib >> 4;
            }
            self->s0_ip = ip + 1;
        }

        /* --- the actions ------------------------------------------------- */
        while (self->s0_nact != 0) {
            uint8_t op;

            ip = self->s0_ip;
            op = *ip;
            if (op & 0x80) {
                uint32_t hi;

                if ((op & 0xc0) == 0x80) {
                    self->s0_ip = ++ip;
                    hi = (uint32_t)(op & 0x3f);
                    self->s0_ip = g_stage0_rules + ((hi << 8) | *ip);
                } else {
                    self->s0_sp->ip = ip;
                    self->s0_sp->nactions = self->s0_nact;
                    self->s0_sp->skip = self->s0_skip;
                    self->s0_sp++;
                    if (self->s0_sp == self->s0_stack + 20)
                        Engine_Error(self, 0xc);
                    ip = self->s0_ip;
                    hi = (uint32_t)(*ip & 0x3f);
                    self->s0_ip = ++ip;
                    self->s0_ip = g_stage0_rules + ((hi << 8) | *ip);
                }
                self->s0_nact = 1;
                self->s0_skip = 0;
                break;
            }
            if (op > 0x16)
                goto next_action;

            switch (op) {
            case 0:
            case 1: {
                const char *p;
                uint32_t hi;

                self->s0_ip = ++ip;
                hi = *ip;
                self->s0_ip = ++ip;
                p = g_s0_text + ((hi << 8) | *ip);
                while (*p != 0) {
                    uint8_t c = (uint8_t)*p;

                    if (c == '\'') {
                        esc = 1;
                        p++;
                    } else {
                        int32_t type = self->s0_ip[-2] == 0 ? 2 : 3;

                        Stage0_Emit(self, type, c, esc);
                        p++;
                        esc = 0;
                    }
                }
                self->s0_nact = (uint8_t)(self->s0_nact - 2);
                esc = 0;
                goto next_action;
            }
            case 2:
                goto advance_all;
            case 3: {
                int32_t hard = 0;

                for (;;) {
                    Node *n = st->d14;
                    uint8_t c = n->value;

                    if (c == '\'') {
                        esc = 1;
                        if (st->ctl == n)
                            st->ctl = Engine_StageNext(self, st->ctl);
                        st->d14 = Engine_NodeFree(self, st->d14, 1);
                        continue;
                    }
                    if (c >= 'a' && c <= 'z') {
                        n->value = (uint8_t)(c - 0x20);
                    } else if (c == '~') {
                        n->value = '@';
                    } else if (c == '`') {
                        if (st->ctl == n)
                            st->ctl = Engine_StageNext(self, st->ctl);
                        st->d14 = Engine_NodeFree(self, st->d14, 0);
                        st->d14->next->flags |= 0x40u;
                    }
                    if (esc == 1) {
                        hard = 1;
                        st->d14->flags = (st->d14->flags & ~8u) | 0x10u;
                    }
                    esc = 0;
                    st->d14->flags = (st->d14->flags & ~5u) | 2u;
                    if (st->scan == st->d14)
                        break;
                    st->d14 = st->d14->next;
                }
                Stage0_Stress(self, (uint8_t)hard);
                goto advance_all;
            }
            case 4:
                for (;;) {
                    Node *n = st->ctl;

                    if (n->value == '\'') {
                        esc = 1;
                        if (st->scan == n)
                            goto clear_esc;
                        st->ctl = Engine_NodeFree(self, n, 1);
                        continue;
                    }
                    n->flags = (n->flags & ~4u) | 3u;
                    if (esc == 1)
                        st->ctl->flags = (st->ctl->flags & ~8u) | 0x10u;
                    esc = 0;
                    if (st->scan == st->ctl)
                        goto clear_esc;
                    st->ctl = st->ctl->next;
                }
            case 5:
                while (*self->s0_word_data != 0) {
                    uint8_t c = *self->s0_word_data;

                    if (c == '\'') {
                        esc = 1;
                    } else {
                        Stage0_Emit(self, 3, c, esc);
                        esc = 0;
                    }
                    self->s0_word_data++;
                }
                goto next_action;
            case 6: {
                uint8_t r;

                self->s0_ip = ++ip;
                r = *ip;
                self->s0_ip = ++ip;
                self->s0_reg[r] = *ip;
                self->s0_nact = (uint8_t)(self->s0_nact - 2);
                goto next_action;
            }
            case 7:
                self->s0_ip = ++ip;
                self->s0_reg[*ip]++;
                self->s0_nact--;
                goto next_action;
            case 8:
                self->s0_ip = ++ip;
                self->s0_reg[*ip]--;
                self->s0_nact--;
                goto next_action;
            case 9:
                st->d14 = st->ctl;
                st->scan = st->ctl;
                goto next_action;
            case 10:
                st->scan = Engine_StageNext(self, st->scan);
                goto next_action;
            case 11:
                if (st->scan == NULL)
                    st->scan = st->last;
                else
                    st->scan = st->scan->prev;
                goto next_action;
            case 12:
                if (st->scan == st->ctl) {
                    Node *n = Engine_StageNext(self, st->ctl);

                    st->d14 = n;
                    st->ctl = n;
                }
                st->scan = Engine_NodeFree(self, st->scan, 1);
                goto next_action;
            case 13: {
                Node *n;

                if (st->scan != st->ctl)
                    do {
                        st->ctl = Engine_NodeFree(self, st->ctl, 1);
                    } while (st->scan != st->ctl);
                n = Engine_NodeFree(self, st->ctl, 1);
                st->ctl = n;
                st->d14 = n;
                st->scan = n;
                goto next_action;
            }
            case 14:
                Engine_NodeAlloc(self, st->scan, 1, 2, ip[1]);
                st->scan = st->scan->next;
                self->s0_ip++;
                self->s0_nact--;
                goto next_action;
            case 15:
                self->s0_ip = ++ip;
                st->scan->value = *ip;
                self->s0_nact--;
                goto next_action;
            case 16:
                self->s0_ip = ++ip;
                Stage0_MatchWord(self, *ip, 1);
                self->s0_nact--;
                goto next_action;
            case 17:
                if (self->s0_1b3d == 0) {
                    self->s0_1b3d = 1;
                    return Stage0_Finish(self, 1);
                }
                self->s0_1b3d = 0;
                goto next_action;
            case 18:
                Engine_RunControl(self);
                goto next_action;
            case 19: {
                S0Frame *f;

                if (self->s0_sp == self->s0_stack)
                    Engine_Error(self, 0x18);
                self->s0_sp--;
                f = self->s0_sp;
                self->s0_ip = f->ip + 1;
                self->s0_skip = f->skip;
                self->s0_nact = (uint8_t)(f->nactions - 1);
                goto next_action;
            }
            case 20:
            case 21: {
                Node *ctl;
                int32_t num[2];
                int32_t *at;
                uint8_t c;

                if (op == 20) {
                    Node *n = st->ctl;

                    if (n->value == '\'') {
                        n = Engine_NodeFree(self, n, 1);
                        st->d14 = n;
                        st->ctl = n;
                        n->flags = (n->flags & ~8u) | 0x10u;
                    }
                    st->ctl->flags |= 0x80u;
                }
                {
                    Node *n = st->ctl;

                    if (n->value == '\'') {
                        n = Engine_NodeFree(self, n, 1);
                        st->d14 = n;
                        st->ctl = n;
                        n->flags = (n->flags & ~8u) | 0x10u;
                    }
                }
                if (*self->s0_ip == 0x15)
                    st->ctl->flags &= ~0x80u;

                ctl = st->ctl;
                num[0] = -1;
                num[1] = -1;
                at = &num[0];
                st->d18 = ctl->next;
                for (;;) {
                    Node *n = st->d18->next;

                    st->d18 = n;
                    c = n->value;
                    if (c >= '0' && c <= '9') {
                        if (*at < 0)
                            *at = (int32_t)(int8_t)c - 0x30;
                        else
                            *at = *at * 10 + (int32_t)(int8_t)c - 0x30;
                        if (*at <= 0xff)
                            continue;
                    }
                    if (c == ';') {
                        at++;
                        if (at < &num[2])
                            continue;
                    }
                    break;
                }
                if (num[0] < 0)
                    num[0] = 0;
                else if (num[0] == 0)
                    num[0] = 2;
                if (num[1] < 0)
                    num[1] = 0;

                if (c == '!' && num[0] <= 0x12c &&
                    ((num[1] <= 0xa0 && num[1] >= 0x28) || num[1] == 0) &&
                    (g_10058618[s0_cls0(ctl->value)] & 0x80)) {
                    if (num[0] != 0)
                        ctl->b15 = (uint8_t)((num[0] - 100) >> 1);
                    if (num[1] != 0)
                        st->ctl->arg = (uint32_t)(num[1] - 100);
                    goto next_action;
                }
                if (c == '/' && (num[0] >= 0x32 || num[0] <= 2) &&
                    num[0] <= 0xc8 && num[1] <= 0x3c &&
                    (g_10058618[s0_cls0(ctl->value)] & 0x80)) {
                    ctl->b15 = (uint8_t)(num[0] >> 1);
                    st->ctl->arg = (uint32_t)num[1];
                }
                goto next_action;
            }
            default:
                st->d14 = st->scan;
                goto next_action;
            }

        clear_esc:
            esc = 0;
        advance_all:
            {
                Node *n = Engine_StageNext(self, st->scan);

                st->scan = n;
                st->d14 = n;
                st->ctl = n;
            }
        next_action:
            self->s0_ip++;
            self->s0_nact--;
        }

        self->s0_need_test = 1;
        self->s0_ip += self->s0_skip;
    }
}
