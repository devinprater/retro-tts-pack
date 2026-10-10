/*
 * Stage 1: word boundaries and the exception lexicon.
 *
 * Stage 0 has turned letters into phonemes; stage 1 finds where one word ends
 * and the next begins, marks the boundary with a '&', and looks the word up in
 * the lexicon that overrides the rules.
 */
#include "es_engine.h"

/* @0x10058618 */
extern const uint8_t g_10058618[0x200];

/*: the two ways this file indexes the flag table; see lang/spa/engine/adjust.c. */
static int32_t s1_cls0(uint8_t v)
{
    return (int32_t)(int16_t)(int8_t)v;
}

static int32_t s1_cls80(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x80);
}

/*
 * Look ahead for the end of the word and, where it belongs, put a '&' there.
 *
 * The scan starts after stage 1's scan pointer and runs forward past anything
 * the stage does not select and anything the 0x80 block marks 1, until it
 * finds a node it does select.  What that node is decides the answer:
 *
 *   0  the window is already finished, or a 'C' came first, or the node is
 *      marked 1 in the 0x80 block
 *  -1  the list ran out
 *   2  a phoneme, or anything that is not a space, that the table does not
 *      mark 8 -- so the word does not end here after all
 *   1  a '&' was put in: the run of spaces and 8-marked nodes after the
 *      boundary is removed and the first of them becomes the marker
 */
/* @0x10010480 */
int32_t TV_THISCALL Stage1_WordMark(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    Node *at, *mark;
    uint8_t c;

    at = st->scan;
    if (at != NULL && st->last == at && st->cur == st->ctl)
        return 0;

    st->d14 = Engine_StageNext(self, at);
    if (st->d14 == NULL)
        return -1;

    for (;;) {
        Node *n;

        if (Engine_TypeSelected(self, st->d14))
            break;
        n = st->d14;
        c = n->value;
        if (g_10058618[s1_cls80(c)] & 1)
            break;
        if (!(n->flags & 7) && c == 'C')
            return 0;
        st->d14 = Engine_StageNext(self, n);
        if (st->d14 == NULL)
            return -1;
    }

    mark = st->d14;
    c = mark->value;
    if (g_10058618[s1_cls80(c)] & 1)
        return 0;
    if ((mark->flags & 7) == 3 && !(g_10058618[s1_cls0(c)] & 8))
        return 2;
    if (c != ' ' && !(g_10058618[s1_cls0(c)] & 8))
        return 2;

    for (;;) {
        st->d14 = Engine_StageNext(self, st->d14);
        if (st->d14 == NULL)
            return -1;
        if (!Engine_TypeSelected(self, st->d14))
            continue;
        c = st->d14->value;
        if (c == ' ' || (g_10058618[s1_cls0(c)] & 8)) {
            Engine_NodeFree(self, mark, 1);
            mark = st->d14;
            continue;
        }
        if (g_10058618[s1_cls80(st->d14->value)] & 1) {
            Engine_NodeFree(self, mark, 1);
            return 0;
        }
        mark->value = '&';
        mark->flags = (mark->flags & ~4u) | 3u;
        return 1;
    }
}

/* The first fold stage 1 applies, before anything else: '@' becomes n, J
 * becomes X, V becomes B, W becomes U, X becomes x and Z becomes S; every
 * other letter is left alone.  Only 0x40 to 0x5f is the map -- what comes
 * before it in the image is unrelated data the phoneme alphabet never
 * reaches -- and the array is declared to the end of it so that the index
 * the original uses is in range. */
/* @0x100487e0 */
extern const uint8_t g_s1_fold[0x60];

static int32_t s1_cls100(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x100);
}

/*
 * One phoneme of stage 1, and what the letter before it turns it into.
 *
 * Stage1_WordMark runs first and says what follows: 0 the word is over, 1 a
 * node to drop, 2 another phoneme.  The control node is then folded through
 * g_s1_fold and dispatched on, and each arm knows one Spanish spelling rule:
 *
 *   B D G  are softened to b d g between vowels (s1_8834 over 1);
 *   C      is K, or S before E I Y, and CH eats the H;
 *   G      is X before E I, and GU eats the U before E I U;
 *   H      is dropped, taking the following vowel with it for hU- and giving
 *          the I of hI- a Y;
 *   L      LL becomes y, or Y at the start of a word;
 *   N      becomes M before B P V;
 *   Q      QU eats the U before I E;
 *   R      RR and word-initial R become the four-node trill r R r R, and an
 *          R between vowels becomes the tap;
 *   S      becomes Z before a voiced consonant;
 *   Y      is y between vowels and I elsewhere;
 *   x      (from X) becomes K S.
 *
 * The result is always 1 -- there is no failure -- and s1_8834 carries to the
 * next call what kind of phoneme this was: 1 a nasal, 2 a liquid, 3 anything
 * else, 0 the end of a word.
 */
/* @0x1000fca0 */
uint8_t TV_THISCALL Stage1_Phoneme(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    int32_t r = Stage1_WordMark(self);
    int32_t next_kind = 3;
    Node *ctl, *scan_node;
    uint8_t c, nb;

    if (r < 0)
        return 0;

    st->scan = st->d14;
    ctl = st->ctl;
    ctl->value = g_s1_fold[s1_cls0(ctl->value)];
    scan_node = st->scan;
    nb = scan_node->value;
    ctl = st->ctl;
    c = ctl->value;

    switch (c) {
    case 'B':
        if (self->s1_8834 > 1) {
            uint8_t n = ctl->next->value;

            if (n != 'S' && n != 'T' && n != 'J')
                ctl->value = 'b';
        }
        break;

    case 'C':
        if (r != 2) {
            ctl->value = 'K';
        } else if (nb == 'E' || nb == 'I' || nb == 'Y') {
            ctl->value = 'S';
        } else if (nb != 'H') {
            ctl->value = 'K';
        } else {
            int32_t r2 = Stage1_WordMark(self);

            if (r2 < 0) {
                st->scan = st->ctl;
                return 1;
            }
            r = r2;
            Engine_NodeFree(self, st->scan, 1);
            st->scan = st->d14;
        }
        break;

    case 'D':
        if (self->s1_8834 > 1)
            ctl->value = 'd';
        break;

    case 'G':
        if (r == 2) {
            if (nb == 'E' || nb == 'I') {
                ctl->value = 'X';
                break;
            }
            if (nb == 'U' && !(scan_node->flags & 0x40u)) {
                int32_t r2 = Stage1_WordMark(self);

                if (r2 < 0) {
                    st->scan = st->ctl;
                    return 1;
                }
                if (r2 == 2) {
                    uint8_t d = st->d14->value;

                    if (d == 'U' || d == 'I' || d == 'E') {
                        r = r2;
                        Engine_NodeFree(self, st->scan, 1);
                        st->scan = st->d14;
                    }
                }
            }
        }
        if (self->s1_8834 > 1)
            st->ctl->value = 'g';
        break;

    case 'H':
        if (r == 0) {
            Node *n;

            Engine_NodeFree(self, ctl, 1);
            st->scan->flags = (st->scan->flags & ~4u) | 3u;
            n = Engine_StageNext(self, st->scan);
            st->ctl = n;
            st->cur = n;
            st->scan = n;
            self->s1_8834 = 0;
            return 1;
        }
        if (r == 1) {
            if (st->cur == ctl)
                st->cur = Engine_StageNext(self, st->cur);
            st->ctl = Engine_NodeFree(self, st->ctl, 1);
            return 1;
        }
        if (Stage1_WordMark(self) < 0) {
            st->scan = st->ctl;
            return 1;
        }
        if (r == 2 && nb == 'U') {
            uint8_t d = st->d14->value;
            int hit = d == 'E' || d == 'A' || d == 'O';

            if (!hit && d == 'I' &&
                (g_10058618[s1_cls0(st->ctl->prev->value)] & 0x80))
                hit = 1;
            if (hit) {
                st->ctl->value = 'g';
                break;
            }
        }
        if (r == 2 && nb == 'I' && !(st->scan->flags & 0x18u)) {
            uint8_t d = st->d14->value;

            if (d == 'A' || d == 'E' || d == 'O')
                st->scan->value = 'Y';
        }
        if (st->ctl == st->cur)
            st->cur = Engine_StageNext(self, st->cur);
        st->ctl = Engine_NodeFree(self, st->ctl, 1);
        return 1;

    case 'L':
        if (r == 2 && nb == 'L') {
            int32_t r2 = Stage1_WordMark(self);

            if (r2 < 0) {
                st->scan = st->ctl;
                return 1;
            }
            r = r2;
            Engine_NodeFree(self, st->scan, 1);
            st->scan = st->d14;
            if (self->s1_8834 == 3 || self->s1_8834 == 0)
                st->ctl->value = 'y';
            else
                st->ctl->value = 'Y';
        }
        next_kind = 2;
        break;

    case 'M':
    case 'n':
        next_kind = 1;
        break;

    case 'N':
        if (nb == 'B' || nb == 'P' || nb == 'V')
            ctl->value = 'M';
        next_kind = 1;
        break;

    case 'Q':
        if (r == 2 && nb == 'U') {
            int32_t r2 = Stage1_WordMark(self);

            if (r2 < 0) {
                st->scan = st->ctl;
                return 1;
            }
            if (r2 == 2) {
                uint8_t d = st->d14->value;

                if (d == 'I' || d == 'E') {
                    r = r2;
                    Engine_NodeFree(self, st->scan, 1);
                    st->scan = st->d14;
                }
            }
        }
        st->ctl->value = 'K';
        break;

    case 'R':
        if (nb == 'R') {
            r = Stage1_WordMark(self);
            if (r < 0) {
                st->scan = st->ctl;
                return 1;
            }
            Engine_NodeFree(self, st->scan, 1);
            st->scan = st->d14;
        } else if (self->s1_8834 != 0 && st->cur != ctl &&
                   Engine_StagePrev(self, ctl)->value != '&') {
            uint8_t p = st->cur->value;

            if (p != 'L' && p != 'N' && p != 'S' && p != 'Z') {
                if (r != 2)
                    st->ctl->value = 'p';
                else if (g_10058618[s1_cls100(nb)] & 2)
                    st->ctl->value = 'r';
                else
                    st->ctl->value = 'p';
            }
        }
        {
            Node *n = st->ctl;

            c = n->value;
            if (c == 'R') {
                n->value = 'r';
                Engine_NodeAlloc(self, st->ctl, 0, 3, 'r');
                Engine_NodeAlloc(self, st->ctl, 0, 3, 'R');
                Engine_NodeAlloc(self, st->ctl, 0, 3, 'r');
                Engine_NodeAlloc(self, st->ctl, 0, 3, 'R');
                break;
            }
            if (c != 'p')
                break;
            n->value = 'r';
            n = st->ctl;
            if (g_10058618[s1_cls100(n->prev->value)] & 2) {
                Node *nx = n->next;

                if (g_10058618[s1_cls0(nx->value)] & 8) {
                    Node *n2 = nx->next;
                    uint8_t d = n2->value;

                    if ((g_10058618[s1_cls100(d)] & 2) || d == 'H')
                        break;
                    if (d == 'Y') {
                        uint8_t e = n2->next->value;

                        if ((g_10058618[s1_cls0(e)] & 8) || e == ' ')
                            break;
                    }
                }
            }
            Engine_NodeAlloc(self, n, 0, 3, 'r');
            Engine_NodeAlloc(self, st->ctl, 0, 3, 'R');
            Engine_NodeAlloc(self, st->ctl, 0, 3, 'R');
        }
        break;

    case 'S':
        if ((g_10058618[s1_cls0(st->cur->value)] & 4) &&
            (g_10058618[s1_cls0(nb)] & 4) &&
            (g_10058618[s1_cls80(nb)] & 0x20) &&
            ctl->next->value != '&')
            ctl->value = 'Z';
        break;

    case 'Y':
        ctl->value = (r == 2) ? 'y' : 'I';
        break;

    case 'x':
        if (self->s1_8834 != 0 && st->cur != ctl &&
            Engine_StagePrev(self, ctl)->value != '&')
            st->cur = Engine_NodeAlloc(self, st->ctl, 0, 3, 'K');
        st->ctl->value = 'S';
        break;

    default:
        break;
    }

    /* the tail: publish the node, step the window on, and say what kind of
     * phoneme this was */
    {
        Node *n = st->ctl;

        n->flags = (n->flags & ~4u) | 3u;
        st->cur = n;
        st->ctl = Engine_StageNext(self, n);
    }
    if (r == 0) {
        Node *n = st->scan;

        if ((n->flags & 7) == 2)
            n->flags = (n->flags & ~4u) | 3u;
        st->ctl = st->scan;
        self->s1_8834 = 0;
        st->cur = st->scan;
        return 1;
    }
    self->s1_8834 = next_kind;
    return 1;
}

/* The exception lexicon: pairs of stored addresses, the spelling and the
 * phonemes to say instead, sorted on the spelling.  Both the array and its
 * count live in .bss and are filled at load time, and a lock guards them
 * because the host may add to them while the engine is running.
 *
 * An entry is two tv_refs and not two pointers.  The engine hands tv_bsearch a
 * width of 8 because 8 is what an entry takes in the image, and two real
 * pointers would be sixteen bytes on a 64-bit host -- the same reason every
 * other stored address in es/ is a tv_ref.
 *
 * 9996 is the space between the table and the count that follows it: 0x9c40
 * bytes, which is 4998 entries exactly.  The size is declared because a build
 * with no DLL has nowhere else to get it from. */
typedef struct { tv_ref key, value; } LexEntry;
/* @0x1002e000 */
extern const tv_ref g_lex_table[9996];
/* @0x10037c40 */
extern int32_t g_lex_count;
/* @0x10037c68 */
extern uint8_t g_lex_cs[24];

/* The DLL's own bsearch, so that which of several equal elements comes back
 * is the original's answer and not the host runtime's. */
/* @0x1002345a */
extern void *TV_CDECL tv_bsearch(const void *key, const void *base, size_t n,
                                 size_t width,
                                 int (TV_CDECL *cmp)(const void *,
                                                     const void *));

/* What the search is given, rather than what it searches: see lex_cmp. */
typedef struct { const char *key; } LexProbe;

/*
 * The lexicon is shared between engine instances, so lookups are serialised --
 * in the DLL, where the loader initialised the critical section at 0x10037c68
 * and every engine in the process shared one table.  A standalone build has no
 * one to have initialised it and no other instance to race with, so the lock is
 * nothing there; src/engine/lexicon.c does the same for English.
 */
#if defined(TV_HOOK_BUILD)
void TV_STDCALL EnterCriticalSection(void *cs);
void TV_STDCALL LeaveCriticalSection(void *cs);
#define Lexicon_Lock()    EnterCriticalSection(g_lex_cs)
#define Lexicon_Unlock()  LeaveCriticalSection(g_lex_cs)
#else
#define Lexicon_Lock()    ((void)0)
#define Lexicon_Unlock()  ((void)0)
#endif

/*: the comparison the lexicon is sorted by: the two elements' first member,
 * as strings, byte by byte unsigned.  The original unrolls it two bytes at a
 * time; the answer is the same.
 *
 * The two sides are not the same kind of thing.  bsearch passes the key it was
 * given first and a table element second, and the key here is a probe this file
 * built on its own stack, so its spelling is a plain pointer while the table's
 * is a stored address.  Making the probe a stored address instead would mean
 * turning a stack pointer into a 32-bit offset, which does not survive 64 bits.
 */
static int TV_CDECL lex_cmp(const void *a, const void *b)
{
    const uint8_t *p = (const uint8_t *)((const LexProbe *)a)->key;
    const uint8_t *q = (const uint8_t *)TV_REF(char, *(const tv_ref *)b);

    while (*p == *q) {
        if (*p == 0)
            return 0;
        p++;
        q++;
    }
    return *p < *q ? -1 : 1;
}

/*
 * The word between stage 1's two cursors, looked up in the exception lexicon.
 *
 * The characters from d14 up to d18 are collected into a key -- forty at most,
 * and more than that gives up -- and looked up.  A miss changes nothing and
 * returns 0.  A hit replaces the whole run of nodes with one node per
 * character of the replacement, and the digits in it are stress marks rather
 * than phonemes: a '1' after a character sets that node's level from s1_322
 * and s1_323, a '2' sets level 1, and a '0' sets none.  If the replacement
 * carries no '1' at all, the first vowel in it gets the level instead.
 *
 * s1_338 and s1_33c follow d14 and d18 when they were already pointing at
 * them, so that a caller holding either keeps hold of the new nodes.
 */
/* @0x10001040 */
uint8_t TV_THISCALL Stage1_Lexicon(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    LexProbe probe;
    char repl[0x2c];
    char key[0x29];
    const tv_ref *found;
    Node *end, *at, *last, *vowel = NULL;
    int32_t flag_ctl = 0, flag_cur = 0;
    int32_t n = 0, i, len;
    uint8_t seen1 = 0;

    end = st->d18;
    if (self->s1_33c != end)
        flag_ctl = 1;
    at = st->d14;
    if (self->s1_338 != at)
        flag_cur = 1;

    if (end->next != at) {
        for (;;) {
            if (n >= 0x28)
                return 0;
            key[n++] = (char)at->value;
            at = at->next;
            if (end->next == at)
                break;
        }
    }

    probe.key = key;
    Lexicon_Lock();
    key[n] = 0;
    found = (const tv_ref *)tv_bsearch(&probe, g_lex_table,
                                       (size_t)g_lex_count, 8, lex_cmp);
    if (found != NULL)
        strcpy(repl, TV_REF(char, found[1]));
    Lexicon_Unlock();
    if (found == NULL)
        return 0;

    at = st->d14;
    if (st->d18 != at)
        do {
            at = Engine_NodeFree(self, at, 1);
        } while (st->d18 != at);

    last = Engine_NodeFree(self, at, 0);
    st->d14 = last;
    len = (int32_t)strlen(repl);
    for (i = 0; i < len; i++) {
        uint8_t c = (uint8_t)repl[i];
        uint8_t nx;

        if (c == '1' || c == '2' || c == '0')
            continue;
        last = Engine_NodeAlloc(self, last, 1, 3, c);
        nx = (uint8_t)repl[i + 1];
        if (nx == '1') {
            int32_t lv;

            seen1 = 1;
            if (self->s1_322 == 1)
                lv = 3;
            else
                lv = self->s1_323 == 1 ? 1 : 2;
            last->flags = (last->flags & ~0x18u) | ((uint32_t)lv << 3);
            i++;
        } else if (nx == '2') {
            last->flags = (last->flags & ~0x10u) | 8u;
            i++;
        } else if (nx == '0') {
            i++;
        } else if (seen1 != 1 &&
                   (g_10058618[s1_cls0(last->value)] & 1) && vowel == NULL) {
            vowel = last;
        }
    }

    at = st->d14;
    st->d18 = last;
    st->d14 = at->next;
    if (flag_cur == 0)
        self->s1_338 = at->next;
    if (flag_ctl == 0)
        self->s1_33c = last;

    if (seen1 == 1 || vowel == NULL)
        return 1;
    if (self->s1_322 == 1)
        vowel->flags |= 0x18u;
    else if (self->s1_323 == 1)
        vowel->flags = (vowel->flags & ~0x10u) | 8u;
    else
        vowel->flags = (vowel->flags & ~8u) | 0x10u;
    return 1;
}

/* The loanword lexicon's records, six bytes ahead of the keys lang/spa/engine/tables.c
 * reads: two bytes, the key's length as an int16, two more, then the key and
 * then the phonemes to say instead of it. */
/* @0x1006afb0 */
extern const char g_lex_rec[];
/* @0x10046088 */
extern int32_t g_lex_index[78];

/*
 * The word between stage 1's two cursors, looked up in the loanword lexicon.
 *
 * The same shape as Stage1_Lexicon and the same replacement rules; only where
 * it looks differs.  This one takes the 77 foreign words Lexicon_Find
 * searches, and the record it finds carries the key's length so that the
 * phonemes after it can be found.
 */
/* @0x10023250 */
uint8_t TV_THISCALL Stage1_Loanword(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    char key[0x2c];
    char repl[0x40];
    Node *end, *at, *last, *vowel = NULL;
    int32_t flag_ctl = 0, flag_cur = 0;
    int32_t n = 0, i, len, index = 0;
    uint8_t seen1 = 0;

    Lexicon_Init();

    end = st->d18;
    if (self->s1_33c != end)
        flag_ctl = 1;
    at = st->d14;
    if (self->s1_338 != at)
        flag_cur = 1;

    if (end->next != at) {
        for (;;) {
            if (n >= 0x28)
                return 0;
            key[n++] = (char)at->value;
            at = at->next;
            if (end->next == at)
                break;
        }
    }
    key[n] = 0;
    if (Lexicon_Find(key, &index) == 0)
        return 0;

    {
        const char *rec = g_lex_rec + g_lex_index[index];
        int32_t klen = (int32_t)*(const int16_t *)(rec + 2);

        strcpy(repl, rec + klen + 7);
    }

    at = st->d14;
    if (st->d18 != at)
        do {
            at = Engine_NodeFree(self, at, 1);
        } while (st->d18 != at);

    last = Engine_NodeFree(self, at, 0);
    st->d14 = last;
    len = (int32_t)strlen(repl);
    for (i = 0; i < len; i++) {
        uint8_t c = (uint8_t)repl[i];
        uint8_t nx;

        if (c == '1' || c == '2' || c == '0')
            continue;
        last = Engine_NodeAlloc(self, last, 1, 3, c);
        nx = (uint8_t)repl[i + 1];
        if (nx == '1') {
            int32_t lv;

            seen1 = 1;
            if (self->s1_322 == 1)
                lv = 3;
            else
                lv = self->s1_323 == 1 ? 1 : 2;
            last->flags = (last->flags & ~0x18u) | ((uint32_t)lv << 3);
            i++;
        } else if (nx == '2') {
            last->flags = (last->flags & ~0x10u) | 8u;
            i++;
        } else if (nx == '0') {
            i++;
        } else if (seen1 != 1 &&
                   (g_10058618[s1_cls0(last->value)] & 1) && vowel == NULL) {
            vowel = last;
        }
    }

    at = st->d14;
    st->d18 = last;
    st->d14 = at->next;
    if (flag_cur == 0)
        self->s1_338 = at->next;
    if (flag_ctl == 0)
        self->s1_33c = last;
    return 1;
}

/*
 * Stage 1, one step at a time.
 *
 * The first thing it does is split a phoneme that is both a word and a
 * boundary: the node becomes a '&' and a copy of it goes in after, carrying
 * its length and level.  Then it walks the window forward, and what it does
 * with each node depends on the class of its value: a node marked 1 in the
 * 0x80 block is published as a phoneme and stepped over, a type 3 node marked
 * 0x80 is stepped over untouched, a node marked 8 is published and both
 * cursors move on, and anything else is removed and the step ends there.
 *
 * A type 2 node whose value is '@' to 'Z' is a word: the two lexicons get
 * first refusal at it, and if neither claims it Stage1_Phoneme is run over it
 * until the control node stops being one.
 */
/* @0x1000fa30 */
uint8_t TV_THISCALL Stage1_Run(Engine *self)
{
    StageCtx *st = &self->stage_ctx[1];
    Node *n;

    if (!Engine_StageBegin(self, st))
        return Engine_StageEnd(self);

    n = st->scan;
    if (st->cur == n && (n->flags & 7) == 3) {
        uint8_t c = n->value;
        uint8_t f = g_10058618[s1_cls0(c)];

        if (!(f & 8) && (f & 0x80)) {
            uint32_t arg = n->arg;
            uint8_t b15 = n->b15;

            n->value = '&';
            Engine_NodeAlloc(self, st->scan, 1, 3, c);
            Engine_StageNext(self, st->scan)->arg = arg;
            Engine_StageNext(self, st->scan)->b15 = b15;
        }
    }

    for (;;) {
        Node *ctl = st->ctl;
        uint8_t c;
        uint32_t flags;

        if (ctl == NULL)
            break;
        /* the original compares the value as a signed byte */
        if ((ctl->flags & 7) == 2 && (int8_t)ctl->value >= '@' &&
            (int8_t)ctl->value <= 'Z')
            break;
        if (!Engine_TypeSelected(self, ctl))
            return Engine_StageEnd(self);

        ctl = st->ctl;
        c = ctl->value;
        if (g_10058618[s1_cls80(c)] & 1) {
            ctl->flags = (ctl->flags & ~4u) | 3u;
            n = Engine_StageNext(self, st->ctl);
            st->ctl = n;
            st->cur = n;
        } else {
            flags = ctl->flags;
            if ((flags & 7) == 3 && (g_10058618[s1_cls0(c)] & 0x80)) {
                n = Engine_StageNext(self, ctl);
                st->ctl = n;
                st->cur = n;
                self->s1_8834 = 0;
            } else if (!(g_10058618[s1_cls0(c)] & 8)) {
                n = Engine_NodeFree(self, st->ctl, 1);
                st->ctl = n;
                st->cur = n;
                Engine_StageEnd(self);
                return 1;
            } else {
                ctl->flags = (flags & ~4u) | 3u;
                if (st->cur == st->ctl)
                    st->cur = Engine_StageNext(self, st->cur);
                st->ctl = Engine_StageNext(self, st->ctl);
                self->s1_8834 = 0;
            }
        }
        if (st->ctl == NULL)
            return Engine_StageEnd(self);
    }

    n = st->ctl;
    if (n == NULL)
        return Engine_StageEnd(self);
    st->scan = n;
    st->d14 = n;
    st->d18 = st->last;
    {
        /* both run, in this order, and either claiming the word is enough */
        uint8_t a = Stage1_Loanword(self);
        uint8_t b = Stage1_Lexicon(self);

        if ((a | b) != 0)
            return 1;
    }

    do {
        Node *before = st->ctl;

        Stage1_Phoneme(self);
        n = st->ctl;
        if (before == n || n == NULL || st->last == n)
            break;
    } while ((int8_t)n->value >= '@' && (int8_t)n->value <= 'Z');
    return Engine_StageEnd(self);
}
