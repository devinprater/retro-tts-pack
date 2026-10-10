#include <stdlib.h>
#include <string.h>
#include "bst.h"
#include "bst_frames.h"
#include "bst_synth.h"
#include "bst_token.h"
#include "bst_priv.h"

/* The whole engine behind one handle: tokenise, assemble, apply the rule
 * pass, place the accents, scan the pairs, build the contour, generate the
 * frames, run the lattice. The stages are the same ones the test drivers
 * walk one at a time; this is the order the engine walks them in. */

#define MAXSEG  0xD0
#define MAXTRN  0x210
#define MAXITO  0x60
#define MAXEMIT 4096
#define STREAM  0x200

typedef struct {
    const char       *name;
    const bst_tabmap *map;
    int               ne;      /* a 16-bit module, which has to be laid out */
    int               rate;    /* what the build asks the host to play at */
    int               voice;   /* the voice the host starts it on */
    bst_offsets       lat;     /* where the lattice tables sit in the file */
    /* The 1998 modules keep the excitation and gain tables in the core module
       rather than the language one, so those builds need both files. */
    int               core_tables;
} build;

/* The 1995 build starts on voice one; the 1998 and 2006 builds start on
   voice zero. Everything else about the opening voice is the same for all of
   them, and is in bst_open. */
static const build BUILDS[] = {
    { "1995",    &BST_MAP_1995,     0, 11025, 1,
      { 0x17E48, 0x17E08, 0x18488, 0x17A08, 0x17C08, 0x18C20, 0, 0, 0, 0 }, 0 },

    { "1998ENG", &BST_MAP_1998_ENG, 1, 11025, 0,
      { 0xC870, 0xC830, 0xCEB0, 0x7AD20, 0x7AF20, 0, 0, 0, 1, 1 }, 1 },
    { "1998DUT", &BST_MAP_1998_DUT, 1, 11025, 0,
      { 0xC870, 0xC830, 0xCEB0, 0x26866, 0x26A66, 0, 0, 0, 1, 1 }, 1 },
    { "1998FRN", &BST_MAP_1998_FRN, 1, 11025, 0,
      { 0xC870, 0xC830, 0xCEB0, 0x1C582, 0x1C782, 0, 0, 0, 1, 1 }, 1 },
    { "1998GRM", &BST_MAP_1998_GRM, 1, 11025, 0,
      { 0xC870, 0xC830, 0xCEB0, 0x3304E, 0x3324E, 0, 0, 0, 1, 1 }, 1 },
    { "1998ITL", &BST_MAP_1998_ITL, 1, 11025, 0,
      { 0xC870, 0xC830, 0xCEB0, 0x210CE, 0x212CE, 0, 0, 0, 1, 1 }, 1 },
    { "1998SPN", &BST_MAP_1998_SPN, 1, 11025, 0,
      { 0xC870, 0xC830, 0xCEB0, 0x137EC, 0x139EC, 0, 0, 0, 1, 1 }, 1 },

    { "2006ARA", &BST_MAP_2006_ARA, 0, 10000, 0,
      { 0x1FBD8, 0x1FB98, 0x20218, 0x1F71C, 0x1F81C, 0, 1, 5, 1, 0 }, 0 },
    { "2006DUT", &BST_MAP_2006_DUT, 0, 10000, 0,
      { 0x2E2FC, 0x2E2BC, 0x2E93C, 0x1FB08, 0x1FC08, 0, 1, 5, 1, 0 }, 0 },
    { "2006ENG", &BST_MAP_2006_ENG, 0, 10000, 0,
      { 0x78E10, 0x78DD0, 0x79450, 0x1EA2C, 0x1EB2C, 0, 1, 5, 1, 0 }, 0 },
    { "2006FRE", &BST_MAP_2006_FRE, 0, 10000, 0,
      { 0x1B2D4, 0x1B294, 0x1B914, 0x1AFE4, 0x1B0E4, 0, 1, 5, 1, 0 }, 0 },
    { "2006GER", &BST_MAP_2006_GER, 0, 10000, 0,
      { 0x2C1F8, 0x2C1B8, 0x2C838, 0x20E0C, 0x20F0C, 0, 1, 5, 1, 0 }, 0 },
    { "2006GRE", &BST_MAP_2006_GRE, 0, 10000, 0,
      { 0x1E278, 0x1E238, 0x1E8B8, 0x1DF08, 0x1E008, 0, 1, 5, 1, 0 }, 0 },
    { "2006HEB", &BST_MAP_2006_HEB, 0, 10000, 0,
      { 0x1EB28, 0x1EAE8, 0x1F168, 0x1DBEC, 0x1DCEC, 0, 1, 5, 1, 0 }, 0 },
    { "2006ITA", &BST_MAP_2006_ITA, 0, 10000, 0,
      { 0x1ED7C, 0x1ED3C, 0x1F3BC, 0x17B1C, 0x17C1C, 0, 1, 5, 1, 0 }, 0 },
    { "2006JPN", &BST_MAP_2006_JPN, 0, 10000, 0,
      { 0x198D8, 0x19898, 0x19F18, 0x17238, 0x17338, 0, 1, 5, 1, 0 }, 0 },
    { "2006POL", &BST_MAP_2006_POL, 0, 10000, 0,
      { 0x258C4, 0x25884, 0x25F04, 0x25654, 0x25754, 0, 1, 5, 1, 0 }, 0 },
    { "2006POR", &BST_MAP_2006_POR, 0, 10000, 0,
      { 0x21548, 0x21508, 0x21B88, 0x1FE7C, 0x1FF7C, 0, 1, 5, 1, 0 }, 0 },
    { "2006RUS", &BST_MAP_2006_RUS, 0, 10800, 0,
      { 0x77AE4, 0x77AA4, 0x78124, 0x25F8C, 0x2608C, 0, 1, 5, 1, 0 }, 0 },
    { "2006SPA", &BST_MAP_2006_SPA, 0, 10000, 0,
      { 0x17E48, 0x17E08, 0x18488, 0x178FC, 0x179FC, 0, 1, 5, 1, 0 }, 0 },
};

#define NBUILDS ((int)(sizeof BUILDS / sizeof BUILDS[0]))

struct bst {
    const build *b;
    bst_image    img;
    bst_tables   tab;

    int base, top, level, voice, rate;
    int gflags, defexc, gain_base, gain_adj, aflags;

    /* The working buffers, here rather than on the stack because the frame
       run of a long sentence is not small. */
    uint8_t         stream[STREAM];
    bst_emit        em[MAXEMIT];
    bst_seg_rec     seg[MAXSEG];
    bst_trn_rec     trn[MAXTRN];
    bst_ito_rec     ito[MAXITO];
    bst_contour_rec cr[MAXITO];
    uint8_t         frames[8192 * 16];
    int16_t         pcm[4096];
};

int bst_builds(const char **names, int max) {
    for (int i = 0; i < NBUILDS && i < max; i++) names[i] = BUILDS[i].name;
    return NBUILDS;
}

static const build *find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < NBUILDS; i++)
        if (strcmp(BUILDS[i].name, name) == 0) return &BUILDS[i];
    return NULL;
}

static bst *settle(bst *h, const build *b) {
    h->b = b;
    h->base = 0x50;
    h->top = 0xA0;
    h->level = b->map->init_level ? b->map->init_level : 3;
    h->voice = b->voice;
    h->rate = 0;
    h->gflags = 0;
    h->defexc = 0x30;
    h->gain_base = 0x10;
    h->gain_adj = -0x12;
    h->aflags = 0;
    return h;
}

bst *bst_open_images(const char *name, const void *image, size_t len,
                     const void *core, size_t corelen) {
    const build *b = find(name);
    if (!b || !image) return NULL;
    if (b->core_tables && !core) return NULL;
    bst *h = calloc(1, sizeof *h);
    if (!h) return NULL;

    int ok = b->ne ? bst_image_init_ne(&h->img, image, len, b->map)
                   : bst_image_init_map(&h->img, image, len, b->map);
    if (ok < 0 ||
        bst_tables_load_split(&h->tab, image, len, &b->lat,
                              b->core_tables ? core : NULL, corelen) < 0) {
        bst_image_free(&h->img);
        free(h);
        return NULL;
    }
    return settle(h, b);
}

bst *bst_open_image(const char *name, const void *image, size_t len) {
    return bst_open_images(name, image, len, NULL, 0);
}

bst *bst_open(const char *name) {
    const build *b = find(name);
    const bst_lifted *d = b ? bst_lifted_for(name) : NULL;
    if (!d) return NULL;
    bst *h = calloc(1, sizeof *h);
    if (!h) return NULL;

    if (bst_image_init_lifted(&h->img, d, b->map) < 0) { free(h); return NULL; }
    memcpy(&h->tab, d->lat, sizeof h->tab);
    return settle(h, b);
}

void bst_close(bst *h) {
    if (!h) return;
    bst_image_free(&h->img);
    free(h);
}

int bst_rate(const bst *h) { return h ? h->b->rate : 0; }

const bst_image *bst_handle_image(const bst *h) { return h ? &h->img : NULL; }

void bst_handle_tables(const bst *h, bst_tables *out) {
    if (h && out) *out = h->tab;
}

static int *setting(bst *h, const char *name) {
    if (!h || !name) return NULL;
    if (!strcmp(name, "pitch")) return &h->base;
    if (!strcmp(name, "top"))   return &h->top;
    if (!strcmp(name, "level")) return &h->level;
    if (!strcmp(name, "voice")) return &h->voice;
    if (!strcmp(name, "rate"))  return &h->rate;
    /* The two settings the engine's own escapes carry and the five above do
       not: the excitation a phrase opens on, which the ~e escape sets to its
       value times sixteen, and the unvoiced-frame gain adjustment, which the
       ~u escape sets to its value less eighteen. A host that cannot write an
       escape into its text has no other way to reach them, and a voice preset
       needs both to be reproducible. */
    if (!strcmp(name, "exc"))      return &h->defexc;
    if (!strcmp(name, "unvoiced")) return &h->gain_adj;
    return NULL;
}

int bst_set(bst *h, const char *name, int value) {
    int *p = setting(h, name);
    if (!p) return 0;
    *p = value;
    return 1;
}

int bst_get(const bst *h, const char *name) {
    int *p = setting((bst *)h, name);
    return p ? *p : 0;
}

/* One utterance. The samples go wherever `out` says, which is the caller's
   buffer or nowhere at all when it is only the length that is wanted. */
static long run(bst *h, const char *text, int16_t *out, long max) {
    const bst_tabmap *map = h->b->map;
    bst_voice voice;
    bst_pair_state ps = { 0x30, 0, 0, 0 };
    bst_accent_state as = { 0, 0, 0, -1, 0 };
    bst_synth synth;
    bst_tok tk;
    bst_assembler z;
    uint8_t buf[128];
    int level = h->level, have_voice = 0, nphrase = 0;
    int prevmark = 0;   /* the mark that closed the phrase before this */
    int words = 0;      /* whether a word has gone into this phrase */
    long used = 0;

    memset(&voice, 0, sizeof voice);
    bst_synth_init(&synth, &h->tab);
    bst_tok_init(&tk, &h->img, text);
    bst_assemble_init(&z, &h->img, h->stream);
    memset(h->stream, 0, sizeof h->stream);
    bst_assemble_start(&z);

    for (int guard = 0; guard < 4096; guard++) {
        memset(buf, 0, sizeof buf);
        int kind = bst_tok_next(&tk, buf);
        z.mode = 0;
        if (kind == 3) words = 1;
        if (!bst_assemble_token(&z, kind, buf)) {
            if (kind == 6) break;
            continue;
        }

        int len = z.len > 0 ? z.len : z.wp + 1;
        if (len <= 0) {
            memset(h->stream, 0, sizeof h->stream);
            bst_assemble_start(&z);
            continue;
        }
        if (map->no_closing_phrase && nphrase > 0) {
            int spoken = 0;
            for (int i = 12; i < len; i++)
                if (h->stream[i] && h->stream[i] < 0x2F) { spoken = 1; break; }
            /* The build says the phrase the closing brace opens only when the
               phrase before it ended on something other than a full stop,
               which is what a closing brace also is. A phrase a stop written
               in the text closes is not that phrase and is said. A first
               phrase with nothing in it is said too, as the silence it is. */
            if (!spoken && !words && (map->no_closing_phrase == 2 ||
                            (!tk.endreal && (prevmark == '.' ||
                                             prevmark == '}' || prevmark == 0))))
                break;
        }
        nphrase++;
        prevmark = tk.endmark;
        words = 0;

        int grew = bst_phrules(&h->img, h->stream, &len, STREAM, 0);
        z.hdr += grew;

        as.flags = h->aflags;
        as.level = level;
        as.tail = z.hdr >= 0 ? z.hdr + 3 : -1;
        bst_accents(&h->img, h->stream, len, &as);
        level = as.level;

        int ne = bst_pairs(&h->img, h->stream, len, &ps, h->em, MAXEMIT);
        if (ne < 0) ne = MAXEMIT;
        int ns = 0, nt = 0;
        for (int i = 0; i < ne; i++) {
            if (h->em[i].kind == BST_EMIT_SEG) {
                if (ns >= MAXSEG) break;
                h->seg[ns].count = (int16_t)h->em[i].count;
                h->seg[ns].index = h->em[i].index;
                if (h->em[i].index & 0xF000) {
                    h->seg[ns].a = h->em[i].a;
                    h->seg[ns].b = h->em[i].b;
                } else {
                    h->seg[ns].a = -1;
                    h->seg[ns].b = -1;
                }
                ns++;
            } else {
                if (nt >= MAXTRN) break;
                h->trn[nt].index = (uint8_t)h->em[i].index;
                h->trn[nt].dur   = (uint8_t)h->em[i].count;
                h->trn[nt].p1    = (uint8_t)h->em[i].a;
                h->trn[nt].p2    = (uint8_t)h->em[i].b;
                h->trn[nt].span  = -1;
                nt++;
            }
        }

        if (!have_voice) {
            voice.base = voice.voicebase = h->base;
            voice.top = map->voice_top ? map->voice_top : h->top;
            voice.voice = h->voice;
            have_voice = 1;
        }
        voice.level = level;
        voice.strong = ps.strong;
        int ni = bst_contour(&h->img, h->stream, len, &voice, h->cr, MAXITO);
        if (ni > MAXITO) ni = MAXITO;
        ps.strong = voice.strong;
        for (int i = 0; i < ni; i++) {
            h->ito[i].kind   = h->cr[i].kind;
            h->ito[i].period = h->cr[i].period;
            h->ito[i].dur    = h->cr[i].dur;
            h->ito[i].slope  = h->cr[i].slope;
        }

        bst_gen g;
        memset(&g, 0, sizeof g);
        g.img = &h->img; g.tab = &h->tab;
        g.seg = h->seg; g.nseg = ns;
        g.trn = h->trn; g.ntrn = nt;
        g.ito = h->ito; g.nito = ni;
        g.voice = h->voice; g.rate = h->rate; g.rate_loaded = h->rate;
        g.flags = h->gflags; g.defexc = h->defexc;
        g.gain_base = h->gain_base; g.gain_adj = h->gain_adj;
        g.out = h->frames;
        g.maxout = (int)(sizeof h->frames / 16);
        int m = bst_generate(&g);
        if (m > g.maxout) m = g.maxout;

        for (int i = 0; i < m; i++) {
            if (!bst_synth_frame(&synth, h->frames + i * 16)) continue;
            size_t got = bst_synth_run(&synth, h->pcm,
                                       sizeof h->pcm / sizeof h->pcm[0]);
            if (out) {
                long room = max - used;
                long take = (long)got < room ? (long)got : room;
                if (take > 0) memcpy(out + used, h->pcm, (size_t)take * 2);
                used += (long)got;
            } else {
                used += (long)got;
            }
        }

        memset(h->stream, 0, sizeof h->stream);
        bst_assemble_start(&z);
        if (kind == 6) break;
    }
    return used;
}

long bst_say(bst *h, const char *text, int16_t *pcm, long max) {
    if (!h || !text || !pcm || max < 0) return -1;
    long n = run(h, text, pcm, max);
    return n < max ? n : max;
}

long bst_length(bst *h, const char *text) {
    if (!h || !text) return -1;
    return run(h, text, NULL, 0);
}
