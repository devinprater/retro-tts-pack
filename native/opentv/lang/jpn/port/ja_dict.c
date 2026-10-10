/*
 * The dictionary, and the Viterbi over it.
 *
 * Translated from build/Japanese_test/jp_dict.py.  lang/jpn/data/jadic.bin is
 * naist-jdic compiled by tools/gen_ja_dict.py: for each of 486,757 entries a
 * surface, a reading, the reading as written, an accent type, a mora count, a
 * part-of-speech tuple and an accent chain rule, plus the 1377x1377 connection
 * matrix a morphological analyser needs and the unknown-word tables from
 * char.def and unk.def.
 *
 * The file is read into one buffer and nothing is copied out of it.  The
 * string pools are NUL-separated, so a surface is just a pointer into the
 * buffer; the arena below exists only for the handful of strings that are
 * genuinely built.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ja_njd.h"
#include "ja_ojt.h"

/* ---- the arena ---------------------------------------------------------- */

#define JA_ARENA_BLK 8192

struct ja_arena_blk {
    ja_arena_blk *next;
    size_t        used, cap;
    char          mem[1];
};

void *ja_arena_alloc(ja_arena *a, size_t n)
{
    ja_arena_blk *b;
    size_t want;
    void *p;

    n = (n + 7u) & ~(size_t)7u;
    for (b = a->head; b != NULL; b = b->next)
        if (b->cap - b->used >= n)
            break;
    if (b == NULL) {
        want = n > JA_ARENA_BLK ? n : JA_ARENA_BLK;
        b = (ja_arena_blk *)malloc(sizeof(ja_arena_blk) + want);
        if (b == NULL)
            return NULL;
        b->next = a->head;
        b->used = 0;
        b->cap = want;
        a->head = b;
    }
    p = b->mem + b->used;
    b->used += n;
    return p;
}

void ja_arena_free(ja_arena *a)
{
    ja_arena_blk *b = a->head, *nx;
    while (b != NULL) {
        nx = b->next;
        free(b);
        b = nx;
    }
    a->head = NULL;
}

char *ja_arena_strn(ja_arena *a, const char *s, size_t n)
{
    char *p = (char *)ja_arena_alloc(a, n + 1);
    if (p == NULL)
        return NULL;
    if (n != 0)
        memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *ja_arena_str(ja_arena *a, const char *s)
{
    return ja_arena_strn(a, s, strlen(s));
}

char *ja_arena_cat(ja_arena *a, const char *s, const char *t)
{
    size_t ls = strlen(s), lt = strlen(t);
    char *p = (char *)ja_arena_alloc(a, ls + lt + 1);
    if (p == NULL)
        return NULL;
    memcpy(p, s, ls);
    memcpy(p + ls, t, lt + 1);
    return p;
}

/* ---- the word list ------------------------------------------------------ */

void ja_word_reset(ja_word *w)
{
    int i;
    w->string = "*";
    for (i = 0; i < JA_POS_N; i++)
        w->pos[i] = "*";
    w->pron = NULL;
    w->read = "*";
    w->acc = 0;
    w->mora_size = 0;
    w->chain_rule = "*";
    w->chain_flag = -1;
}

static int vec_room(ja_vec *v, int n)
{
    ja_word *p;
    int cap;
    if (v->n + n <= v->cap)
        return 0;
    cap = v->cap != 0 ? v->cap * 2 : 32;
    while (cap < v->n + n)
        cap *= 2;
    p = (ja_word *)realloc(v->w, (size_t)cap * sizeof *p);
    if (p == NULL)
        return -1;
    v->w = p;
    v->cap = cap;
    return 0;
}

ja_word *ja_vec_push(ja_vec *v)
{
    if (vec_room(v, 1) != 0)
        return NULL;
    ja_word_reset(&v->w[v->n]);
    return &v->w[v->n++];
}

int ja_vec_add(ja_vec *v, const ja_word *w)
{
    if (vec_room(v, 1) != 0)
        return -1;
    v->w[v->n++] = *w;
    return 0;
}

/*
 * Upstream's NJD_remove_silent_node: a stage says a word is not spoken by
 * clearing its pronunciation, and the node goes.  Several passes depend on
 * this having happened before the next one runs.
 */
void ja_vec_compact(ja_vec *v)
{
    int i, k = 0;
    for (i = 0; i < v->n; i++)
        if (v->w[i].pron != NULL)
            v->w[k++] = v->w[i];
    v->n = k;
}

void ja_vec_free(ja_vec *v)
{
    free(v->w);
    v->w = NULL;
    v->n = v->cap = 0;
}

/* ---- reading the file --------------------------------------------------- */

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int rd16(const unsigned char *p)
{
    return (int)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

static int rds16(const unsigned char *p)
{
    int v = rd16(p);
    return v >= 0x8000 ? v - 0x10000 : v;
}

const char *ja_dict_error(int code)
{
    switch (code) {
    case 0:  return "ok";
    case -1: return "the dictionary file could not be read";
    case -2: return "that file is not a compiled Japanese dictionary";
    case -3: return "the dictionary is the wrong format version; re-run "
                    "tools/gen_ja_dict.py";
    default: return "out of memory reading the dictionary";
    }
}

int ja_dict_open(ja_dict *d, const char *path)
{
    FILE *fp;
    long n;
    size_t got;
    const unsigned char *h;

    memset(d, 0, sizeof *d);
    fp = fopen(path, "rb");
    if (fp == NULL)
        return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    n = ftell(fp);
    if (n < 8 + 25 * 4) {
        fclose(fp);
        return -2;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {    /* not rewind(): the 32-bit build
                                           * links a minimal msvcrt import
                                           * list and does not have it */
        fclose(fp);
        return -1;
    }
    d->b = (unsigned char *)malloc((size_t)n);
    if (d->b == NULL) {
        fclose(fp);
        return -4;
    }
    got = fread(d->b, 1, (size_t)n, fp);
    fclose(fp);
    if (got != (size_t)n) {
        free(d->b);
        d->b = NULL;
        return -1;
    }
    d->len = (size_t)n;
    if (memcmp(d->b, JA_DICT_MAGIC, 8) != 0) {
        ja_dict_close(d);
        return -2;
    }
    h = d->b + 8;
    d->version    = rd32(h +  0 * 4);
    d->n_entries  = rd32(h +  1 * 4);
    d->n_pos      = rd32(h +  2 * 4);
    d->n_chain    = rd32(h +  3 * 4);
    d->n_cat      = rd32(h +  4 * 4);
    d->n_unk      = rd32(h +  5 * 4);
    d->dim        = rd32(h +  6 * 4);
    d->n_comp     = rd32(h +  7 * 4);
    d->off_surf   = rd32(h +  8 * 4);
    d->len_surf   = rd32(h +  9 * 4);
    d->off_pron   = rd32(h + 10 * 4);
    d->len_pron   = rd32(h + 11 * 4);
    d->off_posp   = rd32(h + 12 * 4);
    d->len_posp   = rd32(h + 13 * 4);
    d->off_chainp = rd32(h + 14 * 4);
    d->len_chainp = rd32(h + 15 * 4);
    d->off_posi   = rd32(h + 16 * 4);
    d->off_chaini = rd32(h + 17 * 4);
    d->off_entry  = rd32(h + 18 * 4);
    d->off_matrix = rd32(h + 19 * 4);
    d->off_cat    = rd32(h + 20 * 4);
    d->off_cmap   = rd32(h + 21 * 4);
    d->off_unk    = rd32(h + 22 * 4);
    d->off_comp   = rd32(h + 23 * 4);
    if (d->version != JA_DICT_VERSION) {
        ja_dict_close(d);
        return -3;
    }
    /* A truncated file would otherwise read past the end on the first
     * lookup, which is a crash a long way from its cause. */
    if ((size_t)d->off_entry + (size_t)d->n_entries * JA_ENTRY_SIZE > d->len
        || (size_t)d->off_matrix + (size_t)d->dim * d->dim * 2 > d->len
        || (size_t)d->off_cmap + 0x10000 > d->len
        || (size_t)d->off_unk + (size_t)d->n_unk * JA_UNK_SIZE > d->len) {
        ja_dict_close(d);
        return -2;
    }
    return 0;
}

void ja_dict_close(ja_dict *d)
{
    free(d->b);
    d->b = NULL;
    d->len = 0;
}

/* ---- accessors ---------------------------------------------------------- */

static const char *pool(const ja_dict *d, uint32_t base, uint32_t off)
{
    return (const char *)(d->b + base + off);
}

static const unsigned char *entry(const ja_dict *d, uint32_t i)
{
    return d->b + d->off_entry + (size_t)i * JA_ENTRY_SIZE;
}

/* An entry: surface u32, pron u32, read u32, left u16, right u16, cost i16,
 * pos u16, accent u8, morae u8, chain u8, components u8, component off u32. */
#define E_SURF(e)  rd32((e) + 0)
#define E_PRON(e)  rd32((e) + 4)
#define E_READ(e)  rd32((e) + 8)
#define E_LEFT(e)  rd16((e) + 12)
#define E_RIGHT(e) rd16((e) + 14)
#define E_COST(e)  rds16((e) + 16)
#define E_POS(e)   rd16((e) + 18)
#define E_ACC(e)   ((int)(e)[20])
#define E_MORA(e)  ((int)(e)[21])
#define E_CHAIN(e) ((int)(e)[22])
#define E_NCOMP(e) ((int)(e)[23])
#define E_COMP(e)  rd32((e) + 24)

static void dict_pos(const ja_dict *d, uint32_t pid, const char **out)
{
    uint32_t off = rd32(d->b + d->off_posi + (size_t)pid * 4);
    const char *at = pool(d, d->off_posp, off);
    int i;
    for (i = 0; i < JA_POS_N; i++) {
        out[i] = at;
        at += strlen(at) + 1;
    }
}

static const char *dict_chain(const ja_dict *d, uint32_t cid)
{
    uint32_t off = rd32(d->b + d->off_chaini + (size_t)cid * 4);
    return pool(d, d->off_chainp, off);
}

static int dict_connect(const ja_dict *d, int right_id, int left_id)
{
    size_t at = (size_t)d->off_matrix
        + (((size_t)right_id * d->dim) + (size_t)left_id) * 2;
    return rds16(d->b + at);
}

static int dict_category(const ja_dict *d, unsigned long cp)
{
    if (cp > 0xffffu)
        cp = 0x4e00u;                   /* outside the map, treat as kanji */
    return d->b[d->off_cmap + cp];
}

static void dict_cat_flags(const ja_dict *d, int c,
                           int *invoke, int *group, int *length)
{
    const unsigned char *at = d->b + d->off_cat + (size_t)c * 4;
    *invoke = at[0];
    *group = at[1];
    *length = at[2];
}

/*
 * Bytes compare as Python's bytes do: byte by byte unsigned, and the shorter
 * string is the smaller one.  The key is not NUL-terminated, which is why
 * this is not strncmp.
 */
static int cmp_key(const char *s, const char *key, size_t klen)
{
    const unsigned char *a = (const unsigned char *)s;
    const unsigned char *b = (const unsigned char *)key;
    size_t i;
    for (i = 0; i < klen; i++) {
        if (a[i] == 0)
            return -1;
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    }
    return a[klen] == 0 ? 0 : 1;
}

/*
 * The entries whose surface equals `key`, as a half-open range.  Entries are
 * sorted by surface bytes then by cost, so this is one binary search for the
 * start of the run and another for its end.  Returns 0 when nothing matches.
 */
static int dict_find(const ja_dict *d, const char *key, size_t klen,
                     uint32_t *first, uint32_t *last)
{
    uint32_t lo = 0, hi = d->n_entries, mid;
    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        if (cmp_key(pool(d, d->off_surf, E_SURF(entry(d, mid))), key, klen) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= d->n_entries
        || cmp_key(pool(d, d->off_surf, E_SURF(entry(d, lo))), key, klen) != 0)
        return 0;
    *first = lo;
    hi = d->n_entries;
    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        if (cmp_key(pool(d, d->off_surf, E_SURF(entry(d, mid))), key, klen) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    *last = lo;
    return 1;
}

/* ---- text2mecab ---------------------------------------------------------- */

/*
 * THE DIRECTION IS ASCII TO FULLWIDTH.  naist-jdic is keyed by the fullwidth
 * forms -- `１` is an entry reading イチ and `1` is not in the dictionary at
 * all -- so folding the other way makes every digit and Latin letter miss the
 * dictionary and come out as a pause.  The table is longest-source-first, so
 * the first match is the longest: a halfwidth kana plus its voicing mark is
 * one substitution and not the bare kana.
 */
size_t ja_normalize(const char *text, char *out, size_t cap)
{
    size_t i = 0, w = 0, n = strlen(text);
    int k;
    while (i < n) {
        for (k = 0; k < ja_CONV_N; k++) {
            size_t ls = strlen(ja_CONV[k].src);
            if (ls <= n - i && memcmp(text + i, ja_CONV[k].src, ls) == 0) {
                size_t ld = strlen(ja_CONV[k].dst);
                if (w + ld < cap)
                    memcpy(out + w, ja_CONV[k].dst, ld);
                w += ld;
                i += ls;
                break;
            }
        }
        if (k == ja_CONV_N) {
            if (w + 1 < cap)
                out[w] = text[i];
            w++;
            i++;
        }
    }
    if (cap != 0)
        out[w < cap ? w : cap - 1] = '\0';
    return w;
}

/* ---- a reading for an unknown word -------------------------------------- */

char *ja_unknown_pron(ja_arena *a, const char *s, int *morae)
{
    size_t i = 0, w = 0, n = strlen(s);
    int k, m = 0;
    char *out;
    /* One pass to size it, one to fill it.  The readings are longer than the
     * surfaces they come from -- ＹＴＬ is 9 bytes and ワイティーエル is 18 --
     * so the length cannot be guessed from the input. */
    while (i < n) {
        for (k = 0; k < ja_PRON_LIST_N; k++) {
            size_t ls = strlen(ja_PRON_LIST[k].s);
            if (ls <= n - i && memcmp(s + i, ja_PRON_LIST[k].s, ls) == 0) {
                w += strlen(ja_PRON_LIST[k].read);
                m += ja_PRON_LIST[k].morae;
                i += ls;
                break;
            }
        }
        if (k == ja_PRON_LIST_N)
            i++;                /* nothing matches: skip the character */
    }
    out = (char *)ja_arena_alloc(a, w + 1);
    if (out == NULL)
        return NULL;
    i = 0;
    w = 0;
    while (i < n) {
        for (k = 0; k < ja_PRON_LIST_N; k++) {
            size_t ls = strlen(ja_PRON_LIST[k].s);
            if (ls <= n - i && memcmp(s + i, ja_PRON_LIST[k].s, ls) == 0) {
                size_t lr = strlen(ja_PRON_LIST[k].read);
                memcpy(out + w, ja_PRON_LIST[k].read, lr);
                w += lr;
                i += ls;
                break;
            }
        }
        if (k == ja_PRON_LIST_N)
            i++;
    }
    out[w] = '\0';
    if (morae != NULL)
        *morae = m;
    return out;
}

/* ---- the lattice -------------------------------------------------------- */

typedef struct {
    int      begin, end;
    int      left, right, wcost;
    int32_t  entry;             /* -1 for an unknown word */
    int      unk_pos;           /* part-of-speech id, when entry is -1 */
    long     cost;
    int      prev;              /* node index, -1 for none */
} lnode;

typedef struct {
    lnode *v;
    int    n, cap;
} lvec;

static int lpush(lvec *L, const lnode *nd)
{
    if (L->n == L->cap) {
        int cap = L->cap != 0 ? L->cap * 2 : 64;
        lnode *p = (lnode *)realloc(L->v, (size_t)cap * sizeof *p);
        if (p == NULL)
            return -1;
        L->v = p;
        L->cap = cap;
    }
    L->v[L->n] = *nd;
    return L->n++;
}

/*
 * `ends[p]` is a list of the nodes that finish at byte p, and a node can be
 * in more than one of those lists: where nothing in the dictionary can start
 * at a position, every path reaching it is carried through to the next
 * character, which is the Python's `ends[nxt].extend(ends[p])`.  So the lists
 * are cons cells over node indices rather than a link field inside the node.
 *
 * THE ORDER IS LOAD-BEARING.  Two entries for the same surface can reach the
 * same position at the same cost -- 滝 is both タキ and ダキ, 東土古 both
 * ...ンコ and ...ンゴ -- and the winner is then whichever the scan sees first,
 * because the comparison is a strict `<`.  Appending, as the Python's list
 * does, is therefore not interchangeable with prepending: prepending reversed
 * every tie and changed the reading of 77 of the oracle's 1,900 texts.  Hence
 * the tail index.
 */
typedef struct {
    int node, next;
} lcell;

typedef struct {
    lcell *v;
    int    n, cap;
} cvec;

static int cell(cvec *C, int node)
{
    if (C->n == C->cap) {
        int cap = C->cap != 0 ? C->cap * 2 : 64;
        lcell *p = (lcell *)realloc(C->v, (size_t)cap * sizeof *p);
        if (p == NULL)
            return -1;
        C->v = p;
        C->cap = cap;
    }
    C->v[C->n].node = node;
    C->v[C->n].next = -1;
    return C->n++;
}

/* Append to the list whose head is *head and whose last cell is *tail. */
static int cappend(cvec *C, int *head, int *tail, int node)
{
    int id = cell(C, node);
    if (id < 0)
        return -1;
    if (*head < 0)
        *head = id;
    else
        C->v[*tail].next = id;
    *tail = id;
    return 0;
}

static unsigned long cp_at(const unsigned char *b, size_t n, size_t i)
{
    unsigned c = b[i];
    int k, len;
    unsigned long u;
    if (c < 0x80u)
        return c;
    len = c >= 0xf0u ? 3 : c >= 0xe0u ? 2 : 1;
    u = c & (len == 3 ? 0x07u : len == 2 ? 0x0fu : 0x1fu);
    for (k = 1; k <= len; k++) {
        if (i + (size_t)k >= n)
            return 0xfffdu;
        u = (u << 6) | (b[i + k] & 0x3fu);
    }
    return u;
}

/* The longest surface in naist-jdic is 78 bytes; see the Python. */
#define JA_LONGEST_SURFACE 78

/*
 * Viterbi.  The cost of a path is the sum of the word costs and the
 * connection costs between adjacent parts of speech, and the cheapest path is
 * the analysis.  Unknown words are proposed where char.def says to invoke
 * them or where nothing in the dictionary starts here, with the lengths
 * char.def's grouping and length columns allow.
 */
int ja_analyse(const ja_dict *d, ja_arena *a, const char *text, ja_vec *out)
{
    char *norm = NULL;
    size_t need, blen;
    int *starts = NULL, n_starts = 0;
    int *ends = NULL;            /* head cell index per byte position */
    int *tails = NULL;           /* and its last cell, so appends are O(1) */
    int *chain = NULL;
    lvec L;
    cvec C;
    int rc = -1, ip, i, k;
    const unsigned char *b;

    out->n = 0;
    memset(&L, 0, sizeof L);
    memset(&C, 0, sizeof C);

    need = ja_normalize(text, NULL, 0);
    norm = (char *)malloc(need + 1);
    if (norm == NULL)
        goto done;
    ja_normalize(text, norm, need + 1);
    blen = strlen(norm);
    if (blen == 0) {
        rc = 0;
        goto done;
    }
    b = (const unsigned char *)norm;

    /* byte offsets where a UTF-8 character begins, plus the end */
    starts = (int *)malloc((blen + 2) * sizeof *starts);
    ends = (int *)malloc((blen + 1) * sizeof *ends);
    tails = (int *)malloc((blen + 1) * sizeof *tails);
    if (starts == NULL || ends == NULL || tails == NULL)
        goto done;
    for (i = 0; i < (int)blen; i++)
        if ((b[i] & 0xc0u) != 0x80u)
            starts[n_starts++] = i;
    starts[n_starts++] = (int)blen;
    for (i = 0; i <= (int)blen; i++)
        ends[i] = tails[i] = -1;

    {   /* the start node, so position 0 is reachable */
        lnode bos;
        int id;
        memset(&bos, 0, sizeof bos);
        bos.entry = -1;
        bos.prev = -1;
        id = lpush(&L, &bos);
        if (id < 0)
            goto done;
        if (cappend(&C, &ends[0], &tails[0], id) != 0)
            goto done;
    }

    for (ip = 0; ip + 1 < n_starts; ip++) {
        int p = starts[ip];
        int n_cand = 0, first_cand = L.n;
        int cat, invoke, group, length;

        if (ends[p] < 0)
            continue;                   /* unreachable position */

        /* known words */
        for (k = ip + 1; k < n_starts; k++) {
            int q = starts[k];
            uint32_t lo, hi, e;
            if (!dict_find(d, norm + p, (size_t)(q - p), &lo, &hi)) {
                /* a longer key cannot match if no entry has this prefix, but
                 * proving that needs a trie, so the scan is bounded by the
                 * longest surface instead */
                if (q - p > JA_LONGEST_SURFACE)
                    break;
                continue;
            }
            for (e = lo; e < hi; e++) {
                const unsigned char *en = entry(d, e);
                lnode nd;
                memset(&nd, 0, sizeof nd);
                nd.begin = p;
                nd.end = q;
                nd.left = E_LEFT(en);
                nd.right = E_RIGHT(en);
                nd.wcost = E_COST(en);
                nd.entry = (int32_t)e;
                nd.prev = -1;
                if (lpush(&L, &nd) < 0)
                    goto done;
                n_cand++;
            }
        }

        /* unknown words */
        cat = dict_category(d, cp_at(b, blen, (size_t)p));
        dict_cat_flags(d, cat, &invoke, &group, &length);
        if (invoke || n_cand == 0) {
            int kk = ip, run_end, lens[64], n_lens = 0, j;
            while (kk + 1 < n_starts) {
                int nxt = starts[kk + 1];
                if (nxt >= (int)blen
                    || dict_category(d, cp_at(b, blen, (size_t)nxt)) != cat)
                    break;
                kk++;
            }
            run_end = starts[kk + 1];
            if (group)
                lens[n_lens++] = run_end;
            for (j = 1; j <= length && n_lens < (int)(sizeof lens / sizeof *lens);
                 j++)
                if (ip + j < n_starts && starts[ip + j] <= run_end)
                    lens[n_lens++] = starts[ip + j];
            /* ascending and unique, as the Python's sorted(set(lens)) is */
            for (i = 0; i < n_lens; i++)
                for (j = i + 1; j < n_lens; j++)
                    if (lens[j] < lens[i]) {
                        int t = lens[i];
                        lens[i] = lens[j];
                        lens[j] = t;
                    }
            for (i = 0; i < n_lens; i++) {
                int q = lens[i], u;
                if (q <= p || (i > 0 && q == lens[i - 1]))
                    continue;
                for (u = 0; u < (int)d->n_unk; u++) {
                    const unsigned char *ue =
                        d->b + d->off_unk + (size_t)u * JA_UNK_SIZE;
                    lnode nd;
                    if (ue[8] != (unsigned char)cat)
                        continue;
                    memset(&nd, 0, sizeof nd);
                    nd.begin = p;
                    nd.end = q;
                    nd.left = rd16(ue + 0);
                    nd.right = rd16(ue + 2);
                    nd.wcost = rds16(ue + 4);
                    nd.entry = -1;
                    nd.unk_pos = rd16(ue + 6);
                    nd.prev = -1;
                    if (lpush(&L, &nd) < 0)
                        goto done;
                    n_cand++;
                }
            }
        }

        if (n_cand == 0) {
            /* nothing at all can start here: carry every path reaching it
             * through to the next character, so the rest of the utterance is
             * still analysed rather than lost */
            int nxt = starts[ip + 1], cl;
            for (cl = ends[p]; cl >= 0; cl = C.v[cl].next)
                if (cappend(&C, &ends[nxt], &tails[nxt], C.v[cl].node) != 0)
                    goto done;
            continue;
        }

        for (i = first_cand; i < L.n; i++) {
            lnode *nd = &L.v[i];
            long best = 0;
            int bprev = -1, cl;
            for (cl = ends[p]; cl >= 0; cl = C.v[cl].next) {
                int pv = C.v[cl].node;
                long c = L.v[pv].cost
                    + dict_connect(d, L.v[pv].right, nd->left);
                if (bprev < 0 || c < best) {
                    best = c;
                    bprev = pv;
                }
            }
            nd->cost = best + nd->wcost;
            nd->prev = bprev;
            if (cappend(&C, &ends[nd->end], &tails[nd->end], i) != 0)
                goto done;
        }
    }

    /* end of sentence */
    {
        long best = 0;
        int bprev = -1, cl, n_chain = 0, nd;
        for (cl = ends[blen]; cl >= 0; cl = C.v[cl].next) {
            int pv = C.v[cl].node;
            long c = L.v[pv].cost + dict_connect(d, L.v[pv].right, 0);
            if (bprev < 0 || c < best) {
                best = c;
                bprev = pv;
            }
        }
        if (bprev < 0) {
            rc = 0;                     /* nothing reached the end */
            goto done;
        }
        for (nd = bprev; nd >= 0 && L.v[nd].end > 0; nd = L.v[nd].prev)
            n_chain++;
        chain = (int *)malloc((size_t)(n_chain > 0 ? n_chain : 1) * sizeof *chain);
        if (chain == NULL)
            goto done;
        i = 0;
        for (nd = bprev; nd >= 0 && L.v[nd].end > 0; nd = L.v[nd].prev)
            chain[i++] = nd;
        for (i = n_chain - 1; i >= 0; i--) {
            const lnode *nn = &L.v[chain[i]];
            ja_word *w = ja_vec_push(out);
            if (w == NULL)
                goto done;
            if (nn->entry >= 0) {
                const unsigned char *en = entry(d, (uint32_t)nn->entry);
                const char *pos[JA_POS_N];
                const char *rule = dict_chain(d, (uint32_t)E_CHAIN(en));
                int comp;
                dict_pos(d, E_POS(en), pos);
                w->string = pool(d, d->off_surf, E_SURF(en));
                memcpy(w->pos, pos, sizeof pos);
                w->pron = pool(d, d->off_pron, E_PRON(en));
                w->read = pool(d, d->off_pron, E_READ(en));
                w->acc = E_ACC(en);
                w->mora_size = E_MORA(en);
                w->chain_rule = rule;
                /*
                 * A compound entry is more than one word.  Upstream's
                 * NJDNode_load emits one node per component, sharing the part
                 * of speech and the chain rule, and forces the chain flag of
                 * every component after the first to 0 so it starts its own
                 * accent phrase: ありがとうございます is アリガトー then
                 * ゴザイマス, two phrases with accents 2 and 4, not one word
                 * with no accent.
                 */
                for (comp = 1; comp < E_NCOMP(en); comp++) {
                    const unsigned char *c = d->b + d->off_comp
                        + (size_t)(E_COMP(en) + comp - 1) * JA_COMP_SIZE;
                    ja_word *cw = ja_vec_push(out);
                    if (cw == NULL)
                        goto done;
                    cw->string = pool(d, d->off_surf, rd32(c + 4));
                    memcpy(cw->pos, pos, sizeof pos);
                    cw->pron = pool(d, d->off_pron, rd32(c + 0));
                    cw->read = pool(d, d->off_pron, rd32(c + 8));
                    cw->acc = c[12];
                    cw->mora_size = c[13];
                    cw->chain_rule = rule;
                    cw->chain_flag = 0;
                }
            } else {
                char *s = ja_arena_strn(a, norm + nn->begin,
                                        (size_t)(nn->end - nn->begin));
                char *pr;
                if (s == NULL)
                    goto done;
                dict_pos(d, (uint32_t)nn->unk_pos, w->pos);
                w->string = s;
                pr = ja_unknown_pron(a, s, NULL);
                if (pr == NULL)
                    goto done;
                w->pron = pr;
                w->read = pr;
                w->acc = 0;
                w->mora_size = 0;
                w->chain_rule = "*";
            }
        }
        rc = 0;
    }

done:
    free(chain);
    free(C.v);
    free(L.v);
    free(starts);
    free(ends);
    free(tails);
    free(norm);
    return rc;
}

/* ---- where the dictionary lives ----------------------------------------- */

/*
 * A host does not get to choose its working directory: NVDA runs from its own
 * install and a SAPI client from wherever it likes, so a relative path would
 * never find the file.  The search is the environment first, then beside the
 * module this code is linked into, then beside the executable.
 */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static int module_dir(char *buf, size_t cap)
{
    HMODULE h = NULL;
    DWORD n;
    char *p;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(void *)&module_dir, &h))
        return -1;
    n = GetModuleFileNameA(h, buf, (DWORD)cap);
    if (n == 0 || n >= cap)
        return -1;
    p = strrchr(buf, '\\');
    if (p == NULL)
        p = strrchr(buf, '/');
    if (p == NULL)
        return -1;
    p[1] = '\0';
    return 0;
}
static int exe_dir(char *buf, size_t cap)
{
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)cap);
    char *p;
    if (n == 0 || n >= cap)
        return -1;
    p = strrchr(buf, '\\');
    if (p == NULL)
        p = strrchr(buf, '/');
    if (p == NULL)
        return -1;
    p[1] = '\0';
    return 0;
}
#else
static int exe_dir(char *buf, size_t cap)
{
    FILE *fp = fopen("/proc/self/maps", "rb");
    char *p;
    if (fp != NULL) {
        fclose(fp);
    }
    /* No portable answer outside Windows, and nothing ships there yet, so
     * this is the working directory and the environment variable. */
    if (cap < 3)
        return -1;
    strcpy(buf, "./");
    return 0;
}
static int module_dir(char *buf, size_t cap)
{
    return exe_dir(buf, cap);
}
#endif

static int readable(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;
    fclose(fp);
    return 1;
}

const char *ja_dict_path(char *buf, size_t cap)
{
    /*
     * Beside the module is where a package puts it -- the NVDA add-on and the
     * SAPI installer both ship it next to the DLL.  The two `..` entries are
     * for a development build, where the DLL is in build/bin and the
     * dictionary is at lang/jpn/data/jadic.bin in the repository; without them
     * nothing in the tree would find it without setting the environment
     * variable, which is a trap rather than a design.
     */
    static const char *const rel[] = { "jadic.bin",
                                       "lang/jpn/data/jadic.bin",
                                       "../lang/jpn/data/jadic.bin",
                                       "../../lang/jpn/data/jadic.bin" };
    char dir[1024];
    const char *env = getenv("TVTTS_JA_DICT");
    size_t i;
    int which;

    if (env != NULL && *env != '\0' && readable(env)) {
        if (strlen(env) + 1 > cap)
            return NULL;
        strcpy(buf, env);
        return buf;
    }
    for (which = 0; which < 3; which++) {
        if (which == 0) {
            if (module_dir(dir, sizeof dir) != 0)
                continue;
        } else if (which == 1) {
            if (exe_dir(dir, sizeof dir) != 0)
                continue;
        } else {
            dir[0] = '\0';
        }
        for (i = 0; i < sizeof rel / sizeof *rel; i++) {
            if (strlen(dir) + strlen(rel[i]) + 1 > cap)
                continue;
            strcpy(buf, dir);
            strcat(buf, rel[i]);
            if (readable(buf))
                return buf;
        }
    }
    return NULL;
}
