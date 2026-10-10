/*
 * The TextIn tokenizer.
 *
 * TextIn sits between Engine_Feed and the preformatter when the SAPI
 * "TextIn" option is on, which it is by default.  It pulls characters out of
 * the input ring itself, splits them into tokens, rewrites some of them --
 * numbers, abbreviations -- and puts the result back through
 * Preformat_PutChar.
 *
 * The file runs outward from the middle: first the edges, the two ways it
 * reads a character and the string allocator its tokens use, then the list
 * and its lifecycle, then the tokenizer proper -- `TextIn_ReadToken`, which
 * decides where a token ends, `TextIn_Split`, which cuts one up where the
 * marks say to, and `TextIn_TokenizeText`, which is the same walk over a
 * string.  Mode 4, the mail reader, is last.
 */
#include "es_engine.h"

/* @0x1001dc10 */
int32_t TV_THISCALL TextIn_GetChar(TextIn *self)
{
    int32_t c;
    if (self->engine == NULL)
        return -1;
    c = Engine_InGet(self->engine);
    if (c == -1) {
        self->engine->st_input_empty = 1;
        self->input_done = 1;
        return -2;
    }
    return c;
}

/* @0x1001dc50 */
int32_t TV_THISCALL TextIn_Unget(TextIn *self)
{
    if (self->engine == NULL)
        return -1;
    return Engine_InUnget(self->engine);
}

/* Feed a string to the preformatter a character at a time, and wake the
 * engine if anything went in. */
/* @0x1001dc70 */
int32_t TV_THISCALL TextIn_PutString(TextIn *self, const char *s)
{
    int32_t n = (int32_t)strlen(s);
    int32_t i;

    for (i = 0; i < n; i++)
        Preformat_PutChar(self->engine, (uint8_t)s[i]);
    if (i > 0)
        self->engine->st_idle = 0;
    return 0;
}

/* Replace the string held in *p with room for n characters.  A length of
 * zero frees without allocating, and the caller is left with NULL. */
/* @0x1001dbc0 */
int32_t TV_CDECL AllocString(char **p, int32_t n)
{
    char *s;

    if (*p != NULL)
        tv_free(*p);
    *p = NULL;
    if (n == 0)
        return 1;
    s = (char *)tv_malloc((size_t)(n + 1));
    if (s == NULL)
        return -1;
    *p = s;
    return 1;
}

/* Take one token off the list and free everything hanging off it.  dir picks
 * which neighbour to hand back: -1 for the one before, anything else for the
 * one after. */
/* @0x1001d960 */
Token *TV_THISCALL TextIn_RemoveToken(TextIn *self, Token *t, int32_t dir)
{
    Token *ret;

    if (t == NULL)
        return NULL;
    ret = dir == -1 ? t->prev : t->next;
    /* English updates self->head when there is no previous token; this one
     * treats that as a caller error and gives up, so the list it works on
     * always has something in front. */
    if (t->prev == NULL) {
        TextIn_Error(self, 0);
        return NULL;
    }
    t->prev->next = t->next;
    if (t->next != NULL)
        t->next->prev = t->prev;
    if (t->text != NULL)
        tv_free(t->text);
    if (t->text2 != NULL)
        tv_free(t->text2);
    if (t->types != NULL)
        tv_free(t->types);
    tv_free(t);
    self->count--;
    return ret;
}

/* @0x1001d740 */
Token *TV_THISCALL TextIn_InsertAfter(TextIn *self, Token *ref)
{
    Token *t = (Token *)tv_malloc(sizeof(Token));

    t->prev = ref;
    if (ref == NULL) {
        /* the same refusal as TextIn_RemoveToken, and the token just
         * allocated is left where it is -- the original does not free it */
        TextIn_Error(self, 0);
        return NULL;
    }
    t->next = ref->next;
    ref->next = t;
    if (t->next != NULL)
        t->next->prev = t;
    if (self->tail == ref)
        self->tail = t;

    t->w08 = 0;
    t->w0a = 0;
    t->bits[0] = t->bits[1] = t->bits[2] = 0;
    t->d18 = NULL;
    t->d1c = 0;
    t->text = NULL;
    t->len = 0;
    t->text2 = NULL;
    t->types = NULL;
    t->trail = 0;
    t->is_number = 0;
    t->num = 0;
    t->w34 = 1;

    self->count++;
    return t;
}

/* Read tokens until the list is full or the input runs out.  In mode 4 a
 * token whose predecessor carries bit 0x53 gets special handling; English
 * tests bit 0x45 for the same thing.
 *
 * That 0x53 is read straight out of the disassembly ("push 0x53") and is not
 * covered by any test.  Bit 83 is set by TextIn_ReadToken and TextIn_Split,
 * neither of which is written yet, and nothing in tests/corpus_es produces a
 * token carrying it -- putting 0x45 here instead passes all 205
 * configurations even with -M 4.  It becomes testable once the tokenizer
 * proper is decompiled and it is possible to say which tokens get the bit. */
/* @0x1001c950 */
int32_t TV_THISCALL TextIn_Tokenize(TextIn *self)
{
    int32_t n = 0;
    int32_t room = 210 - self->count;
    Token *t;

    if (room < 1)
        return 1;
    while (room > n) {
        if (TextIn_ReadToken(self, &t) != 1)
            break;
        n++;
        TextIn_Split(self, &t);
        if (self->mode == 4 && t->prev != NULL && Bits_Test(0x53, t->prev->bits))
            TextIn_Mode4(self, t);
    }
    return n;
}

/* @0x1001c8a0 */
int32_t TV_THISCALL TextIn_Flush(TextIn *self, int32_t final)
{
    int i;

    if (final == 0) {
        TextIn_Tokenize(self);
        if (self->input_done && self->item_done) {
            while (TextIn_Advance(self))
                ;
            TextIn_Emit(self, 1);
            return 1;
        }
        for (i = 10; i != 0; i--)
            TextIn_Advance(self);
        TextIn_Emit(self, 0);
        return 10;
    }
    while (TextIn_Tokenize(self) > 0) {
        for (i = 10; i != 0; i--)
            TextIn_Advance(self);
        TextIn_Emit(self, 0);
    }
    while (TextIn_Advance(self))
        ;
    TextIn_Emit(self, 1);
    return 1;
}

/* A stub that takes a token and returns success without touching it.  Both
 * call sites are in TextIn_Split, on the path where the rule lookup did not
 * match the token, so it is named for where it sits rather than for what it
 * did before the shipping build compiled it away to "mov eax,1; ret 4".
 * Engine_Trace and Engine_Error went the same way; this is the third. */
/* @0x1001fdd0 */
int32_t TV_THISCALL TextIn_Unmatched(TextIn *self, Token *t)
{
    (void)self;
    (void)t;
    return 1;
}

/*
 * Insert a fresh token in front of a reference token.  The mirror of
 * TextIn_InsertAfter, with the same fields initialised and the same
 * refusal -- there is nothing in front of the head node, so it gives up
 * rather than growing the list past it.
 *
 * It gives up too late, though.  By the time it decides the reference has
 * no predecessor it has already written "ref->prev = t", so the refused
 * token is left linked in front of ref with a NULL prev, where nothing owns
 * it and self->head does not know about it, and the token itself is never
 * freed.  TextIn_InsertAfter refuses before it links, which is why the note
 * there only has to mention the leak.  Nothing in the corpus reaches either
 * refusal; both are reachable only through a caller that has already lost
 * track of the head.
 */
/* @0x1001d7c0 */
Token *TV_THISCALL TextIn_InsertBefore(TextIn *self, Token *ref)
{
    Token *t = (Token *)tv_malloc(sizeof(Token));

    t->next = ref;
    t->prev = NULL;
    if (ref == NULL) {
        TextIn_Error(self, 0);
        return NULL;
    }
    t->prev = ref->prev;
    ref->prev = t;
    if (t->prev == NULL) {
        TextIn_Error(self, 0);
        return NULL;
    }
    t->prev->next = t;

    t->w08 = 0;
    t->w0a = 0;
    t->bits[0] = t->bits[1] = t->bits[2] = 0;
    t->d18 = NULL;
    t->d1c = 0;
    t->text = NULL;
    t->len = 0;
    t->text2 = NULL;
    t->types = NULL;
    t->trail = 0;
    t->is_number = 0;
    t->num = 0;
    t->w34 = 1;

    self->count++;
    return t;
}

/*
 * Put back the token TextIn_Detach lifted out, beside a reference token.
 *
 * This is the other half of TextIn.detached, and between them they are how a
 * rule moves a token: opcode 74 lifts it, opcodes 75 and 76 drop it in
 * before or after somewhere else.  The slot holds one token and is cleared
 * on the way out, so a second reattach without a detach in between does
 * nothing and answers NULL.
 *
 * Inserting before has the same late refusal as TextIn_InsertBefore -- it
 * writes ref->prev before it decides the reference had no predecessor --
 * except that here the token it leaves behind was already in the list once.
 *
 * Inserting after sets TextIn.cur when the token lands at the end, where
 * TextIn_InsertAfter sets TextIn.tail in the same situation.  The two are
 * different fields, four bytes apart, and the asymmetry is the original's.
 */
/* @0x1001d850 */
Token *TV_THISCALL TextIn_Reattach(TextIn *self, Token *ref, int32_t dir)
{
    Token *t = self->detached;

    if (t == NULL || ref == NULL)
        return NULL;
    self->detached = NULL;
    if (dir == -1) {
        t->next = ref;
        t->prev = NULL;
        t->prev = ref->prev;
        ref->prev = t;
        if (t->prev == NULL) {
            TextIn_Error(self, 0);
            return NULL;
        }
        t->prev->next = t;
        self->count++;
        return t;
    }
    t->prev = ref;
    t->next = NULL;
    t->next = ref->next;
    ref->next = t;
    if (t->next == NULL) {
        self->cur = t;
        self->count++;
        return t;
    }
    t->next->prev = t;
    self->count++;
    return t;
}

/*
 * The TextIn object's construction, reset and one-token advance.
 *
 * TextIn_Construct is where the abbreviation index gets built, the first
 * time an engine is asked for a tokenizer.  It sets the list up as a single
 * head node with everything in it cleared, points head, cur and tail at that
 * node, and takes the mode the SAPI object was carrying.
 *
 * TextIn_Reset does the same to an object that already exists, minus the
 * head node's own fields, and then calls sub_10022970 when the mode is 4 --
 * the one arm of it nothing in the corpus reaches.
 *
 * TextIn_Advance moves cur on by one token and decides what the new one
 * needs.  A token that already has a value in d1c is taken as finished; so
 * is one carrying flag 0x52 when ti_04 is 1.  Anything else goes to the rule
 * runner, which is what eventually reaches Rule_Eval.
 */
/* @0x1001c790 */
TextIn *TV_THISCALL TextIn_Construct(TextIn *self, int32_t mode)
{
    Token *h;
    int i;

    Abbrev_Init();
    self->head = &self->head_node;
    self->head->prev = NULL;
    h = self->head;
    h->next = h->prev;
    self->head->w0a = 0;
    h = self->head;
    h->w08 = h->w0a;
    for (i = 0; i < 3; i++)
        self->head->bits[i] = 0;
    self->head->d18 = NULL;
    self->head->d1c = 0;
    self->head->text2 = NULL;
    self->head->text = self->head->text2;
    self->head->len = 0;
    self->head->types = NULL;
    self->head->trail = 0;
    self->head->w32 = 0;
    self->head->w34 = 0;
    self->head->is_number = 0;
    self->head->num = 0;

    self->ti_6e = 0;
    self->detached = NULL;
    self->cur = self->head;
    self->tail = self->head;
    self->ti_74[0] = 0;
    self->count = 0;
    self->err_count = 0;
    self->mode = mode;
    self->ti_04 = 0;
    return self;
}

/* @0x1001c850 */
int32_t TV_THISCALL TextIn_Reset(TextIn *self)
{
    Token *h;

    self->detached = NULL;
    self->head = &self->head_node;
    self->head->prev = NULL;
    h = self->head;
    h->next = h->prev;
    self->ti_6e = 0;
    self->cur = self->head;
    self->tail = self->head;
    self->count = 0;
    self->ti_74[0] = 0;
    self->err_count = 0;
    if (self->mode == 4)
        TextIn_Mode4Reset(self);
    return 1;
}

/* @0x1001e0d0 */
int32_t TV_THISCALL TextIn_Advance(TextIn *self)
{
    Token *t;

    t = self->head == self->cur ? self->head->next : self->cur->next;
    if (t == NULL)
        return 0;
    if (self->ti_04 == 1 && !Bits_Test(0x52, t->bits)) {
        self->cur = t;
        return 1;
    }
    if (t->d1c != 0) {
        self->cur = t;
        return 1;
    }
    Rule_Run(self, &t);
    self->cur = t;
    return 1;
}

/*
 * Give the engine a tokenizer.
 *
 * The mode comes from the SAPI object when there is one and is zero when the
 * engine is standalone, which is the one place the tokenizer's behaviour
 * depends on the host.  A failed allocation is reported rather than
 * crashed on, and leaves the engine without a tokenizer -- Engine_Flush
 * tests for that before it uses one.
 */
/* @0x1001c6c0 */
uint8_t TV_THISCALL Engine_CreateTextIn(Engine *self)
{
    int32_t mode = 0;
    TextIn *t;

    if (self->sapi != NULL)
        mode = self->sapi->textin_mode;
    t = (TextIn *)tv_new(sizeof(TextIn));
    if (t != NULL)
        t = TextIn_Construct(t, mode);
    if (t == NULL)
        return 0;
    t->engine = self;
    self->textin = t;
    return 1;
}

/*
 * Hand the finished tokens back to the engine and drop them.
 *
 * Each token's text goes out through TextIn_PutString -- the replacement text
 * if the rules left one, otherwise the original -- followed by the character
 * that ended it, and then the token is removed from the list.  So this is
 * where a rewritten token stops being a token and becomes characters again.
 *
 * `final` says how much to let go of.  Set, everything from the head to the
 * end goes.  Clear, the cursor is walked back up to a hundred tokens (or to
 * the one after the head, whichever comes first) and the walk stops there, so
 * that much stays in the list for the rules to keep looking at.
 */
/* @0x1001f850 */
int32_t TV_THISCALL TextIn_Emit(TextIn *self, int32_t final)
{
    Token *head = self->head;
    Token *t = head->next;
    Token *stop = self->cur;

    if (t == NULL || stop == NULL)
        return 0;

    if (final == 0) {
        int32_t back = 0;

        if (stop->prev != head) {
            while (back < 100) {
                stop = stop->prev;
                back++;
                if (stop->prev == head)
                    break;
            }
        }
        while (t != NULL && t != stop) {
            const char *s = t->text2 != NULL ? t->text2 : t->text;
            char trail[2];

            if (s != NULL)
                TextIn_PutString(self, s);
            if (t->trail != 0) {
                trail[0] = (char)t->trail;
                trail[1] = 0;
                TextIn_PutString(self, trail);
            }
            t = TextIn_RemoveToken(self, t, 1);
        }
        return 1;
    }

    while (t != NULL) {
        const char *s = t->text2 != NULL ? t->text2 : t->text;
        char trail[2];

        if (s != NULL)
            TextIn_PutString(self, s);
        if (t->trail != 0) {
            trail[0] = (char)t->trail;
            trail[1] = 0;
            TextIn_PutString(self, trail);
        }
        t = TextIn_RemoveToken(self, t, 1);
    }
    return 1;
}

/* The tokenizer's own character table, one dword each, and not the same table
 * as the rule interpreter's in lang/spa/engine/rule.c: bit 1 is punctuation, bit 2 one of
 * the three separators -./, bit 4 a letter -- which is what ends a CSI
 * sequence -- bit 8 a digit and bit 0x10 one of # $ % & @ ` ~. */
/* @0x10061450 */
extern const uint32_t g_tok_class[0x100];
/* The two bytes a CSI sequence starts with, as one int16: ESC and then a
 * zero the '[' overwrites. */
/* @0x1006185c */
extern const uint16_t g_esc_prefix;

/*
 * Read one ESC[ sequence and make a token of it.
 *
 * The characters come either from the tokenizer's own input, or -- when `base`
 * is not NULL -- from `base` at the position `*ppos`, which is walked forward
 * as it goes and back again if the sequence turns out not to be one.  The
 * sequence is ESC, '[', any number of parameter bytes and then a letter;
 * twenty characters is the limit.
 *
 * On success the whole sequence becomes a new token on the end of the list,
 * flagged 0x51 and 0x52, with its length in Token.len.  Anything that is not a
 * sequence after all -- no '[', or the input running out -- is put back, a
 * character at a time, and nothing is made.
 *
 * ESC[4X is the one sequence handled here rather than passed on: it switches
 * the tokenizer to mode 4 and resets it, unless `no_mode4` says not to.
 */
/* @0x1001d090 */
int32_t TV_THISCALL TextIn_ReadEscape(TextIn *self, const char *base,
                                      int32_t *ppos, int32_t no_mode4)
{
    char buf[0x18];
    Token *tok;
    int32_t state = 1;
    int32_t n = 1;

    buf[0] = (char)(g_esc_prefix & 0xff);
    buf[1] = (char)(g_esc_prefix >> 8);
    memset(buf + 2, 0, 0x12);

    while (state < 3) {
        int32_t c;

        if (base != NULL) {
            c = (int32_t)(int8_t)base[*ppos];
            (*ppos)++;
        } else {
            c = TextIn_GetChar(self);
        }
        buf[n] = (char)c;
        n++;
        if (c == -2 || c == 0)
            goto putback;
        if (state == 1) {
            if (c != '[')
                goto putback;
            state = 2;
        } else if (state == 2) {
            if (g_tok_class[c] & 4)
                state = (c == 'X' && buf[n - 2] == '4') ? 4 : 3;
            if (n > 0x14)
                goto putback;
        }
    }

    if (state == 4) {
        if (no_mode4 == 0) {
            self->mode = 4;
            TextIn_Mode4Reset(self);
        }
        return 0;
    }

    buf[n] = 0;
    tok = TextIn_InsertAfter(self, self->tail);
    if (tok == NULL)
        return TextIn_Error(self, 1);
    if (AllocString(&tok->text, n + 1) == -1)
        return TextIn_Error(self, 0);
    strcpy(tok->text, buf);
    tok->len = (int16_t)n;
    Bits_Set(0x51, tok->bits);
    Bits_Set(0x52, tok->bits);
    /* the original tests the state against 4 again here, which by now it
     * cannot be */
    return 0;

putback:
    {
        int32_t k = n;

        n--;
        if (k > 1) {
            do {
                k = n;
                if (base != NULL)
                    (*ppos)--;
                else
                    TextIn_Unget(self);
                n--;
            } while (k > 1);
        }
    }
    return 0;
}

/*
 * One line of input into a buffer.
 *
 * Characters up to 0xff of them, stopping at end of input, a NUL or a newline;
 * a carriage return is dropped, and an ESC is handed to TextIn_ReadEscape so
 * that a sequence in the middle of a line does not end up in the buffer.  The
 * index of the first space goes to `first_space`, or -1 when there is none,
 * and the length comes back.
 */
/* @0x100230b0 */
int32_t TV_THISCALL TextIn_ReadLine(TextIn *self, char *buf,
                                    int32_t *first_space)
{
    int32_t n = 0;

    *first_space = -1;
    for (;;) {
        int32_t c = TextIn_GetChar(self);

        if (c == -2 || c == 0)
            break;
        if (c == 0x1b) {
            TextIn_ReadEscape(self, NULL, NULL, 1);
            continue;
        }
        if (c == '\r')
            continue;
        if (c == '\n')
            break;
        if (*first_space == -1 && c == ' ')
            *first_space = n;
        buf[n++] = (char)c;
        if (n >= 0xff)
            break;
    }
    buf[n] = 0;
    return n;
}

/*
 * Cut a token into several where its split marks say to.
 *
 * Every token is classified first.  One that has no marks -- Token.types is
 * where Rule_Run's own opcodes put them -- or that turned out to be an
 * abbreviation, or that carries flag 7 for a dotted abbreviation, is left
 * whole; a token with no marks and no abbreviation record goes to
 * TextIn_Unmatched instead, which is how the rule interpreter hears about a
 * word it could not place.
 *
 * Otherwise the text is walked and a mark of anything other than 1 ends the
 * piece at hand: a new token is inserted before this one and gets the
 * characters collected so far, with the character at the mark as its trailing
 * character -- unless the mark is 2, which takes no trailing character and
 * leaves the character for the next piece.  The last piece stays in the
 * original token, the marks are freed, and then every new piece and the
 * original are classified again, in the order they will be spoken.
 */
/* @0x1001d260 */
int32_t TV_THISCALL TextIn_Split(TextIn *self, Token **tp)
{
    char buf[0x68];
    Token *t = *tp;
    Token *first = NULL;
    int32_t n = 0;
    int32_t i;

    Rule_ClassifyToken(self, t);
    if (t->w34 == 0)
        return 1;
    if (Rule_MatchAbbrev(t) == 1)
        return 1;
    if (t->types == NULL) {
        TextIn_Unmatched(self, t);
        return 1;
    }
    if (Bits_Test(7, t->bits))
        return 1;

    for (i = 0; i < (int32_t)t->len; ) {
        if (t->types[i] != 1) {
            Token *tok = TextIn_InsertBefore(self, t);

            if (tok == NULL)
                return TextIn_Error(self, 0);
            if (first == NULL)
                first = tok;
            if (AllocString(&tok->text, n + 5) == -1)
                return TextIn_Error(self, 0);
            if (t->types[i] == 2) {
                tok->trail = 0;
            } else {
                tok->trail = (uint8_t)t->text[i];
                i++;
                t->types[i] = 1;
            }
            buf[n] = 0;
            strcpy(tok->text, buf);
            tok->len = (int16_t)n;
            n = 0;
        }
        buf[n] = t->text[i];
        i++;
        n++;
    }

    if (first == NULL)
        return 1;
    buf[n] = 0;
    strcpy(t->text, buf);
    t->len = (int16_t)n;
    tv_free(t->types);
    t->types = NULL;
    for (;;) {
        Rule_ClassifyToken(self, t);
        if (Rule_MatchAbbrev(t) == 0)
            TextIn_Unmatched(self, t);
        if (first == t)
            break;
        t = t->prev;
    }
    return 1;
}

/*
 * Read one token from the input.
 *
 * A hundred characters at most.  A NUL or a carriage return counts as a space;
 * an ESC begins a control sequence, which TextIn_ReadEscape takes over when it
 * is the first character of the token and otherwise ends the token; a newline
 * is a token of its own.  Whitespace ends a token, and so does any character
 * that cannot follow what came before: punctuation after a letter or a digit, a
 * digit after a leading space run, a letter after one, a digit right after
 * punctuation.  The character that ended the token becomes the token's trailing
 * character, and when it was a letter, a digit or one of # $ % & @ ` ~ it is
 * pushed back instead so that the next token starts with it.
 *
 * Three marks come out of the walk, one per character, for TextIn_Split to
 * divide the token on later: 1 for "no split", 2 where a letter and a digit
 * meet in either order, and 3 for one of - . / between two letters.  The marks
 * are only kept when at least one of them is not 1.
 *
 * Trailing punctuation is given back to the input a character at a time, so
 * "casa." leaves the full stop to be read as its own token.  The loop stops at
 * the first character that is not punctuation, which it can rely on being
 * there: a token of nothing but punctuation has every character counted in the
 * leading run and the trimming is skipped entirely.
 *
 * All digits and nothing else makes the token a number, with its value in
 * Token.num.  Returns 0 at end of input with nothing read, otherwise 1 with
 * the new token in *out.
 */
/* @0x1001c9e0 */
int32_t TV_THISCALL TextIn_ReadToken(TextIn *self, Token **out)
{
    char buf[0x65];
    uint8_t marks[0x64];
    Token *tok;
    uint32_t cls = 0;          /* the class of the character in hand */
    uint32_t prevcls = 0;      /* and of the one before it */
    int32_t c = 0;
    int32_t n = 0;             /* characters collected */
    int32_t lead = 0;          /* leading punctuation */
    int32_t nmarks = 0;        /* marks other than 1 */
    int32_t seen_digit = 0, seen_letter = 0;
    int32_t i;

    memset(buf, 0, sizeof buf);
    for (;;) {
        if (n >= 0x64) {
            c = 0;
            cls = 0;
            break;
        }
        c = TextIn_GetChar(self);
        if (c == -2) {
            c = 0;
            if (n == 0)
                return 0;
            cls = 0;
            break;
        }
        if (c == 0x1b) {
            if (n != 0) {
                cls = g_tok_class[c];
                break;
            }
            TextIn_ReadEscape(self, NULL, NULL, 0);
            continue;
        }
        if (c == 0 || c == 0x0d)
            c = ' ';
        cls = g_tok_class[c];
        marks[n] = 1;
        if (c == '\n') {
            if (n != 0)
                break;
            buf[n] = '\n';
            n++;
            c = 0;
            cls = 0;
            break;
        }
        if (cls & 0x20)                     /* whitespace ends it */
            break;
        if ((cls & 2) && seen_letter != 0) {
            /* one of - . / between two letters: a mark, not a break */
            if (prevcls != 4)
                break;
            marks[n] = 3;
            nmarks++;
        } else if (cls & 1) {
            if (seen_digit != 0 || seen_letter != 0)
                break;
            lead++;
        } else if (cls & 8) {
            if (n == 0) {
                seen_digit = 1;
            } else {
                if (lead != 0)
                    break;
                if (prevcls & 1)
                    break;
            }
            if (prevcls == 4) {
                marks[n] = 2;
                nmarks++;
            }
            seen_letter = 0;
        } else if (cls & 4) {
            if (n == 0) {
                seen_letter = 1;
            } else if (lead != 0) {
                break;
            }
            if (prevcls == 8) {
                marks[n] = 2;
                nmarks++;
            }
            seen_digit = 0;
        }
        buf[n] = (char)c;
        n++;
        prevcls = cls;
    }

    /* Trailing punctuation goes back into the input.  The n > 0 guard is this
     * translation's: the original reads the byte before the buffer when the
     * whole of it is punctuation, and cannot reach that because such a token
     * has lead == n and never enters the loop at all. */
    if (n > lead && (g_tok_class[(uint8_t)buf[n - 1]] & 1)) {
        do {
            if (c != 0)
                TextIn_Unget(self);
            n--;
            c = (uint8_t)buf[n];
            buf[n] = 0;
            cls = g_tok_class[c];
        } while (n > 0 && (g_tok_class[(uint8_t)buf[n - 1]] & 1));
    }

    buf[n] = 0;
    tok = TextIn_InsertAfter(self, self->tail);
    if (tok == NULL)
        return TextIn_Error(self, 1);
    if (n > 0) {
        if (AllocString(&tok->text, n + 1) == -1)
            return TextIn_Error(self, 0);
        strcpy(tok->text, buf);
    }
    tok->len = (int16_t)n;
    if (cls & 0x1c) {
        /* a letter, a digit or one of # $ % & @ ` ~ ended the token: the next
         * one starts with it */
        c = 0;
        TextIn_Unget(self);
    }
    tok->trail = (uint8_t)c;
    if (seen_digit != 0) {
        tok->num = tv_atol(buf);
        tok->w34 = 0;
        tok->is_number = 1;
    } else {
        if (nmarks != 0) {
            if (AllocString(&tok->types, n + 1) == -1)
                return TextIn_Error(self, 0);
            for (i = 0; i < n; i++)
                tok->types[i] = (char)marks[i];
            tok->types[n] = 1;
        }
        tok->w34 = 1;
        tok->is_number = 0;
    }
    *out = tok;
    return 1;
}

/*
 * Tokenize a string, the way TextIn_ReadToken tokenizes the input.
 *
 * Character for character the same walk, with the same marks and the same
 * rules about what ends a token; only where the characters come from differs,
 * and so does what "putting one back" means -- the position in the string goes
 * back by one instead of a character going back into the input ring.  Each
 * token is split as it is made, rather than by the caller, and when `mark` is
 * set every one of them also gets flag 3.
 *
 * Used by the mode 4 machinery, which has text of its own to turn into tokens.
 * Always returns 1 unless a token could not be made.
 */
/* @0x1001cd10 */
int32_t TV_THISCALL TextIn_TokenizeText(TextIn *self, const char *text,
                                        int32_t mark)
{
    char buf[0x65];
    uint8_t marks[0x64];
    int32_t pos = 0;
    int32_t len = (int32_t)strlen(text);

    if (len <= 0)
        return 1;

    do {
        Token *tok;
        uint32_t cls = 0;
        uint32_t prevcls = 0;
        int32_t c = 0;
        int32_t n = 0;
        int32_t lead = 0;
        int32_t nmarks = 0;
        int32_t seen_digit = 0, seen_letter = 0;
        int32_t i;

        memset(buf, 0, sizeof buf);
        for (;;) {
            c = (uint8_t)text[pos];
            pos++;
            if (c == 0x1b) {
                if (n != 0) {
                    c = 0;
                    pos--;
                    cls = 0;
                    break;
                }
                TextIn_ReadEscape(self, text, &pos, 1);
                continue;
            }
            if (c == 0 || c == 0x0d)
                c = ' ';
            cls = g_tok_class[c];
            marks[n] = 1;
            if (c == '\n') {
                if (n != 0)
                    break;
                n++;
                c = 0;
                cls = 0;
                buf[n - 1] = '\n';
                break;
            }
            if (cls & 0x20)
                break;
            if ((cls & 2) && seen_letter != 0) {
                if (prevcls != 4)
                    break;
                marks[n] = 3;
                nmarks++;
            } else if (cls & 1) {
                if (seen_digit != 0 || seen_letter != 0)
                    break;
                lead++;
            } else if (cls & 8) {
                if (n == 0) {
                    seen_digit = 1;
                } else {
                    if (lead != 0)
                        break;
                    if (prevcls & 1)
                        break;
                }
                if (prevcls == 4) {
                    marks[n] = 2;
                    nmarks++;
                }
                seen_letter = 0;
            } else if (cls & 4) {
                if (n == 0) {
                    seen_letter = 1;
                } else if (lead != 0) {
                    break;
                }
                if (prevcls == 8) {
                    marks[n] = 2;
                    nmarks++;
                }
                seen_digit = 0;
            }
            buf[n] = (char)c;
            n++;
            prevcls = cls;
            if (n >= 0x64)
                break;
        }

        /* trailing punctuation goes back to the string; the n > 0 guard is
         * this translation's, as in TextIn_ReadToken */
        if (lead < n && (g_tok_class[(uint8_t)buf[n - 1]] & 1)) {
            do {
                if (c != 0)
                    pos--;
                n--;
                c = (uint8_t)buf[n];
                buf[n] = 0;
                cls = g_tok_class[c];
            } while (n > 0 && (g_tok_class[(uint8_t)buf[n - 1]] & 1));
        }

        buf[n] = 0;
        tok = TextIn_InsertAfter(self, self->tail);
        if (tok == NULL)
            return TextIn_Error(self, 1);
        if (n > 0) {
            if (AllocString(&tok->text, n + 1) == -1)
                return TextIn_Error(self, 0);
            strcpy(tok->text, buf);
        }
        tok->len = (int16_t)n;
        if (cls & 0x1c) {
            c = 0;
            pos--;
        }
        tok->trail = (uint8_t)c;
        if (seen_digit != 0) {
            tok->num = tv_atol(buf);
            tok->w34 = 0;
            tok->is_number = 1;
        } else {
            if (nmarks != 0) {
                if (AllocString(&tok->types, n + 1) == -1)
                    return TextIn_Error(self, 0);
                for (i = 0; i < n; i++)
                    tok->types[i] = (char)marks[i];
                tok->types[n] = 1;
            }
            tok->w34 = 1;
            tok->is_number = 0;
        }
        TextIn_Split(self, &tok);
        if (mark != 0)
            Bits_Set(3, tok->bits);
    } while (len > pos);
    return 1;
}

/* The characters mode 4 treats as the marker that brackets inserted text, and
 * the two announcements it makes around it.  Both are in German: the strings
 * were never translated, so a Spanish voice in mode 4 says "Achtung: Anfang
 * des eingesetzten textes".  Neither is reachable through the SAPI interface
 * this build offers, which is why nothing in the corpus hears them. */
/* @0x10061850 */
extern const char g_str_mode4_marks[];   /* "!@#%*+|><:" */
/* @0x10069c38 */
extern const char g_str_mode4_start[];   /* ". Achtung: Anfang des ..." */
/* @0x10069c68 */
extern const char g_str_mode4_end[];     /* ". Achtung: Ende des ..." */

/*
 * Put a string into the list as a token's replacement text.
 *
 * A new token goes before the one given when `dir` is -1 and after it
 * otherwise, and the string becomes its text2 -- the replacement the emitter
 * speaks in place of text -- with a space for its trailing character.
 */
/* @0x1001d8d0 */
int32_t TV_THISCALL TextIn_InsertText(TextIn *self, Token *ref,
                                      const char *text, int32_t dir)
{
    Token *tok = dir == -1 ? TextIn_InsertBefore(self, ref)
                           : TextIn_InsertAfter(self, ref);

    if (AllocString(&tok->text2, (int32_t)strlen(text) + 1) == -1)
        return TextIn_Error(self, 0);
    strcpy(tok->text2, text);
    tok->trail = ' ';
    return 1;
}

/*
 * Mode 4: find the run of marker characters that brackets inserted text.
 *
 * Mode 4 is for text quoted from somewhere else, marked out by a run of one of
 * ! @ # % * + | > < : at the start of every line.  This is called once per
 * token and its job is to recognise that run and, once it is sure, silence it
 * and announce the quoted passage.
 *
 * The first five characters of the token -- and its trailing character too when
 * it is shorter than that -- are compared against the run remembered from the
 * token before, in ti_74, and counted.  A marker character whose position is
 * one less than the number of matches so far continues the remembered run, and
 * the last marker character at any position ends it.  With no marker at all the
 * remembered run is forgotten; with one, either the run carries on -- and ti_70
 * is how many characters of it there are -- or a new one is remembered in its
 * place.
 *
 * Then, if the quotation is not open yet, a second token carrying the run opens
 * it: up to three tokens back to the last one flagged 0x53 are silenced, either
 * by flagging them 2 and 0x51 when the run is all they hold or by blanking the
 * run's characters out of their text, and the "Anfang" announcement goes in
 * front.  While it is open, a token with the run has the same done to it, and a
 * token without one closes the quotation with the "Ende" announcement.
 *
 * Returns 2 when it made an announcement and 1 otherwise.
 */
/* @0x1001d470 */
int32_t TV_THISCALL TextIn_Mode4(TextIn *self, Token *t)
{
    /* The original leaves buf[5] as whatever the stack held when the token is
     * five characters or longer -- the terminator goes one past the last
     * character written -- and copies it into ti_74[5].  Nothing ever reads
     * ti_74[5]: the comparison below only reaches index 4, and the index the
     * trailing character uses is the length, which that case does not take.
     * Zeroed here so the object is the same from one run to the next. */
    char buf[8];
    int32_t special = -1;      /* a marker that continues the remembered run */
    int32_t last_special = -1; /* the last marker, wherever it was */
    int32_t same = 0;          /* characters matching the remembered run */
    int32_t n, i;

    memset(buf, 0, sizeof buf);
    if (t->text == NULL) {
        self->ti_72 = 0;
        self->ti_74[0] = 0;
    } else {
        n = t->len < 5 ? (int32_t)t->len : 5;
        for (i = 0; i < n; i++) {
            uint8_t c = (uint8_t)t->text[i];

            buf[i] = (char)c;
            if ((uint8_t)self->ti_74[i] == c)
                same++;
            if (tv_strchr(g_str_mode4_marks, (int32_t)(int8_t)c) != NULL) {
                last_special = i;
                if (i - same == -1)
                    special = i;
            }
        }
        if (t->len < 5) {
            uint8_t c = t->trail;

            buf[i] = (char)c;
            if ((uint8_t)self->ti_74[i] == c)
                same++;
            if (c != 0 &&
                tv_strchr(g_str_mode4_marks, (int32_t)(int8_t)c) != NULL) {
                last_special = i;
                if (i - same == -1)
                    special = i;
            }
        }
        buf[i + 1] = 0;
        if (last_special < 0) {
            self->ti_74[0] = 0;
        } else if (self->ti_74[0] == 0) {
            strcpy(self->ti_74, buf);
            self->ti_70 = (int16_t)(last_special + 1);
            self->ti_72 = 0;
        } else if (special > -1) {
            self->ti_70 = (int16_t)(special + 1);
        } else {
            self->ti_70 = (int16_t)(last_special + 1);
            strcpy(self->ti_74, buf);
            self->ti_72 = 0;
        }
    }

    if (self->ti_6e != 0) {
        if (special <= -1) {
            TextIn_InsertText(self, t, g_str_mode4_end, -1);
            t->prev->d1c = 1;
            self->ti_6e = 0;
            self->ti_72 = 0;
            return 2;
        }
        if (t->len > self->ti_70) {
            for (i = 0; i < (int32_t)self->ti_70; i++)
                t->text[i] = ' ';
            return 1;
        }
        Bits_Set(2, t->bits);
        Bits_Set(0x51, t->bits);
        return 1;
    }

    if (special <= -1)
        return 1;
    self->ti_72 = (int16_t)(self->ti_72 + 1);
    if (self->ti_72 <= 1)
        return 1;
    for (i = 0; i < 3; ) {
        while (!Bits_Test(0x53, t->prev->bits))
            t = t->prev;
        if (t->len > self->ti_70) {
            int32_t k;

            for (k = 0; k < (int32_t)self->ti_70; k++)
                t->text[k] = ' ';
        } else {
            Bits_Set(0x51, t->bits);
            Bits_Set(2, t->bits);
        }
        t = t->prev;
        i++;
    }
    TextIn_InsertText(self, t, g_str_mode4_start, -1);
    self->ti_6e = 1;
    return 2;
}

/* The two lists of mail headers, each entry wrapped in '#' so that a match can
 * be checked for being a whole entry.  The first list is what makes the text a
 * message at all -- only the first line is tried against it -- and the second
 * is the headers that are spoken. */
/* @0x10048be8 */
extern const char g_str_mail_ident[];   /* "#Received:#Return-Path:#..." */
/* @0x10048bc8 */
extern const char g_str_mail_spoken[];  /* "#From:#Subject:#Date:#Cc:#Bcc#" */
/* @0x100496e8 */
extern const char g_str_dot[];          /* "." */
/* @0x1006ae68 */
extern const char g_str_newline[];      /* "\n" */

/*
 * Mode 4: read the headers of a mail message.
 *
 * Called when mode 4 starts, and again by TextIn_ReadEscape when it sees
 * ESC[4X.  Lines are read one at a time; a line whose first space is between 1
 * and 20 characters in has that space replaced by a NUL, which leaves the part
 * before it to look up.  The very first line has to be one of the headers in
 * g_str_mail_ident for this to be a message at all, and if it is not, the line
 * is put back together and spoken as ordinary text.  After that, "From:",
 * "Subject:", "Date:", "Cc:" and "Bcc" each go to their handler and every other
 * header is passed over in silence.
 *
 * A blank line, or a line that is just a full stop, ends the headers: a "." and
 * a newline go into the list as two tokens of their own, both with d1c = 3, and
 * the body follows as ordinary text.
 *
 * Returns 1 when the headers ended properly and 0 otherwise.
 */
/* @0x10022970 */
int32_t TV_THISCALL TextIn_Mode4Reset(TextIn *self)
{
    char line[0x104];
    int32_t first_space;
    int32_t nlines = 0;
    Token *tok;

    for (;;) {
        int32_t n = TextIn_ReadLine(self, line, &first_space);

        nlines++;
        if (n == 0)
            break;
        if (n == 1 && line[0] == '.')
            break;
        if (n == -1)
            return 0;
        if (first_space > 0x14 || first_space < 1) {
            /* not a header line at all */
            if (nlines != 1)
                continue;
            strcat(line, g_str_newline);
            TextIn_TokenizeText(self, line, 0);
            return 0;
        }
        line[first_space] = 0;
        if (nlines == 1) {
            const char *p = tv_strstr(g_str_mail_ident, line);

            if (p == NULL || p[-1] != '#' || p[strlen(line)] != '#') {
                /* the first line is not a header this recognises, so the text
                 * is not a message: put the space back and say it */
                line[first_space] = ' ';
                strcat(line, g_str_newline);
                TextIn_TokenizeText(self, line, 0);
                return 0;
            }
        }
        {
            const char *p = tv_strstr(g_str_mail_spoken, line);
            int32_t k;

            if (p == NULL || p[-1] != '#')
                continue;
            k = (int32_t)(p - g_str_mail_spoken);
            if (p[strlen(line)] != '#')
                continue;
            k--;
            if ((uint32_t)k > 0x19)
                continue;
            switch (k) {
            case 0:
                Mode4_From(self, &line[first_space + 1]);
                break;
            case 6:
                Mode4_Header(self, &line[first_space + 1], 1);
                break;
            case 15:
                Mode4_Date(self, &line[first_space + 1]);
                break;
            case 21:
                Mode4_Header(self, &line[first_space + 1], 2);
                break;
            case 25:
                Mode4_Header(self, &line[first_space + 1], 3);
                break;
            default:
                break;
            }
        }
    }

    /* the headers are over: a full stop and a newline of their own */
    tok = TextIn_InsertAfter(self, self->tail);
    if (tok == NULL)
        return TextIn_Error(self, 0);
    if (AllocString(&tok->text, 2) == -1)
        return TextIn_Error(self, 0);
    strcpy(tok->text, g_str_dot);
    tok->len = 1;
    tok->trail = ' ';
    tok->d1c = 3;
    tok = TextIn_InsertAfter(self, self->tail);
    if (tok == NULL)
        return TextIn_Error(self, 0);
    if (AllocString(&tok->text, 2) == -1)
        return TextIn_Error(self, 0);
    strcpy(tok->text, g_str_newline);
    tok->len = 1;
    tok->trail = ' ';
    tok->d1c = 3;
    return 1;
}

/* The labels mode 4 reads a header out with, and the two endings it puts after
 * one.  " Ett: " for the at sign is not Spanish for it -- "arroba" is -- and
 * belongs with the German announcements in TextIn_Mode4: another string that
 * was never translated. */
/* @0x10069c98 */
extern const char g_str_from[];        /* " De: " */
/* @0x10069ca0 */
extern const char g_str_at[];          /* " Ett: " */
/* @0x10069ca8 */
extern const char g_str_period[];      /* " Punto: " */
/* @0x10069cb8 */
extern const char g_str_subject[];     /* " Asunto: " */
/* @0x10069cc8 */
extern const char g_str_date[];        /* " Fecha: " */
/* @0x10069cd8 */
extern const char g_str_cc[];          /* " C C : " */
/* @0x10069ce0 */
extern const char g_str_bcc[];         /* " B C C : " */
/* @0x1006ae6c */
extern const char g_str_dot_nl[];      /* ".\n" */
/* @0x1006ae70 */
extern const char g_str_sp_dot_nl[];   /* " .\n" */

/*
 * Mode 4: the From: header.
 *
 * A name in brackets after the address -- "someone@somewhere (Nombre)" -- is
 * the whole of what is said, and everything else is dropped.  Otherwise an
 * address in angle brackets is cut off, but only when the '<' is more than four
 * characters in, and what is left is read a character at a time with '.' said
 * as " Punto: " and '@' as " Ett: ".
 */
/* @0x10022cf0 */
int32_t TV_THISCALL Mode4_From(TextIn *self, char *text)
{
    char buf[276];
    char *open, *close;

    strcpy(buf, g_str_from);
    open = tv_strchr(text, '(');
    close = open != NULL ? tv_strchr(text, ')') : NULL;
    if (open != NULL && close != NULL) {
        *close = 0;
        strcat(buf, open + 1);
    } else {
        char *lt = tv_strchr(text, '<');
        int32_t at, n;

        if (lt != NULL && tv_strchr(text, '>') != NULL && text + 4 < lt)
            *lt = 0;
        n = (int32_t)strlen(buf);
        for (at = 0; text[at] != 0; at++) {
            char c = text[at];

            if (c == '.') {
                strcat(buf, g_str_period);
                n = (int32_t)strlen(buf);
            } else if (c == '@') {
                strcat(buf, g_str_at);
                n = (int32_t)strlen(buf);
            } else {
                buf[n++] = c;
                buf[n] = 0;
            }
        }
    }
    strcat(buf, g_str_dot_nl);
    TextIn_TokenizeText(self, buf, 0);
    return 1;
}

/*
 * Mode 4: the Subject:, Cc: and Bcc: headers, which differ only in the label.
 * Anything else is refused.
 */
/* @0x10022e90 */
int32_t TV_THISCALL Mode4_Header(TextIn *self, const char *text, int32_t which)
{
    char buf[275];
    const char *label;

    memset(buf, 0, sizeof buf);
    if (which == 1)
        label = g_str_subject;
    else if (which == 2)
        label = g_str_cc;
    else if (which == 3)
        label = g_str_bcc;
    else
        return 0;
    strcpy(buf, label);
    strcat(buf, text);
    strcat(buf, g_str_dot_nl);
    TextIn_TokenizeText(self, buf, 0);
    return 1;
}

/*
 * Mode 4: the Date: header.
 *
 * The label goes in as a token of its own, flagged d1c = 1, and then the date
 * itself is tokenized with `mark` set so that every token of it carries flag 3.
 * The caller's buffer is overwritten with " .\n" and tokenized again to close
 * the line, which is why the text has to be writable.
 */
/* @0x10022f90 */
int32_t TV_THISCALL Mode4_Date(TextIn *self, char *text)
{
    char label[12];
    Token *tok;
    int32_t len;

    strcpy(label, g_str_date);
    tok = TextIn_InsertAfter(self, self->tail);
    if (tok == NULL)
        return TextIn_Error(self, 0);
    len = (int32_t)strlen(label);
    if (AllocString(&tok->text, len + 1) == -1)
        return TextIn_Error(self, 0);
    strcpy(tok->text, label);
    tok->len = (int16_t)len;
    tok->trail = ' ';
    tok->d1c = 1;
    TextIn_TokenizeText(self, text, 1);
    strcpy(text, g_str_sp_dot_nl);
    TextIn_TokenizeText(self, text, 0);
    return 1;
}
