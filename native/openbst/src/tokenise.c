#include <stdio.h>
#include <string.h>
#include "bst_token.h"

/* See bst_token.h. The transition table and every string the handlers emit
   are read from the image, so the only thing here is the machine that walks
   them. */

/* The tail the reader adds after the text, which is what closes the last
   sentence whether or not the text ends in a full stop. This is the 1995
   build's; a build that names its own in the map uses that instead. */
static const uint8_t TAIL[4] = { '}', ' ', ' ', 0 };

static int u8at(const bst_image *img, unsigned va, unsigned i) {
    const uint8_t *p = bst_at(img, va + i, 1);
    return p ? *p : 0;
}

static int chattr(const bst_tok *t, int c) { return u8at(t->img, t->img->t.chattr, c & 0xFF); }
static int is_letter(const bst_tok *t, int c) { return (chattr(t, c) & 8) != 0; }
static int is_digit(const bst_tok *t, int c)  { return (chattr(t, c) & 4) != 0; }
static int is_punct(const bst_tok *t, int c)  { return (chattr(t, c) & 2) != 0; }
static int is_upper(const bst_tok *t, int c)  { return (chattr(t, c) & 0x20) != 0; }
static int lower(const bst_tok *t, int c) { return u8at(t->img, t->img->t.casemap, c & 0xFF); }
static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' ||
                                    c == '\r' || c == '\v' || c == '\f'; }

/* ---- input ------------------------------------------------------------- */

/* The sequences the 2006 French build rewrites on its way in. They are
   written into its reader rather than held in a table, so they are written
   here too: three dots become one, "dent" loses its t and "ouvrir" comes out
   as "uvrier". The match is against the text as it was written, before the
   input map, which changes nothing here because none of these letters is in
   the map. */
static const struct { const char *from, *to; } TEXT_FIX_FRENCH[] = {
    { "...",    "."      },
    { "dent",   "den"    },
    { "ouvrir", "uvrier" },
};

/* The next character, from the text while there is any and from the tail
   after that. `real` says which, because the stop the tail carries closes the
   last sentence without being the character the sentence ended on. */
static int source(bst_tok *t, int *real) {
    *real = 1;
    if (t->fixi < t->fixn) return t->fix[t->fixi++];
    if (t->queued) { int c = t->queued; t->queued = 0; return c; }
    if (t->tp < t->tn) {
        if (t->img->t.text_fix_kind == 1) {
            for (size_t k = 0; k < sizeof TEXT_FIX_FRENCH / sizeof TEXT_FIX_FRENCH[0]; k++) {
                const char *from = TEXT_FIX_FRENCH[k].from;
                const char *to   = TEXT_FIX_FRENCH[k].to;
                size_t n = strlen(from);
                if ((size_t)(t->tn - t->tp) < n) continue;
                if (memcmp(t->text + t->tp, from, n) != 0) continue;
                t->tp += (int)n;
                t->fixn = (uint8_t)strlen(to);
                t->fixi = 1;
                memcpy(t->fix, to, t->fixn);
                return (unsigned char)to[0];
            }
        }
        int c = t->text[t->tp++];
        if (t->img->t.in_map[1]) {
            t->queued = t->img->t.in_map2[c];
            c = t->img->t.in_map[c];
        }
        for (int i = 0; i < 4 && t->img->t.squash[i]; i++)
            if (c == t->img->t.squash[i] && t->tp < t->tn &&
                t->text[t->tp] == c) { t->tp++; break; }
        return c;
    }
    *real = 0;
    const uint8_t *tl = t->img->t.tail_chars[0] ? t->img->t.tail_chars : TAIL;
    if (t->tail < 4 && tl[t->tail]) return tl[t->tail++];
    return 0xFFFF;
}

static int rd(bst_tok *t) {
    if (t->push) {
        t->push--;
        t->cur++;
        return t->ring[t->cur] == 0xFF ? 0xFFFF : t->ring[t->cur];
    }
    int real = 0;
    int c = source(t, &real);
    if (c == 0xFFFF) {
        if (t->cur + 1 < BST_TOK_RING) t->ring[++t->cur] = 0xFF;
        return 0xFFFF;
    }
    if (c == 0x5C) {
        int dreal;
        int d = source(t, &dreal);
        if (d == 0x7E) c = 0x7E;
        else if (d != 0x5C && d != 0xFFFF) { if (t->tp > 0) t->tp--; else t->tail--; }
    }
    if (t->cur + 1 < BST_TOK_RING) t->ring[++t->cur] = (uint8_t)c;
    if (t->cur + 1 > t->nring) t->nring = t->cur + 1;
    if (real && t->cur > t->textend) t->textend = t->cur;
    return c;
}

int bst_tok_read(bst_tok *t) { return rd(t); }

/* The lookahead an exception entry's conditions read. It runs on through the
   end of the text and into the tail the reader adds after it, which is how an
   entry that wants the full stop after it can tell that one is there when the
   text ends on it. */
int bst_tok_peek_read(bst_tok *t) {
    return rd(t);
}

static void unread(bst_tok *t, int n) {
    t->push += n;
    t->cur -= n;
    if (t->cur < -1) { t->cur = -1; t->push = 0; }
}

void bst_tok_unread(bst_tok *t, int n) { unread(t, n); }

/* ---- output ------------------------------------------------------------ */

static void emit(bst_tok *t, int c) {
    if (c == ' ' && !t->textmode && t->lastout == ' ') return;
    if (t->overflow) return;
    t->lastout = c;
    if (t->nout >= BST_TOK_OUT) { t->overflow = 1; t->nout = 0; return; }
    t->out[t->nout++] = (uint8_t)c;
}

/* A run of phoneme codes, bracketed by the two mode markers. */
void bst_tok_put(bst_tok *t, int c) { emit(t, c); }

static void emit_bytes(bst_tok *t, const uint8_t *p) {
    if (!p || !*p) return;
    emit(t, ' ');
    emit(t, 0xFE);
    int cmd = bst_code(t->img, 0x7C);
    for (; *p; p++) {
        if (*p == cmd) {
            emit(t, 0x7C);
            for (int k = 1; k < 6 && p[1]; k++) emit(t, *++p);
        } else emit(t, bst_uncode(t->img, *p));
    }
    emit(t, 0xFF);
    emit(t, ' ');
}

static void emit_codes(bst_tok *t, unsigned va) {
    emit_bytes(t, bst_at(t->img, va, 1));
}

/* Several handlers say something outright rather than spelling it: the
   pointer names a stored code string. Only the ones plain text reaches are
   wired up so far. */
static void emit_ptr(bst_tok *t, unsigned ptrva) {
    const uint8_t *e = bst_at(t->img, ptrva, 4);
    if (!e) return;
    uint32_t va = (uint32_t)(e[0] | (e[1] << 8) | (e[2] << 16) | (e[3] << 24));
    if (va) emit_codes(t, va);
}

void bst_tok_say(bst_tok *t, unsigned ptrva) { emit_ptr(t, ptrva); }

/* The French build says a word that stands on its own with a marker joined to
   the front of it, which is what lets the rules decide the sound it ends on. */
void bst_tok_say_marked(bst_tok *t, int lead, unsigned ptrva) {
    uint8_t buf[160];
    const uint8_t *e = bst_at(t->img, ptrva, 4);
    if (!e) return;
    unsigned va = (unsigned)(e[0] | (e[1] << 8) | (e[2] << 16) | (e[3] << 24));
    const uint8_t *p = va ? bst_at(t->img, va, 1) : NULL;
    if (!p || !*p) return;
    size_t n = 0;
    buf[n++] = (uint8_t)bst_code(t->img, lead);
    for (size_t k = 0; p[k] && n + 1 < sizeof buf; k++) buf[n++] = p[k];
    buf[n] = 0;
    emit_bytes(t, buf);
}

/* The Italian build runs one word into the next by copying it out and cutting
   the sound it ends on off the copy. */
void bst_tok_say_clip(bst_tok *t, unsigned ptrva) {
    uint8_t buf[160];
    const uint8_t *e = bst_at(t->img, ptrva, 4);
    if (!e) return;
    unsigned va = (unsigned)(e[0] | (e[1] << 8) | (e[2] << 16) | (e[3] << 24));
    const uint8_t *p = va ? bst_at(t->img, va, 1) : NULL;
    if (!p || !*p) return;
    size_t n = 0;
    while (p[n] && n + 1 < sizeof buf) n++;
    memcpy(buf, p, n);
    buf[n - 1] = 0;
    emit_bytes(t, buf);
}

/* ---- the transition table ---------------------------------------------- */

static uint32_t col(const uint8_t *p, int off, int w) {
    uint32_t v = 0;
    for (int i = 0; i < w; i++) v |= (uint32_t)p[off + i] << (8 * i);
    return v;
}

static int row(const bst_tok *t, int i, int *state, unsigned *handler, int *next) {
    const bst_tabmap *m = &t->img->t;
    int stride = m->tok_stride;
    const uint8_t *p = bst_at(t->img, m->tokstates + (unsigned)i * stride, stride);
    if (!p) return 0;
    uint32_t h = col(p, m->tok_handler_off, m->tok_handler_w);
    if (h < m->code_lo || h >= m->code_hi) return 0;
    *state = (int)col(p, m->tok_state_off, m->tok_state_w);
    *handler = h;
    *next = (int)col(p, m->tok_next_off, m->tok_next_w);
    return 1;
}

/* ---- the handlers ------------------------------------------------------ */

static void word_out(bst_tok *t);
static int dotted_out(bst_tok *t);
static int number_out(bst_tok *t);
static int one_digit_out(bst_tok *t);
static int groups_out(bst_tok *t);
static int sepnum_out(bst_tok *t);
static int ordinal_seen(bst_tok *t, int c);
static void ordinal_emit(bst_tok *t);
static int dash_out(bst_tok *t, int c);
static void say_char(bst_tok *t, int c);
static void word_range(bst_tok *t, int from, int to, int dotted);

/* Punctuation that closes a phrase versus punctuation that only groups. */
static int closes(int b) {
    switch (b) {
    case '"': case 0xAE: case '\'': case '{': case '[': case '(':
    case '`': case ',':  case ';':   case ':': case '.': case '!':
    case '?': case 0xAF: case '}':   case ']': case ')': case '-':
        return 1;
    default:
        return 0;
    }
}

static void punct_out(bst_tok *t, int c) {
    int b = t->ring[t->start];
    t->prevkind = t->kind;
    t->kind = 3;
    unread(t, 1);
    t->quote = (b == '"' || b == 0xAE || b == '\'' || b == '{' ||
                b == '[' || b == '(' || b == '`');
    t->sentence = !closes(b);
    if (b == '.' || b == '?' || b == '!' || b == ',' || b == ';') t->lastend = b;
    t->rawmark = b;
    if (t->start <= t->textend) t->markreal = 1;
    if (t->literal) return;
    /* A tilde is a lead-in rather than a symbol: it and the character after
       it are one mark, and neither is said by name. Speak now closes the
       phrase where it stands, which is what the bar does as punctuation.
       Two builds do neither: the Japanese one reads the two characters and
       goes on as though they were absent, and the Russian one stops the text
       there. */
    if (b == 0x7E && c == 0x7C) {
        rd(t);
        if (t->img->t.speak_now_none == 2) t->tp = t->tn;
        else if (!t->img->t.speak_now_none) emit(t, 0x7C);
        return;
    }
    if (b == ':' && is_digit(t, t->prevch) && is_digit(t, c)) { emit(t, ','); return; }
    if (b == '}' && t->img->t.brace_ends_text) {
        /* The text stops here. Whatever the reader has already pulled in
           behind this mark is the tail's as far as the rest is concerned. */
        t->tp = t->tn;
        t->textend = t->start;
        emit(t, b);
        return;
    }
    if (b == ',' && t->img->t.comma_ends_text) {
        /* The build stops here: nothing after the comma reaches the machine.
           Most builds then say the comma the way a full stop is said; the
           Russian one says nothing at all. */
        t->tp = t->tn;
        t->textend = t->start;
        t->lastend = t->rawmark = '.';
        if (t->img->t.comma_ends_text != 2) emit(t, '.');
        return;
    }
    if (t->img->t.punct_dropped && t->start <= t->textend) return;
    if (!t->sentence && b != '\'' && b != '`') { emit(t, b); return; }
    if (b == '-') { emit(t, ','); return; }
    /* Anything else is said by name: "percent", "dollar", "at". */
    say_char(t, b);
}

static void dot_out(bst_tok *t, int c) {
    (void)c;
    t->sentence = 0;
    t->rawmark = '.';
    if (t->start <= t->textend) t->markreal = 1;
    if (t->cur <= t->textend) t->lastend = '.';
    t->prevkind = t->kind;
    t->kind = 3;
    unread(t, 1);
    if (t->literal) { emit(t, '.'); return; }

    /* A full stop that something other than a space follows is part of a
       token rather than the end of one. */
    int n = 0, d;
    do {
        do { n++; d = rd(t); } while (d == '"');
    } while (d == 0xAF || d == '\'' || d == '}' || d == ']' || d == ')');
    unread(t, n);
    if (!is_space(d) && d != 0xFFFF && d != 0x7F) return;
    if (t->img->t.punct_dropped && t->start <= t->textend) return;
    emit(t, '.');
}

/* The table names its handlers by address, and every build puts them
   somewhere different, so the address is turned back into the directory's
   index before anything is decided on it. */
static int handler_index(const bst_image *img, unsigned h) {
    for (int i = 0; i < BST_H_COUNT; i++)
        if (img->t.h[i] == h) return i;
    return -1;
}

static int handler(bst_tok *t, unsigned h, int c) {
    switch (handler_index(t->img, h)) {
    case BST_H_LETTER:   return is_letter(t, c);
    case BST_H_DIGIT:    return is_digit(t, c);
    case BST_H_SPACE:    return is_space(c);
    case BST_H_DOT:      return c == 0x2E;
    case BST_H_DASH:     return c == 0x2D;
    case BST_H_TILDE:    return c == 0x7E;
    case BST_H_APOS:     return c == 0x27;
    case BST_H_APOSDOT:  return c == 0x27 || c == 0x2E || c == 0x7E;
    case BST_H_CURRENCY: return c == 0x24 || c == 0x9C || c == 0xBE;
    case BST_H_PUNCT:    return is_punct(t, c);
    case BST_H_MODE1:    return 0;
    case BST_H_MODE2:    return 0;
    case BST_H_EXPONENT: return 0;
    case BST_H_OPENER:   return 0;
    case BST_H_POSSESS:  return 0;
    case BST_H_DEL:      if (c == 0x7F) { unread(t, 1); return 1; } return 0;
    case BST_H_EAT1:     unread(t, 1); return 1;
    case BST_H_EAT2:     unread(t, 2); return 1;
    case BST_H_EAT3:     unread(t, 3); return 1;
    case BST_H_PUNCTOUT: punct_out(t, c); return 1;
    case BST_H_DOTOUT:   dot_out(t, c); return 1;
    case BST_H_ONEDIGIT: return one_digit_out(t);
    case BST_H_WORD:     unread(t, 1); word_out(t); return 1;
    case BST_H_NUMBER:   return number_out(t);
    case BST_H_DOTTED:   return dotted_out(t);
    case BST_H_GROUPS:   return groups_out(t);
    case BST_H_SEPNUM:   return sepnum_out(t);
    case BST_H_DASH2:    return dash_out(t, c);
    case BST_H_ORDINAL:  return ordinal_seen(t, c);
    case BST_H_ORDEMIT:  ordinal_emit(t); unread(t, 1); return 1;
    case BST_H_MONEY:    /* a currency sign: the number after it is an amount */
        t->money = 1;
        unread(t, 1);
        return 1;
    case BST_H_SEP:      /* what may sit between two runs of digits */
        return c == ':' || c == '.' || c == ',' || c == '/' || c == '-' || c == ' ';
    /* Two rows ask the same question in different places: one ends a word,
       the other ends a run of digits. */
    case BST_H_HIGH:
    case BST_H_HIGH2:    return c > 0x7F;
    case BST_H_HIGHOUT:
        /* A character above the alphabet that none of the build's tables
           knows. The row before this one has already taken it; this one is
           reached with the character after it in hand and gives that back.
           What it leaves behind is a word boundary and no sound, so "nino"
           with a tilde over the n comes out as "ni o". */
        unread(t, 1);
        t->prevkind = t->kind;
        t->kind = 8;
        return 1;
    case BST_H_DASHLETTER: {
        /* One letter with a hyphen on each side, the middle of "c.-a-d.".
           The letter is said on its own and everything after it is given
           back, so the hyphen that follows starts the next piece. */
        unread(t, 1);
        int p = t->start;
        while (p < t->cur && t->ring[p] == '-') p++;
        word_range(t, p, p, 0);
        if (t->cur > p) unread(t, t->cur - p);
        t->prevkind = t->kind;
        t->kind = 5 - ((chattr(t, t->ring[p]) & 0x20) == 0);
        return 1;
    }
    default:         return 0;
    }
}

/* What a character is called when it is said rather than read. The table is
   indexed from five upward, with three ranges packed end to end. */
static void say_char(bst_tok *t, int c) {
    int idx;
    if (c < 0x21) return;
    if (c < 0x30)      idx = c - 0x21;
    else if (c < 0x41) idx = c - 0x2B;
    else               idx = (is_upper(t, c) ? lower(t, c) : c) - 0x45;
    emit_ptr(t, t->img->t.names + (unsigned)idx * 4);
}

/* The word the machine has just delimited. The exception table gets first
   refusal: a word it holds goes into the scratch buffer as phoneme codes and
   never reaches the dictionary or the rules. */
/* `dotted` says the piece came from a run like "i.e.", where a lone letter is
   always said by name rather than looked up. */
static void word_range(bst_tok *t, int from, int to, int dotted) {
    int n = to - from + 1;
    int first = n > 0 ? t->ring[from] : 0;

    /* A possessive is two words here, and the second of them is "is", which
       is what the two characters become once the apostrophe is overwritten. */
    if (t->img->t.possessive_is && n > 2 &&
        (t->ring[to] == 's' || t->ring[to] == 'S') &&
        (t->ring[to - 1] == '\'' || t->ring[to - 1] == '`')) {
        word_range(t, from, to - 2, dotted);
        emit(t, ' ');
        t->ring[to - 1] = 'i';
        word_range(t, to - 1, to, 0);
        return;
    }
    /* A lone letter is said by name. The letters that are words in their own
       right are not, and neither is one the exception table holds. */
    if (n == 1 && (dotted || t->img->t.lone_no_words ||
                   (first != 'a' && first != 'A' && first != 'I'))) {
        uint8_t codes[64];
        int m = dotted ? 0 : bst_except_at(t, t->ring + from, 1, codes,
                                          (int)sizeof codes, to == t->cur);
        if (m > 0) {
            emit(t, ' ');
            emit(t, 0xFE);
            for (int i = 0; i < m; i++) emit(t, bst_uncode(t->img, codes[i]));
            emit(t, 0xFF);
            emit(t, ' ');
            t->prevkind = t->kind;
            t->kind = 5 - ((chattr(t, first) & 0x20) == 0);
            return;
        }
        say_char(t, first);
        t->prevkind = t->kind;
        t->kind = 5 - ((chattr(t, first) & 0x20) == 0);
        return;
    }
    if (n > 0 && n < 64) {
        uint8_t codes[256];
        int m = bst_except_at(t, t->ring + from, n, codes, (int)sizeof codes,
                              to == t->cur);
        if (m > 0) {
            emit(t, ' ');
            emit(t, 0xFE);
            for (int i = 0; i < m; i++) emit(t, bst_uncode(t->img, codes[i]));
            emit(t, 0xFF);
            emit(t, ' ');
            t->prevkind = t->kind;
            t->kind = 5 - ((chattr(t, t->ring[from]) & 0x20) == 0);
            return;
        }
    }
    /* A short word in capitals is an abbreviation, and is spelled. The run of
       capitals has to be the whole word, or the whole word bar a plural s. */
    int caps = 0;
    while (caps < n && is_upper(t, t->ring[from + caps])) caps++;
    if (caps < 5 && n > 1 &&
        (caps == n || (caps == n - 1 && t->ring[to] == 's'))) {
        for (int i = 0; i < caps; i++) say_char(t, lower(t, t->ring[from + i]));
        if (caps != n) emit_ptr(t, t->img->t.s[BST_S_PLURAL]);
        t->prevkind = t->kind;
        t->kind = 5;
        return;
    }

    /* A word with no vowel in it cannot be pronounced, so it is spelled. */
    int vowel = 0;
    for (int i = from; i <= to; i++) if (chattr(t, t->ring[i]) & 0x10) { vowel = 1; break; }
    if (!vowel) {
        for (int i = from; i <= to; i++) say_char(t, t->ring[i]);
        t->prevkind = t->kind;
        t->kind = 5 - ((chattr(t, t->ring[from]) & 0x20) == 0);
        return;
    }

    char w[128];
    int wn = 0;
    for (int i = from; i <= to && wn < 120; i++) {
        int c = t->ring[i];
        w[wn++] = (char)(is_upper(t, c) ? lower(t, c) : c);
    }
    w[wn] = 0;

    /* A build that writes its compounds run together cuts the word into the
       parts the rules are to read, with a marker between them that keeps
       them one word with one accent. */
    int cut[8];
    int nc = bst_word_split(t->img, w, cut, 8);
    int p = 0;
    for (int j = 0; j < nc; j++) {
        for (; p <= cut[j] && p < wn; p++) emit(t, (unsigned char)w[p]);
        emit(t, ' ');
        emit(t, 0xFE);
        emit(t, 0x7C);
        emit(t, 0x7E);
        emit(t, 0x00);
        emit(t, 0x1C);
        emit(t, 0x00);
        emit(t, 0x00);
        emit(t, 0xFF);
        emit(t, ' ');
    }
    for (; p < wn; p++) emit(t, (unsigned char)w[p]);
    emit(t, ' ');
    t->prevkind = t->kind;
    t->kind = 5 - ((chattr(t, t->ring[from]) & 0x20) == 0);
}

static void word_out(bst_tok *t) { word_range(t, t->start, t->cur, 0); }

/* A run of single letters separated by full stops. The whole span gets one
   look in the exception table, and failing that each piece is said on its
   own, which for a lone letter means by name. */
static int dotted_out(bst_tok *t) {
    /* The stop that ended the run belongs to the abbreviation, so it is not
       given back and no phrase break comes of it. */
    int last = t->cur - 1;
    int n = last - t->start + 1;
    if (n > 0 && n < 64) {
        uint8_t codes[256];
        int m = bst_except(t, t->ring + t->start, n, codes, (int)sizeof codes);
        if (m > 0) {
            emit(t, ' ');
            emit(t, 0xFE);
            for (int i = 0; i < m; i++) emit(t, bst_uncode(t->img, codes[i]));
            emit(t, 0xFF);
            emit(t, ' ');
            t->prevkind = t->kind;
            t->kind = 5 - ((chattr(t, t->ring[t->start]) & 0x20) == 0);
            return 1;
        }
    }
    int i = t->start;
    while (i <= last) {
        int j = i;
        while (j <= last && t->ring[j] != '.') j++;
        if (j > i) word_range(t, i, j - 1, 1);
        i = j + 1;
    }
    return 1;
}

/* A run of digits. The machine has read one character past it, and the run
   itself is whatever lies between the token's start and here. */
/* The currency word, singular when the amount is exactly one. */
static void say_currency(bst_tok *t, const uint8_t *d, int n) {
    int one = (n == 1 && d[0] == '1');
    emit_ptr(t, t->img->t.s[one ? BST_S_DOLLAR : BST_S_DOLLARS]);
    t->money = 0;
}

/* The Polish build reads a lone digit as a number and hands a longer run
   back, a digit at a time, to the state after it. */
static int one_digit_out(bst_tok *t) {
    int p = t->start;
    if (t->cur > p) unread(t, t->cur - p);
    if (!is_digit(t, t->ring[p])) { say_char(t, t->ring[p]); }
    else                          { bst_say_number(t, t->ring + p, 1); emit(t, ','); }
    t->prevkind = t->kind;
    t->kind = 6;
    return 1;
}

static int number_out(bst_tok *t) {
    unread(t, 1);
    while (t->cur >= t->start && !is_digit(t, t->ring[t->cur])) unread(t, 1);
    int n = t->cur - t->start + 1;
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++)
        if (!is_digit(t, t->ring[t->start + i])) return 0;
    if (t->img->t.num_one_digit && n > 1) return 0;
    /* The Arabic and Japanese builds have the tables for a number and never
       reach them: a run of digits leaves no sound at all. */
    if (t->img->t.num_silent) {
        t->prevkind = t->kind;
        t->kind = 6;
        return 1;
    }
    bst_say_number(t, t->ring + t->start, n);
    if (t->money) say_currency(t, t->ring + t->start, n);
    t->prevkind = t->kind;
    t->kind = 6;
    return 1;
}

/* A dash. Two or more in a row are a break in the sentence and go into the
   scratch as themselves; a single one between digits is a group separator,
   and anywhere else it is a pause with nothing said. */
static int dash_out(bst_tok *t, int c) {
    int n = t->cur - t->start;
    t->prevkind = t->kind;
    t->kind = ((t->kind == 4 || t->kind == 5) && n == 1) ? 0x0F : 3;
    unread(t, 1);
    if (n >= 2) { emit(t, '-'); return 1; }
    if (is_digit(t, t->prevch) && is_digit(t, c)) { emit_ptr(t, t->img->t.s[BST_S_GRPSEP]); return 1; }
    return 1;
}

/* "1st", "2nd", "3rd", "4th": the two letters after a number, if they are the
   right ones for its last digit. */
static int ordinal_seen(bst_tok *t, int c) {
    int a = is_upper(t, c) ? lower(t, c) : c;
    if (a != 't' && a != 'n' && a != 'r' && a != 's') return 0;
    int b = rd(t);
    b = is_upper(t, b) ? lower(t, b) : b;
    int kind;
    if      (a == 'n' && b == 'd') kind = 1;
    else if (a == 'r' && b == 'd') kind = 2;
    else if (a == 's' && b == 't') kind = 3;
    else if (a == 't' && b == 'h') kind = 0;
    else { unread(t, 1); return 0; }

    /* Nothing but a plural s may follow. */
    int d = rd(t);
    int dl = is_upper(t, d) ? lower(t, d) : d;
    if (is_letter(t, dl)) {
        if (dl != 's') { unread(t, 2); return 0; }
        int e = rd(t);
        if (is_letter(t, is_upper(t, e) ? lower(t, e) : e)) { unread(t, 3); return 0; }
        unread(t, 1);
    }
    unread(t, 1);

    /* The suffix has to agree with the last digit, and the teens take th. */
    int last = 0, prev = '0';
    for (int i = t->cur; i >= t->start; i--)
        if (is_digit(t, t->ring[i])) {
            last = t->ring[i];
            if (i > t->start && is_digit(t, t->ring[i - 1])) prev = t->ring[i - 1];
            break;
        }
    if (!last) return 0;
    switch (kind) {
    case 0: if (prev != '1' && last > '0' && last < '4') return 0; break;
    case 1: if (last != '2' || prev == '1') return 0; break;
    case 2: if (last != '3' || prev == '1') return 0; break;
    case 3: if (last != '1' || prev == '1') return 0; break;
    default: return 0;
    }
    t->ord = kind + 1;
    t->ordlast = last;
    t->ordprev = prev;
    return 1;
}

/* Rewrites the tail of what has just been said into the ordinal form: the
   scratch is wound back to a chosen sound and the suffix put on instead. */
static void ordinal_emit(bst_tok *t) {
    int stop;
    unsigned str;
    switch (t->ord) {
    case 4: stop = 8;    str = t->img->t.s[BST_S_ORD_ST]; break;
    case 2: stop = 0x18; str = t->img->t.s[BST_S_ORD_ND]; break;
    case 3: stop = 0x12; str = t->img->t.s[BST_S_ORD_RD]; break;
    case 1:
        if (t->ordlast == '0' && (t->ordprev > '1' ||
                                  (t->ordprev != '1' && t->ordprev != '0'))) {
            stop = 0; str = t->img->t.s[BST_S_ORD_TIETH];
        } else if (t->ordlast == '5' && t->ordprev != '1') {
            stop = 0x1F; str = t->img->t.s[BST_S_ORD_FIFTH];
        } else if (t->ordlast == '2' && t->ordprev == '1') {
            stop = 4; str = t->img->t.s[BST_S_ORD_FIRST];
        } else {
            stop = 0; str = t->img->t.s[BST_S_ORD_TH];
        }
        break;
    default:
        t->ord = 0;
        return;
    }
    t->ord = 0;
    int marker = stop ? stop : 0xFF;
    while (t->nout > 0) { t->nout--; if (t->out[t->nout] == marker) break; }
    const uint8_t *e = bst_at(t->img, str, 4);
    if (e) {
        uint32_t va = (uint32_t)(e[0] | (e[1] << 8) | (e[2] << 16) | (e[3] << 24));
        const uint8_t *p = va ? bst_at(t->img, va, 1) : NULL;
        for (; p && *p; p++) emit(t, *p);
    }
    emit(t, 0xFF);
}

/* A number written with thousands separators, and optionally a decimal part.
   The first group may be one to three digits and every later group is three;
   what follows the last group, if anything, is the decimal separator and the
   digits after it are spelled. */
static int sepnum_out(bst_tok *t) {
    uint8_t digits[32];
    int nd = 0, p = t->start, first = 1;
    if (p > t->cur || !is_digit(t, t->ring[p])) return 0;
    for (;;) {
        int q = p;
        while (q <= t->cur && is_digit(t, t->ring[q])) q++;
        int len = q - p;
        if (first && t->img->t.sepnum_first_three && len != 3 &&
            q <= t->cur && t->ring[q] == ',') {
            /* The build gives up here: the first group is said and the rest
               of the text never reaches the machine. */
            bst_say_number(t, t->ring + p, len);
            if (t->money) say_currency(t, t->ring + p, len);
            t->push = 0;
            t->tp = t->tn;
            t->prevkind = t->kind;
            t->kind = 6;
            return 1;
        }
        if (first ? (len < 1 || len > 3) : (len != 3)) return 0;
        first = 0;
        for (int i = 0; i < len && nd < 24; i++) digits[nd++] = t->ring[p + i];
        if (q > t->cur || t->ring[q] != ',') { p = q; break; }
        p = q + 1;
    }
    if (nd == 0) return 0;

    int frac = -1, fn = 0;
    if (p <= t->cur && t->ring[p] == '.') {
        int q = p + 1;
        while (q <= t->cur && is_digit(t, t->ring[q])) q++;
        if (q > p + 1) { frac = p + 1; fn = q - (p + 1); p = q; }
    }
    if (nd <= 3 && frac < 0) return 0;   /* a plain number is not our business */

    bst_say_grouped(t, digits, nd);
    if (t->money) {
        emit_ptr(t, t->img->t.s[(nd == 1 && digits[0] == '1')
                                ? BST_S_DOLLAR : BST_S_DOLLARS]);
        if (frac >= 0) {
            emit_ptr(t, t->img->t.s[BST_S_AND]);
            bst_say_number(t, t->ring + frac, fn);
            emit_ptr(t, t->img->t.s[BST_S_CENTS]);
        }
        t->money = 0;
    } else if (frac >= 0) {
        emit_ptr(t, t->img->t.s[BST_S_POINT]);
        bst_say_digits(t, t->ring + frac, fn);
    }
    unread(t, t->cur - (p - 1));
    t->prevkind = t->kind;
    t->kind = 6;
    return 1;
}

/* Runs of digits joined by hyphens: a telephone number, a date. Any group
   longer than three means the whole thing is spelled digit by digit with a
   pause between the groups; otherwise each group is read as a number. */
static int groups_out(bst_tok *t) {
    int p = t->start;
    if (p > t->cur || !is_digit(t, t->ring[p])) return 0;

    int ends[12], n = 0, longest = 0;
    while (p <= t->cur && n < 12) {
        int q = p;
        while (q <= t->cur && is_digit(t, t->ring[q])) q++;
        if (q == p) break;
        if (q - p > longest) longest = q - p;
        ends[n++] = q;
        if (q > t->cur || t->ring[q] != '-') break;
        p = q + 1;
    }
    if (n < 2) return 0;

    /* One group of four followed by one of one, two or four is a year and a
       fraction of one, and is read rather than spelled. */
    int spell = longest > 3;
    if (n == 2) {
        int a = ends[0] - t->start, b = ends[1] - ends[0] - 1;
        if (a == 4 && (b == 1 || b == 2 || b == 4)) spell = 0;
    }

    int at = t->start;
    for (int g = 0; g < n; g++) {
        int len = ends[g] - at;
        if (spell) bst_say_digits(t, t->ring + at, len);
        else       bst_say_number(t, t->ring + at, len);
        if (g + 1 < n) {
            if (spell) emit(t, ',');
            else       emit_ptr(t, t->img->t.s[BST_S_GRPSEP]);
        }
        at = ends[g] + 1;
    }

    /* The cursor goes back to the last digit consumed. */
    unread(t, t->cur - (ends[n - 1] - 1));
    t->prevkind = t->kind;
    t->kind = 6;
    return 1;
}

/* ---- the machine ------------------------------------------------------- */

/* The first row for a state. The rows for a state are contiguous, and once
   inside one the machine walks forward without looking at the state column
   again, so a state with no match falls into the next state's rows. */
static int first_row(const bst_tok *t, int state) {
    int i = 0, st, nx;
    unsigned h;
    int prev = -1;
    while (row(t, i, &st, &h, &nx)) {
        if (st != prev) {
            if (st == state) return i;
            prev = st;
        }
        i++;
    }
    return -1;
}

/* Runs until the machine returns to state zero, which is one unit of text. */
static int scan(bst_tok *t) {
    t->overflow = 0;
    t->state = 0;
    for (int guard = 0; guard < 100000; guard++) {
        int c = rd(t);
        if (c == 0xFFFF) return 0;

        int i = first_row(t, t->state);
        if (i < 0) return 0;
        int st, nx;
        unsigned h;
        while (row(t, i, &st, &h, &nx)) {
            if (handler(t, h, c)) break;
            i++;
        }
        if (!row(t, i, &st, &h, &nx)) return 0;
        if (bst_trace)
            bst_tracef("sm %d '%c' %08x -> %d\n", t->state,
                       (c >= 32 && c < 127) ? c : '.', h, nx);
        t->state = nx;
        if (t->state == 0) {
            t->start = t->cur + 1;
            return 1;
        }
    }
    return 0;
}

/* ---- the classifier ----------------------------------------------------- */

/* Reads the scratch buffer a byte at a time, refilling it from the machine,
   and says what kind of thing each byte is. */
static int classify(bst_tok *t, int *cls) {
    for (;;) {
        while (t->pos < t->nout) {
            int b = t->out[t->pos++];
            if (t->raw) { t->raw--; *cls = 0; return b; }
            if (b == 0xFE) { t->textmode = 0; continue; }
            if (b == 0xFF) { t->textmode = 1; *cls = 0x13; return 0x20; }
            if (t->textmode) {
                if (is_letter(t, b) || b == 0x27) *cls = 0x111;
                else if (b == 0x20)               *cls = 0x13;
                else                              *cls = 0x112;
                return b;
            }
            /* A sound, including one of the extra ones a language with more
               than forty-eight keeps above 0x80. */
            if ((b != 0 && b < 0x31) || b >= 0x80 ||
                (b > 0x4E && b < 0x5C)) { *cls = 0x124; return b; }
            if (b < 0x39) { *cls = 0x25;  return b; }
            if (b < 0x49) { *cls = 0x68;  return b; }
            if (b < 0x4F) { *cls = 0x146; return b; }
            if (b == 0x5C) { *cls = 0x47; return b; }
            if (b == 0x7C) { *cls = 0x189; t->raw = 5; return b; }
        }
        if (t->ended) { *cls = 0x101; return 0xFFFF; }
        t->pos = 0;
        t->nout = 0;
        /* A pass that neither consumes input nor emits anything would spin
           for ever, which a build whose state table is not yet placed can
           do. Treat it as the end of the text. */
        int before = t->cur;
        if (!scan(t)) t->ended = 1;
        else if (t->nout == 0 && t->cur == before) t->ended = 1;
    }
}

static int token(bst_tok *t, uint8_t *buf);

/* ---- the token ring ----------------------------------------------------- */

/* A sentence longer than one breath gets a break put into it, at the last
   word whose opening marker allows one. The further back that word is, the
   longer the run has to be to justify breaking there. */
static void split(bst_tok *t) {
    int prev = 0, n = t->held, i = t->rd;
    int acc = 0;
    if (!n || t->img->t.no_breath_break) return;
    while (n--) {
        if (t->tok[i].type == 3) {
            int f = t->tok[i].flag;
            if (acc > t->window && (bst_ph_attr2(t->img, f) & 0x40) &&
                !(bst_ph_attr2(t->img, prev) & 0x80)) {
                int back = i ? i - 1 : 19;
                if (t->tok[back].flag == 0x18) return;
                int take = 0;
                if (f == 0x53 || f == 0x57) take = 1;
                else if (f == 0x54) take = acc > t->window * 2;
                else if (f == 0x55 || f == 0x51) take = acc > t->window * 4;
                if (take) {
                    t->tok[i].pushback = 0x5D;
                    t->blocked |= 1;
                    return;
                }
            }
            prev = t->tok[i].flag;
            acc += t->tok[i].flag ? t->tok[i].len - 4 : t->tok[i].len;
        }
        if (t->total - t->window < acc) return;
        i = (i + 1) % 20;
    }
}

/* Reads one token and files it. */
static void produce(bst_tok *t) {
    uint8_t buf[128];
    memset(buf, 0, sizeof buf);
    int kind = token(t, buf);

    /* A word arrives as text and is pronounced here, before it is filed, so
       that the breath splitter can see the marker it opens with. */
    if (kind == 2) {
        char word[128];
        int n = 0;
        for (const uint8_t *q = buf + 1; *q && n < 120; q++) word[n++] = (char)*q;
        word[n] = 0;
        bst_recs r;
        bst_stream st;
        bst_word w;
        bst_normalise(t->img, word, &w);
        bst_word_pronounce(t->img, word, &r, &st);
        int m = st.len > 1 ? st.len - 2 : 0;
        buf[0] = (uint8_t)m;
        memcpy(buf + 1, st.buf, (size_t)st.len);
        /* A word that lost an -ed or -ing is marked, because the assembler
           treats it as one syllable longer than it looks. */
        if (w.flags & 0xC0) buf[1] = 'V';
        kind = 3;
    }

    int i = t->wr;
    t->wr = (t->wr + 1) % 20;
    t->held++;
    memset(&t->tok[i], 0, sizeof t->tok[i]);
    t->tok[i].type = (uint8_t)kind;

    int len = 0;
    switch (kind) {
    case 1:
        len = 6;
        t->tok[i].flag = 1;
        t->tok[i].pushback = 1;
        t->blocked |= 1;
        break;
    case 3:
        /* A spelled-out letter opens with one marker and may carry a second
           the assembler does not want. */
        if (t->run < 3) {
            t->run++;
            if (buf[2] == 'X') {
                t->quest = 1;
                if (buf[3] == 'Q')      buf[3] = 0;
                else if (buf[4] == 'Q') buf[4] = 0;
            }
        }
        len = buf[0] + 2;
        t->tok[i].flag = buf[1];
        t->total += buf[1] ? len - 4 : len;
        break;
    case 4:
        switch (buf[0]) {
        case '!': case '.': case ';': case '>':
            buf[0] = '.'; t->run = 0; t->quest = 0; break;
        case '(': case '-': case ':': case '[':
            buf[0] = ','; break;
        case ')': case ']':
            if (t->img->t.close_paren_comma) buf[0] = ',';
            else if (buf[0] == ')')          buf[0] = '}';
            break;
        case '?':
            buf[0] = (uint8_t)(t->quest ? '.' : '?');
            t->run = 0; t->quest = 0; break;
        case '{': case '}':
            t->run = 0; t->quest = 0; break;
        default: break;
        }
        len = 1;
        t->tok[i].flag = 1;
        t->tok[i].fromtext = (uint8_t)t->markreal;
        t->tok[i].raw = (uint8_t)t->rawmark;
        t->markreal = 0;
        t->tok[i].pushback = 1;
        t->blocked |= 1;
        break;
    case 5:
        len = 6;
        if (buf[0] > 0x4C) { t->tok[i].pushback = 1; t->blocked |= 1; }
        break;
    case 6:
        len = 0;
        t->tok[i].flag = 1;
        t->tok[i].pushback = 1;
        t->blocked |= 1;
        break;
    default:
        len = 0;
        break;
    }
    if (len > (int)sizeof t->tok[i].buf) len = (int)sizeof t->tok[i].buf;
    t->tok[i].len = (uint8_t)len;
    memcpy(t->tok[i].buf, buf, (size_t)len);
    t->bytes += len;

    if (kind == 3 && t->total > t->breath) split(t);
}

static int can_read(const bst_tok *t) {
    if (t->blocked & 1) return 0;
    return t->held < 20 && t->bytes < 200;
}

/* ---- the front ---------------------------------------------------------- */

void bst_tok_init(bst_tok *t, const bst_image *img, const char *text) {
    memset(t, 0, sizeof *t);
    t->img = img;
    if (img->t.xlat_kind && bst_translit(img, text, t->xbuf, sizeof t->xbuf) >= 0) {
        text = t->xbuf;
        if (bst_trace) bst_tracef("xlat '%s'\n", t->xbuf);
    }
    t->text = (const uint8_t *)text;
    t->tn = (int)strlen(text);
    t->cur = -1;
    t->textend = -1;
    t->start = 0;
    t->lastout = ' ';
    t->textmode = 1;
    t->breath = 0x3C;
    t->window = 0x14;
}

static int token(bst_tok *t, uint8_t *buf) {
    for (;;) {
        int cls;
        int b = classify(t, &cls);
        if (!(cls & 0x100)) continue;
        switch (cls) {
        case 0x101:
            return 6;
        case 0x111: {
            /* A run of letters and apostrophes: one word. */
            int n = 0;
            /* A build that writes its words without spaces joins the run
               across them. */
            int join = t->img->t.join_words;
            do {
                if (cls == 0x111) {
                    if (n < 99) buf[n + 1] = (uint8_t)b;
                    n++;
                    /* The Japanese build writes this pair long. */
                    if (t->img->t.long_ku && b == 'u' && n > 1 &&
                        buf[n - 1] == 'k') {
                        if (n < 99) buf[n + 1] = 'u';
                        n++;
                    }
                }
                b = classify(t, &cls);
            } while (b != 0xFFFF && (cls == 0x111 || (join && cls == 0x13)));
            if (b != 0xFFFF) t->pos--;
            buf[n + 1] = 0;
            buf[0] = 0;
            return 2;
        }
        case 0x112:
            buf[0] = (uint8_t)b;
            return 4;
        case 0x124: {
            /* A run of phoneme codes the machine wrote outright. */
            bst_builder w;
            bst_build_init(&w);
            do {
                bst_build_emit(t->img, &w, b);
                b = classify(t, &cls);
            } while (b != 0xFFFF && (cls & 0x20));
            if (b != 0xFFFF) t->pos--;
            buf[0] = (uint8_t)w.pos;
            memcpy(buf + 1, w.buf + 1, (size_t)w.pos + 1);
            /* A token that opens with a marker repeats it in the flags byte,
               which is where the assembler looks for the emphasis it carries. */
            if (!(bst_ph_attr2(t->img, buf[2]) & 0x10))       buf[1] = 0;
            else if (buf[2] == 'X' && (bst_ph_attr2(t->img, buf[3]) & 0x10))
                                                              buf[1] = buf[3];
            else                                              buf[1] = buf[2];
            return 3;
        }
        case 0x146:
            buf[0] = (uint8_t)b;
            return 5;
        case 0x189:
            /* A command: the marker, then the letter and four bytes. The
               record the later passes read is one longer than that, and its
               last byte is always missing from the text. */
            memset(buf, 0, 6);
            for (int i = 0; i < 5; i++) buf[i] = (uint8_t)classify(t, &cls);
            return 1;
        default:
            break;
        }
    }
}

int bst_tok_next(bst_tok *t, uint8_t *buf) {
    while (can_read(t)) produce(t);
    if (!t->held) return 6;

    int i = t->rd;
    int pb = t->tok[i].pushback;
    if (pb) {
        t->tok[i].pushback = 0;
        t->blocked &= ~1;
        if (pb != 1) { buf[0] = (uint8_t)pb; return 4; }
    }
    memcpy(buf, t->tok[i].buf, (size_t)t->tok[i].len);
    t->bytes -= t->tok[i].len;
    t->held--;
    t->rd = (t->rd + 1) % 20;
    if (t->tok[i].type == 3)
        t->total -= t->tok[i].flag ? t->tok[i].len - 4 : t->tok[i].len;
    if (t->tok[i].type == 4) {
        t->endreal = t->tok[i].fromtext;
        t->endmark = t->tok[i].raw;
    }
    return t->tok[i].type;
}
