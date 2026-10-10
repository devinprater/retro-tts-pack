/*
 * Helpers of the rule interpreter.
 *
 * `TextIn_Advance` looks a token up, gets back a list of rules, and hands
 * each one to `Rule_Eval`, a recursive interpreter over a bytecode: a stream
 * of int16 words, one opcode per word, dispatched through an 88-entry jump
 * table for opcodes 4..0x5b.  Operands that are single bytes occupy the low
 * half of the following word, which is why every handler here advances
 * `rule_ip` by two and reads one byte.  The interpreter's state lives on the
 * `TextIn` at 0x68 onward -- the 0x3c this object has and the English one
 * does not.
 *
 * These are the arms of it that are separate functions, which is what let
 * them be replaced and checked one at a time while the dispatcher was still
 * the original's.  At the end of the file are the two classifiers that decide
 * what a token is before any rule runs at all.
 */
#include "es_engine.h"

/* Does the token carry these flags?  Mode 1 asks whether any of them are
 * set, anything else whether all of them are.  The bit set is Token.bits,
 * which is why the caller is handed the token and the offset is applied
 * here rather than by the interpreter. */
/* @0x10021630 */
int32_t TV_STDCALL Rule_TestBits(const uint32_t *want, Token *t, int32_t any)
{
    if (any == 1)
        return Bits_AnyIn(want, t->bits);
    return Bits_AllIn(want, t->bits);
}

/* Set the token's trailing character from the operand, and park a value in
 * the token's spare word. */
/* @0x10021600 */
int32_t TV_THISCALL Rule_SetTrail(TextIn *self, Token *t, uint32_t v)
{
    uint8_t c = *(const uint8_t *)self->rule_ip;

    self->rule_ip = (int16_t *)((uint8_t *)self->rule_ip + 2);
    t->trail = c;
    t->d1c = v;
    return 1;
}

/* Compare the token's trailing character with the operand, leaving the
 * character behind in rule_trail either way.  The store is sign-extending
 * and the comparison is not, so a trailing character above 0x7f is recorded
 * as a negative number and still matches. */
/* @0x100215d0 */
int32_t TV_THISCALL Rule_MatchTrail(TextIn *self, Token *t)
{
    uint8_t c = *(const uint8_t *)self->rule_ip;

    self->rule_ip = (int16_t *)((uint8_t *)self->rule_ip + 2);
    self->rule_trail = (int16_t)(int8_t)t->trail;
    return (uint8_t)(t->trail - c) == 0;
}

/* Unlink a token and keep it, where TextIn_RemoveToken unlinks it and frees
 * it.  The links are cleared and the token is parked in TextIn.detached.
 *
 * It refuses a token with nothing in front of it, the same refusal
 * TextIn_RemoveToken and TextIn_InsertAfter make, so the list it works on
 * always begins with the head node.  The original computes a return value
 * for that case first -- t->next -- and then takes the error path, which
 * returns zero regardless; the dead arm is kept below because it is what the
 * original does. */
/* @0x1001d9f0 */
Token *TV_THISCALL TextIn_Detach(TextIn *self, Token *t)
{
    Token *ret;

    if (t == NULL)
        return NULL;
    ret = t->prev != NULL ? t->prev : t->next;
    if (t->prev == NULL) {
        TextIn_Error(self, 0);
        return NULL;
    }
    t->prev->next = t->next;
    if (t->next != NULL)
        t->next->prev = t->prev;
    t->next = NULL;
    t->prev = NULL;
    self->detached = t;
    self->count--;
    return ret;
}

/* The error sink every part of the tokenizer reaches for.  It keeps the
 * first ten codes and counts them, and always answers -1 so that a caller
 * can return its result straight out.
 *
 * Nothing in the corpus reaches it -- no input the differential test carries
 * makes the tokenizer give up -- so it is one of the few functions here
 * whose evidence is entirely the unit case.  That is also why the ring is
 * worth having decompiled: the moment an input does trip it, the codes are
 * readable from the object rather than lost inside the DLL.
 */
/* @0x1001dce0 */
int32_t TV_THISCALL TextIn_Error(TextIn *self, int32_t code)
{
    if (self->err_count < 10) {
        self->errors[self->err_count] = code;
        self->err_count++;
    }
    return -1;
}

/*
 * Walk away from a token looking for one that carries the wanted flags, and
 * report how far it had to go.
 *
 * Each pass steps one token in the given direction and then keeps stepping
 * while the token it lands on has flag 0x51 set, which is what makes a token
 * transparent to this search; the pass stops on the first token that does
 * not.  Flag 0x54 is a wall: meeting one abandons the search outright.  Up
 * to `count` passes are made, so a count of 2 means "the second real token
 * along", not "within two tokens".
 *
 * The signed number of steps taken is left in TextIn.rule_trail on a hit and
 * is zero on every other path, including the ones that give up early.  It is
 * an int16 and it is not clamped, so a long enough walk wraps; nothing in
 * the corpus gets near that.
 *
 * Backwards, the head node ends the search, which is the same boundary
 * TextIn_Detach and TextIn_RemoveToken refuse to cross.
 *
 * The two flag tests at the end are asked in the order the original asks
 * them, and the second only adds the case the first cannot answer: an empty
 * want set, where nothing intersects but everything contains.  A `check`
 * other than 1 skips both, which makes the whole call a walk that always
 * answers no -- and four of the interpreter's opcodes do exactly that,
 * passing 2.  Whether that is deliberate or a slip is not established; what
 * is established is that the original behaves the same way, over every check
 * value the dispatcher is seen to pass.
 */
/* @0x10021080 */
int32_t TV_THISCALL Rule_Scan(TextIn *self, const uint32_t *want, Token *t,
                              int32_t dir, int32_t count, int32_t check)
{
    int16_t steps = 0;
    int32_t i;

    self->rule_trail = 0;
    if (t == NULL)
        return 0;
    for (i = 0; i < count; i++) {
        int transparent = 1;
        while (transparent) {
            if (dir == -1) {
                t = t->prev;
                if (t == NULL || self->head == t)
                    return 0;
                steps--;
            } else {
                t = t->next;
                if (t == NULL)
                    return 0;
                steps++;
            }
            if (!Bits_Test(0x51, t->bits))
                transparent = 0;
            if (Bits_Test(0x54, t->bits))
                return 0;
        }
        if (check == 1) {
            if (Bits_AnyIn(want, t->bits) || Bits_AllIn(want, t->bits)) {
                self->rule_trail = steps;
                return 1;
            }
        }
    }
    return 0;
}

/*
 * Insert a spoken word next to a token.
 *
 * The operand names one of twenty-six fixed words, the new token is linked
 * in before or after the reference, and the word becomes its replacement
 * text.  This is how a rule turns a symbol into something sayable: "." ->
 * "punto", "$" -> "dolar", "%" -> "por".
 *
 * The table has not been fully translated.  Alongside the Spanish there is
 * "tiret", which is French, "minutes", which is French or English but not
 * Spanish, and "un mitad", which is not how a half is said.  They are left
 * exactly as the image has them: what the engine says is what this table
 * says, and correcting it would change the audio.
 *
 * Two details of the original are kept.  The room asked of AllocString is
 * the word's length plus five, not plus one, so every one of these tokens
 * carries four bytes of slack for whatever appends to it later.  And the
 * operand is read sign-extended, so a negative one indexes off the front of
 * the table; nothing in the corpus produces one.
 */
/* @0x10069b18 */
extern const tv_ref g_rule_words[26];

/* @0x10021670 */
int32_t TV_THISCALL Rule_InsertWord(TextIn *self, Token *ref, uint32_t v,
                                    int32_t dir)
{
    Token *t;
    const char *w;
    int32_t idx;
    size_t len;

    t = dir == -1 ? TextIn_InsertBefore(self, ref) : TextIn_InsertAfter(self, ref);
    idx = *self->rule_ip++;
    w = TV_REF(char, g_rule_words[idx]);
    len = strlen(w);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, w, len + 1);
    t->d1c = v;
    t->w0a = (int16_t)idx;
    t->w08 = (int16_t)idx;
    t->trail = ' ';
    return 1;
}

/*
 * Spell a token's text out character by character into its replacement
 * text: each character's spoken name, separated by spaces.
 *
 * With `all` set every character above 0x1f is named; with it clear only
 * those whose class word has bit 2 or 3, which is letters and digits.  The
 * control characters at 0x1f and below are dropped either way, which is also
 * what keeps the one empty entry in the name table out of reach -- index
 * 0x1f is the only one of the 256 with no name.
 *
 * The buffer is the original's 132 bytes and the guard is its 100
 * characters, checked before each append rather than after.  That cannot
 * overflow: the longest name in the table is 22 characters, so the worst
 * append starts at 99 and ends at 123.
 *
 * The names are the shipped ones, typos and all -- "signo de pocentaje" is
 * missing its r, "Dollar" never made it out of English, and the exclamation
 * mark is named with a French apostrophe.  All three are audible, and all
 * three stay.
 */
/* @0x10069d08 */
extern const uint32_t g_char_flags[256];
/* @0x1006a2a0 */
extern const tv_ref g_char_names[256];
/* @0x10069c30 */
extern const char g_str_space[];

/* @0x10022810 */
int32_t TV_THISCALL Rule_SpellOut(TextIn *self, Token *t, uint32_t v,
                                  int32_t all)
{
    char buf[132];
    const char *s = t->text;
    size_t len;

    if (s == NULL)
        return 0;
    buf[0] = 0;
    for (; *s != 0; s++) {
        uint8_t c = (uint8_t)*s;
        if (strlen(buf) >= 0x64)
            break;
        if (c <= 0x1f)
            continue;
        if (all == 0 && (g_char_flags[c] & 0xc) == 0)
            continue;
        strcat(buf, TV_REF(char, g_char_names[c]));
        strcat(buf, g_str_space);
    }
    len = strlen(buf);
    if (AllocString(&t->text2, (int32_t)len + 1) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, buf, len + 1);
    t->d1c = v;
    return 1;
}

/*
 * Forward to the number-to-words formatter with its last two arguments
 * zeroed.  Eight of the interpreter's arms call the engine through this
 * thunk rather than the six-argument function behind it, so it is worth
 * having as itself: replacing it replaces all eight call sites at once.
 *
 * `digits` must be writable.  Above three digits the formatter writes a NUL
 * three characters from the end, recurses on the leading part, names the
 * group it cut off and then puts the three digits back, so a caller that
 * hands it a string literal gets a fault even though the string it gets back
 * is the one it passed in.  That restoration is asserted in unit_es over
 * every input it tries, because the callers rely on it -- Rule_SayNumberText
 * and Rule_SayNumberOrSpell hand over the token's own text.
 */
/* @0x10020920 */
int32_t TV_CDECL Number_Words(char *digits, char *out, int32_t style,
                              int32_t mode)
{
    return Number_WordsEx(digits, out, style, mode, 0, 0);
}

/*
 * Say a token's number.
 *
 * The number is turned into digits and then into words, and the words
 * become the token's replacement text.  Two of the token's flags steer it:
 * 0x4a picks the first of the formatter's two modes rather than the second,
 * and 0x31 picks style 1 rather than style 4.  A negative number is refused
 * outright.
 *
 * A token that had no trailing character gets a space and flag 0x50, which
 * is how a number that ran to the end of its text is given something to sit
 * against before the next token.
 *
 * The two buffers are the original's: 52 bytes for the digits and 256 for
 * the words, laid out exactly as its frame lays them out.
 */
/* @0x10021720 */
int32_t TV_THISCALL Rule_SayNumber(TextIn *self, Token *t, uint32_t v)
{
    char digits[52];
    char words[256];
    int32_t mode = 2, style;
    size_t len;

    if (t->num < 0)
        return 0;
    if (Bits_Test(0x4a, t->bits))
        mode = 1;
    tv_itoa(t->num, digits, 10);
    style = Bits_Test(0x31, t->bits) ? 1 : 4;
    Number_Words(digits, words, style, mode);
    len = strlen(words);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, words, len + 1);
    t->d1c = v;
    if (t->trail == 0) {
        t->trail = ' ';
        Bits_Set(0x50, t->bits);
    }
    return 1;
}

/*
 * Say a number that is held as text rather than as a number.
 *
 * Rule_SayNumber works from Token.num, which is an int32 and so tops out at
 * ten digits.  This one works from Token.text and accepts up to seventeen,
 * which is what the length guard is for.
 *
 * The formatter's first mode is chosen when the token itself carries flag
 * 0x4a or when one of the next two real tokens does; otherwise the second.
 * That is the only place Rule_Scan is used to decide something rather than
 * to measure a distance.
 *
 * Two things it does that Rule_SayNumber does not.  It sets flag 0x31 on the
 * token rather than reading it.  And it overwrites the trailing character
 * with a space whatever it was, where Rule_SayNumber only fills one in when
 * there was none -- the 0x50 flag still marks only the tokens that had none.
 *
 * It hands the formatter the token's own text rather than a copy, so that
 * text has to be writable; the formatter puts it back before returning.
 */
/* @0x10021820 */
int32_t TV_THISCALL Rule_SayNumberText(TextIn *self, Token *t, uint32_t v)
{
    uint32_t want[3];
    char out[256];
    int32_t mode = 2;
    size_t len;

    if (t == NULL || t->text == NULL || strlen(t->text) > 0x11)
        return 0;
    if (Bits_Test(0x4a, t->bits)) {
        mode = 1;
    } else {
        want[0] = want[1] = want[2] = 0;
        Bits_Set(0x4a, want);
        if (Rule_Scan(self, want, t, 1, 2, 1))
            mode = 1;
    }
    Number_Words(t->text, out, 1, mode);
    Bits_Set(0x31, t->bits);
    len = strlen(out);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, out, len + 1);
    if (t->trail == 0)
        Bits_Set(0x50, t->bits);
    t->trail = ' ';
    t->d1c = v;
    return 1;
}

/*
 * Say a short number from a token's text, or spell it out if it is long.
 *
 * The third of the number handlers, and the one that has somewhere to go
 * when the text is too long: above seven characters it hands the token
 * straight to Rule_SpellOut with `all` clear, so a long run of digits is
 * read out digit by digit instead of being named.
 *
 * Against Rule_SayNumberText, which takes up to seventeen: this asks the
 * formatter for style 4 rather than style 1, it leaves flag 0x31 alone
 * rather than setting it, and it fills in a trailing space only when there
 * was none.  The way it picks the formatter's mode is the same -- flag 0x4a
 * on the token, or on one of the next two real tokens.
 *
 * Like the other one it hands the token's own text to the formatter, which
 * needs it writable.
 */
/* @0x10021aa0 */
int32_t TV_THISCALL Rule_SayNumberOrSpell(TextIn *self, Token *t, uint32_t v)
{
    uint32_t want[3];
    char out[256];
    int32_t mode = 2;
    size_t len;

    if (t == NULL || t->text == NULL)
        return 0;
    if (strlen(t->text) > 7)
        return Rule_SpellOut(self, t, v, 0);
    if (Bits_Test(0x4a, t->bits)) {
        mode = 1;
    } else {
        want[0] = want[1] = want[2] = 0;
        Bits_Set(0x4a, want);
        if (Rule_Scan(self, want, t, 1, 2, 1))
            mode = 1;
    }
    Number_Words(t->text, out, 4, mode);
    len = strlen(out);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, out, len + 1);
    t->d1c = v;
    if (t->trail == 0) {
        t->trail = ' ';
        Bits_Set(0x50, t->bits);
    }
    return 1;
}

/*
 * Does this short token read as a word, or as initials?
 *
 * Answers 1 when it should be spelled out and 0 when it should be said as a
 * word, which is the way round the caller wants it.  Only three- and
 * four-letter tokens are really judged: anything shorter is spelled, anything
 * longer is said.
 *
 * The test builds a consonant/vowel pattern and then applies Spanish
 * phonotactics to it.  A token with no vowel after its first consonant is
 * initials -- CBS, IBM.  A doubled letter is a word.  An H at either end is
 * initials, H being silent.  Beyond that only three-letter tokens are
 * decided: two consonants in front are a word if the second is R or L
 * (BRA), initials if the first is S and the second is P, T or C (SPC), and a
 * word otherwise; and a vowel in each of the last two places is a word.
 * Everything else is initials.
 *
 * SOL and USA come out as words, IBM and CBS as initials, which is the
 * behaviour to keep in mind when reading it.
 */
/* @0x1006ae40 */
extern const char g_str_vowels[];      /* "AEIOUYaeiouy" */
/* @0x1006ae3c */
extern const char g_str_ptc[];         /* "PTC" */

/* @0x10020f00 */
int32_t TV_CDECL Word_IsAcronym(const char *s)
{
    char pat[5];
    char prev = 0;
    int32_t len, i, state = 0, repeats = 0;

    memset(pat, 0, sizeof pat);
    len = (int32_t)strlen(s);
    if (len < 3)
        return 1;
    if (len > 4)
        return 0;
    for (i = 0; s[i] != 0; i++) {
        char c = s[i];
        if (tv_strchr(g_str_vowels, c) != NULL) {
            pat[i] = 'V';
            if (state == 1)
                state = 2;
        } else {
            if (state == 0)
                state = 1;
            pat[i] = 'C';
        }
        if (prev == c)
            repeats++;
        prev = c;
    }
    if (repeats != 0)
        return 1;
    if (state != 2)
        return 1;
    if (s[0] == 'H' || s[i - 1] == 'H')
        return 1;
    if (len != 3)
        return 0;
    if (pat[0] == 'C' && pat[1] == 'C') {
        if (s[1] == 'R' || s[1] == 'L')
            return 0;
        if (s[0] == 'S' && tv_strchr(g_str_ptc, s[1]) != NULL)
            return 0;
        return 1;
    }
    return (pat[1] == 'V' && pat[2] == 'V') ? 1 : 0;
}

/*
 * Say a token as a word, or spell its letters out.
 *
 * A token of one character is left alone.  Flag 7 forces the spelling.
 * Otherwise Word_IsAcronym decides, with one softening: a token that looks
 * like initials is still said as a word if a neighbour carrying flag 0x19
 * reads as a word itself, which is how initials embedded in real text avoid
 * being spelled one letter at a time.  The token before is consulted first
 * and the token after second, and either one is enough.
 *
 * Said as a word, the text is lowercased into the replacement text.  Spelled
 * out, a trailing full stop is turned into a space first, so the letters do
 * not end on a sentence break that was really an abbreviation mark.
 *
 * The lowercasing goes through a 104-byte stack buffer with no length check,
 * which is the original's and is a real defect: a token of 104 characters or
 * more overruns it into the return address.  The tokenizer splits on spaces,
 * so it takes an unbroken run that long to reach -- a URL or a row of
 * symbols would do it.  Reproduced as-is; beyond that length neither the
 * original nor this has defined behaviour.
 */
/* @0x10020d70 */
int32_t TV_THISCALL Rule_Acronym(TextIn *self, Token *t, uint32_t v)
{
    char buf[104];
    int spell = 0;
    size_t len;

    if (t->text == NULL)
        return 0;
    if (t->len == 1)
        return 1;
    if (Bits_Test(7, t->bits)) {
        spell = 1;
    } else if (Word_IsAcronym(t->text) == 1) {
        spell = 1;
        /* Only a token that carries 0x19 itself gets the softening; without
         * it the spelling stands whatever the neighbours look like. */
        if (Bits_Test(0x19, t->bits)) {
            if (t->prev != NULL && Bits_Test(0x19, t->prev->bits) &&
                Word_IsAcronym(t->prev->text) == 0)
                spell = 0;
            else if (t->next != NULL && Bits_Test(0x19, t->next->bits) &&
                     Word_IsAcronym(t->next->text) == 0)
                spell = 0;
        }
    }
    if (spell) {
        if (t->trail == '.')
            t->trail = ' ';
        Rule_SpellOut(self, t, v, 0);
        return 1;
    }
    len = strlen(t->text);
    if (AllocString(&t->text2, (int32_t)len + 1) == -1)
        return TextIn_Error(self, 0);
    memcpy(buf, t->text, len + 1);
    tv_strlwr(buf);
    memcpy(t->text2, buf, strlen(buf) + 1);
    t->d1c = v;
    return 1;
}

/*
 * Say the text of the record the tokenizer attached to a token, and make it
 * plural if the rule asks.
 *
 * The record hangs off Token.d18 and carries a key of its own; the handler
 * does nothing unless that key matches the one the rule passes, which is how
 * one opcode serves several kinds of attachment.  Where the record came from
 * is not established -- what is known is the three fields this touches, so
 * that is all RuleRec names.
 *
 * Pluralising is Spanish's rule and the engine's own conditions.  It happens
 * only when the token carries flag 0x37, has something in front of it, and
 * the number on the token in front is more than one -- and for key 13 the
 * number on the token behind is added in first, which is how "2 metros 50"
 * counts as more than one.  A word ending in a vowel takes "s", anything
 * else takes "es".  The vowel set used here is "aoieuAOIEU", which is not
 * the "AEIOUYaeiouy" that Word_IsAcronym uses: no Y.
 *
 * Three smaller things it settles about spacing.  A trailing full stop
 * becomes a space unless flag 0x49 says to keep it, a token in front with no
 * trailing character gets a space, and a token with none of its own gets one
 * and flag 0x50.
 */
/* @0x1006ae58 */
extern const char g_str_vowels_plain[];   /* "aoieuAOIEU" */
/* @0x10069ba0 */
extern const char g_str_s[];
/* @0x10069b9c */
extern const char g_str_es[];

/* Only the fields Rule_SayRecord reads; the rest is unexamined. */
typedef struct {
    uint8_t  unknown00[0x20];
    uint32_t key;                /* 0x20, matched against the rule's */
    uint8_t  unknown24[6];
    int16_t  text_off;           /* 0x2a, from the start of text[] */
    uint8_t  unknown2c[1];
    char     text[1];            /* 0x2d */
} RuleRec;

/* @0x100212b0 */
int32_t TV_THISCALL Rule_SayRecord(TextIn *self, Token *t, uint32_t key,
                                   int32_t plural)
{
    const RuleRec *rec;
    const char *src;
    size_t len;
    int32_t count;

    if (t == NULL)
        return TextIn_Error(self, 0);
    rec = (const RuleRec *)t->d18;
    if (rec == NULL)
        return 0;
    if (rec->key != key)
        return 0;
    src = rec->text + rec->text_off;
    len = strlen(src);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, src, len + 1);
    t->d1c = key;
    if (t->trail == '.' && !Bits_Test(0x49, t->bits))
        t->trail = ' ';
    if (t->prev != NULL && t->prev->trail == 0)
        t->prev->trail = ' ';
    if (t->trail == 0) {
        t->trail = ' ';
        Bits_Set(0x50, t->bits);
    }
    if (plural == 0)
        return 1;

    len = strlen(t->text2);
    if ((int32_t)len < 1)
        return 0;
    if (!Bits_Test(0x37, t->bits) || t->prev == NULL)
        return 1;
    count = t->prev->num;
    if (key == 0xd && t->next != NULL)
        count += t->next->num;
    if (count <= 1)
        return 1;
    strcat(t->text2,
           tv_strchr(g_str_vowels_plain, t->text2[len - 1]) != NULL
               ? g_str_s : g_str_es);
    return 1;
}

/*
 * Read a number written in groups: thousands separated by spaces or full
 * stops, and a decimal part after a comma.
 *
 * It is the only handler that takes a Token ** rather than a Token *,
 * because it consumes several tokens and has to tell the interpreter which
 * one is left.  The digits of every group are pasted into one string, the
 * tokens that supplied them are removed, and the last token of the run
 * survives holding the whole thing spoken.
 *
 * A group joins if it carries flag 0x14 and not 0x0f, is exactly three
 * characters long, and follows the same separator as the first -- so
 * "1.234.567" joins and "1.234 567" does not.  At most fifteen groups.
 * After a comma, one more token is taken as the decimal part, and the words
 * become "<integer> coma <decimal>"; the comma is only looked for when the
 * rule's fourth argument is zero.
 *
 * Two things carry over from the other number handlers.  The formatter's
 * mode comes from flag 0x4a on the token or on one of the next two.  And the
 * text handed to it has to be writable -- here a local buffer for the joined
 * digits, and the decimal token's own text for the fraction.
 *
 * The value the formatter parsed is stored back into Token.num, which is the
 * only place that return value is used.
 */
/* @0x10021be0 */
int32_t TV_THISCALL Rule_SayGroupedNumber(TextIn *self, Token **first,
                                          uint32_t v, int32_t force_space)
{
    char digits[256];
    char words[256];
    char frac[256];
    uint32_t want[3];
    Token *t = *first;
    int32_t mode = 2, comma = 0, merged = 1;
    uint8_t trail;
    size_t len;

    want[0] = want[1] = want[2] = 0;
    if (t == NULL || t->text == NULL)
        return 0;
    digits[0] = 0;
    words[0] = 0;
    trail = t->trail;
    strcat(digits, t->text);
    if (trail == ' ' || trail == '.') {
        while (t->next != NULL) {
            if (t->trail != trail)
                break;
            if (!Bits_Test(0x14, t->next->bits))
                break;
            if (Bits_Test(0xf, t->next->bits))
                break;
            if (t->next->len != 3)
                break;
            if (merged >= 0xf)
                break;
            strcat(digits, t->next->text);
            t = t->next;
            merged++;
        }
        trail = t->trail;
    }
    if (force_space == 0 && trail == ',' && t->next != NULL &&
        Bits_Test(0x14, t->next->bits) && !Bits_Test(0xf, t->next->bits)) {
        t = t->next;
        comma = t->num;
    }
    if (Bits_Test(0x4a, t->bits)) {
        mode = 1;
    } else {
        Bits_Set(0x4a, want);
        if (Rule_Scan(self, want, t, 1, 2, 1))
            mode = 1;
    }
    if (strlen(digits) > 0x11)
        return 0;
    t->num = Number_Words(digits, words, 4, mode);
    if (comma != 0) {
        strcat(words, g_str_space);
        strcat(words, TV_REF(char, g_rule_words[1]));        /* "coma" */
        strcat(words, g_str_space);
        Number_Words(t->text, frac, 4, mode);
        strcat(words, frac);
    }
    len = strlen(words);
    if (AllocString(&t->text2, (int32_t)len + 5) == -1)
        return TextIn_Error(self, 0);
    memcpy(t->text2, words, len + 1);
    t->d1c = v;
    if (t->trail == 0) {
        t->trail = ' ';
        Bits_Set(0x50, t->bits);
    }
    if (force_space != 0)
        t->trail = ' ';
    if (t != *first) {
        Token *p = t->prev;
        while (*first != p)
            p = TextIn_RemoveToken(self, p, -1);
        *first = TextIn_RemoveToken(self, p, 1);
    }
    return 1;
}

/*
 * What kind of number a numeric token is, as flags.
 *
 * The flag 0x14 says "a number" and goes on unconditionally; a leading zero
 * adds 0x35 and five digits add 0xc.  What follows the number then decides the
 * rest, and the interesting cases are the ones that make a date or a time out
 * of two numbers in a row:
 *
 *   ª  an ordinal, feminine: 0x4a and 0x31, and nothing else
 *   º  an ordinal, masculine: 0x31, and nothing else
 *   '  0x40
 *   ,  0x12 and 0xe
 *   - . /  a day, if it is 1 to 31 in at most two digits: 0x39 and 0xa.  A
 *          full stop also gets 0x11, and doubles as a month when the token
 *          before it was not already one.
 *   :  a time: 0x3e for the hour, or 0x3c for the minutes when the token
 *          before was the hour
 *
 * and then, whatever the trailing character, the token before is looked at:
 * after a day comes a month (0x3a), after a month a year (0x3b), and after an
 * hour the minutes (0x3c).
 */
/* @0x1001fde0 */
int32_t TV_STDCALL Rule_ClassifyNumber(Token *t)
{
    uint32_t *bits = t->bits;
    uint32_t *prev = t->prev != NULL ? t->prev->bits : NULL;
    uint8_t trail;

    Bits_Set(0x14, bits);
    t->w34 = 0;
    if (t->text[0] == '0')
        Bits_Set(0x35, bits);
    if (t->len == 5)
        Bits_Set(0xc, bits);

    trail = t->trail;
    switch (trail) {
    case 0xaa:
        Bits_Set(0x4a, bits);
        Bits_Set(0x31, bits);
        return 1;
    case 0xba:
        Bits_Set(0x31, bits);
        return 1;
    case '\'':
        Bits_Set(0x40, bits);
        break;
    case ',':
        Bits_Set(0x12, bits);
        Bits_Set(0xe, bits);
        break;
    case '-':
    case '.':
    case '/':
        if (!Bits_Test(0x39, prev) && t->num > 0 && t->num < 0x20 &&
            t->len < 3) {
            Bits_Set(0x39, bits);
            Bits_Set(0xa, bits);
        }
        if (trail == '.') {
            Bits_Set(0x11, bits);
            if (!Bits_Test(0x3e, prev) && t->num >= 0 && t->num < 0x19 &&
                t->len < 3) {
                Bits_Set(0x3e, bits);
                Bits_Set(0xb, bits);
            }
        }
        break;
    case ':':
        if (Bits_Test(0x3e, prev)) {
            if (t->len == 2 && t->num >= 0 && t->num < 0x3c) {
                Bits_Set(0x3c, bits);
                Bits_Set(0xb, bits);
            }
        } else if (t->len < 3 && t->num >= 0 && t->num < 0x19) {
            Bits_Set(0x3e, bits);
            Bits_Set(0xb, bits);
        }
        break;
    default:
        break;
    }

    if (Bits_Test(0x39, prev)) {
        if (t->num > 0 && t->num < 0xd && t->len < 3) {
            Bits_Set(0x3a, bits);
            Bits_Set(0xa, bits);
        }
    } else if (Bits_Test(0x3a, prev)) {
        if ((t->len == 2 && t->num > 0x14) ||
            (t->len == 4 && t->num < 0x7e4)) {
            Bits_Set(0x3b, bits);
            Bits_Set(0xa, bits);
        }
    }
    if (Bits_Test(0x3e, prev) && t->len == 2 && t->num >= 0 &&
        t->num < 0x3c) {
        Bits_Set(0x3c, bits);
        Bits_Set(0xb, bits);
    }
    return 1;
}

/* The abbreviation records, 0x2c bytes ahead of the expansion text
 * lang/spa/engine/tables.c searches: two int16 for the token's w08 and w0a, three dwords of
 * flags to be or-ed into it, the record's own flag set at +0x14, and the
 * expansion at +0x2c. */
/* @0x10062048 */
extern const uint8_t g_abbrev_rec[];
/* @0x10045898 */
extern int32_t g_abbrev_index[507];

/*
 * Is this token an abbreviation, and if so which one.
 *
 * The text is looked up as it stands; failing that, and only when folding its
 * accents actually changed it, the folded form is tried.  Abbrev_Find gives a
 * run of records with the same spelling, and each is then tested against the
 * token: the record carries a flag set of its own and every bit in it has to
 * hold.  Most bits simply have to be set on the token as well.  Four --
 * 25, 67, 68 and 70 -- and bit 69 form one group between them, of which any one
 * passing is enough; bit 69 asks that the token's text equal the expansion
 * exactly, and bit 71 asks for a trailing full stop.
 *
 * The first record that passes wins: its two int16 go into the token, its three
 * flag words are or-ed into the token's, the record itself goes into d18 and
 * flag 0x51 is cleared.  Returns whether one matched.
 */
/* @0x1001de40 */
int32_t TV_CDECL Rule_MatchAbbrev(Token *t)
{
    char folded[0xb8];
    const uint8_t *rec = NULL;
    int32_t index = 0;
    int32_t count;
    int32_t changed = 0;
    int32_t n = 0;
    int32_t hit = 0;
    int32_t group = 1;
    int32_t ok = 1;
    int32_t left;

    if (t->text == NULL)
        return 0;

    count = Abbrev_Find(t->text, &index);
    if (count == 0) {
        const char *s = t->text;
        uint8_t c = (uint8_t)*s;

        while (c != 0) {
            uint8_t out = Accent_Fold(&c);

            if (out != 0) {
                folded[n++] = (char)out;
                if (c != 0) {
                    n++;
                    changed = 1;
                    folded[n - 1] = (char)c;
                }
            }
            c = (uint8_t)s[1];
            s++;
        }
        folded[n] = 0;
        if (changed)
            count = Abbrev_Find(folded, &index);
    }

    left = count;
    while (left-- > 0) {
        const uint32_t *recbits;
        int32_t bit = 0;

        if (hit != 0)
            goto apply;
        rec = g_abbrev_rec + g_abbrev_index[index];
        recbits = (const uint32_t *)(rec + 0x14);
        hit = 1;
        group = 1;
        ok = 1;
        for (;;) {
            bit = Bits_Next(bit, recbits);
            if (bit == 0) {
                if (ok != 0) {
                    hit = 1;
                    if (group != 0)
                        break;
                }
                hit = 0;
                break;
            }
            if (ok == 0) {
                hit = 0;
                break;
            }
            if (bit == 25 || bit == 67 || bit == 68 || bit == 70) {
                if (hit != 0 || group == 0)
                    group = Bits_Test(bit, t->bits);
                hit = 0;
            } else if (bit == 69) {
                if (hit != 0 || group == 0)
                    group = strcmp(t->text,
                                   (const char *)rec + 0x2c) == 0 ? 1 : 0;
                hit = 0;
            } else if (bit == 71) {
                if (t->trail != '.')
                    ok = 0;
            } else if (!Bits_Test(bit, t->bits)) {
                ok = 0;
            }
        }
        index++;
    }
    if (hit == 0)
        return 0;

apply:
    {
        const uint32_t *w = (const uint32_t *)(rec + 8);
        uint32_t *b = t->bits;
        int k;

        t->w08 = (int16_t)*(const int32_t *)rec;
        t->w0a = (int16_t)*(const int32_t *)(rec + 4);
        t->d18 = (void *)rec;
        for (k = 0; k < 3; k++)
            b[k] |= w[k];
        Bits_Clear(0x51, b);
    }
    return 1;
}

/* The word Rule_ClassifyToken puts in place of ten or more of the same
 * character in a row. */
/* @0x1006ae00 */
extern const char g_str_etcetera[];   /* "etcetera." */

/*
 * What kind of word a token is, as flags.
 *
 * The token's flag set is cleared and then built from one walk over its text,
 * counting what each character is against the class table: digits, letters,
 * capitals, vowels, punctuation, arithmetic operators, Roman numeral letters,
 * carriage returns, and the dots and letters that make up a dotted
 * abbreviation.  A count equal to the length means every character was of that
 * kind, and that is what most of the flags below test.
 *
 *   0x43  a capital and only one, so the word is merely capitalised
 *   0x19 / 0x46  all capitals, or not
 *   0x18  more than one capital, or all capitals, or letters and no vowel at
 *         all, or all dots and letters -- between them, "spell it out"
 *   7     a dotted abbreviation
 *   0xf, 0x14  a Roman numeral, with its value in Token.num; and 0x3a on top
 *         of that when the number before it could be a day, so XII becomes a
 *         month
 *   0x44  letters throughout and not one capital
 *   0x51, 2, 0x54  all punctuation: 0x51 unless it is one sentence mark,
 *         0x54 once past six of them or if a carriage return was among them
 *   0x13  all operators, or a trailing one
 *   0x1a  trailing punctuation
 *   0x31, 0xf, 0x14  a Roman numeral with an 'e' after it
 *
 * All digits is not a flag but a different question, so it hands the token to
 * Rule_ClassifyNumber with its value in Token.num, and so does a token that
 * arrives already marked as a number.  A newline in the first two characters
 * is 0x53 and 0x54 and nothing else.
 *
 * Ten or more of the same character in a row -- a rule of dots, say -- is cut
 * back to the four that came before them and "etcetera." is inserted after it
 * as a token of its own.
 *
 * The trailing character is looked up with its sign kept, so a trailing
 * character above 0x7f reads the class table at a negative index -- 0x200
 * bytes before the table for 0x80, four bytes before it for 0xff.  Faithful
 * to the original, which does the same, and the reason the flags do not have
 * to make sense there.
 */
/* @0x1001f950 */
int32_t TV_THISCALL Rule_ClassifyToken(TextIn *self, Token *t)
{
    const char *s;
    uint32_t *bits = t->bits;
    uint32_t cls = 0;             /* the class word of the character before */
    int32_t dotted = 0, upper = 0, roman = 0, letters = 0, digits = 0;
    int32_t ops = 0, crs = 0, punct = 0, vowels = 0, starts_upper = 0;
    uint8_t prev = 0, prev2 = 0;
    int32_t run = 0;              /* how many of the same character in a row */
    int32_t n = 0;

    bits[0] = 0;
    bits[1] = 0;
    bits[2] = 0;
    if (t->is_number != 0) {
        Rule_ClassifyNumber(t);
        return 1;
    }
    s = t->text;
    if (s == NULL) {
        if ((g_char_flags[(int32_t)(int8_t)t->trail] & 0x40) == 0)
            Bits_Set(0x51, bits);
        return 1;
    }
    if (s[0] == '\n' || s[1] == '\n') {
        Bits_Set(0x53, bits);
        Bits_Set(0x54, bits);
        return 1;
    }

    if (s[0] != 0) {
        for (;;) {
            uint8_t c = (uint8_t)s[n];
            uint32_t k = g_char_flags[c];

            if (c == '\r')
                crs++;
            if (k & 8)
                digits++;
            if (k & 0x40)
                ops++;
            if (k & 0x200)
                roman++;
            if (k & 4) {
                letters++;
                if (prev == '.')
                    dotted++;
            }
            if (k & 0x10) {
                upper++;
                if (n == 0)
                    starts_upper = 1;
            }
            if (k & 0x80)
                vowels++;
            if (k & 0x20)
                punct++;
            if (c == '.' && (cls & 4))
                dotted++;
            if ((k & 0x20) == 0) {
                if (prev == c)
                    run++;
                else if (prev2 == c)
                    run++;
                else
                    run = 0;
                if (run >= 10) {
                    Token *tok;

                    /* the four before the run stay, and everything from there
                     * on becomes a token of its own */
                    t->text[n - 6] = 0;
                    t->trail = ' ';
                    t->len = (int16_t)(t->len - 6);
                    tok = TextIn_InsertAfter(self, t);
                    AllocString(&tok->text, 10);
                    strcpy(tok->text, g_str_etcetera);
                    tok->len = 9;
                    tok->trail = ' ';
                    break;
                }
            }
            prev2 = prev;
            n++;
            prev = c;
            cls = k;
            if (s[n] == 0)
                break;
        }
    }

    if (starts_upper && upper == 1)
        Bits_Set(0x43, bits);
    Bits_Set(n == upper ? 0x19 : 0x46, bits);
    if (upper > 1 || n == upper)
        Bits_Set(0x18, bits);
    if (vowels == 0 && letters > 0)
        Bits_Set(0x18, bits);
    if (n == dotted) {
        Bits_Set(7, bits);
        Bits_Set(0x18, bits);
    }
    if (n == roman) {
        int32_t v = Roman_Value(t->text);

        if (v > 0) {
            Bits_Set(0xf, bits);
            Bits_Set(0x14, bits);
            if (t->prev != NULL && t->prev->num > 1 && t->prev->num < 0x20 &&
                v < 0xd)
                Bits_Set(0x3a, bits);
            t->num = v;
        }
    }
    if (upper == 0 && n == letters)
        Bits_Set(0x44, bits);
    if (n == punct) {
        if (n == 1 && s[0] == '\'')
            t->w08 = 0x212;
        if (n > 1 || (cls & 0x42) == 0)
            Bits_Set(0x51, bits);
        if (n > 3)
            Bits_Set(2, bits);
        if (n > 6 || crs != 0)
            Bits_Set(0x54, bits);
    }
    if (n == ops && n <= 2)
        Bits_Set(0x13, bits);
    if (g_char_flags[(int32_t)(int8_t)t->trail] & 0x40)
        Bits_Set(0x13, bits);
    if (g_char_flags[(int32_t)(int8_t)t->trail] & 0x20)
        Bits_Set(0x1a, bits);
    if (n == digits) {
        int32_t v = tv_atol(t->text);

        t->w34 = 0;
        t->num = v;
        t->is_number = 1;
        Rule_ClassifyNumber(t);
        return 1;
    }

    n--;
    if (s[n] == 'e' && n > 0 && n == roman) {
        int32_t v = Roman_Value(t->text);

        if (v > 0) {
            Bits_Set(0x31, bits);
            Bits_Set(0xf, bits);
            Bits_Set(0x14, bits);
            t->num = v;
        }
    }
    return 1;
}

/* The rest of the rule opcodes: the seven arms Rule_Eval dispatches to that
 * nothing in the corpus reaches.  They are written because the standalone
 * build has no DLL to fall back on, and tested by unit_es rather than by the
 * corpus -- see the "rule opcodes" suite, which drives each one directly. */

/* @0x1006ae30 */
extern const char g_str_del[];         /* "del " */
/* @0x1006ae38 */
extern const char g_str_de[];          /* "de " */
/* @0x1006ae64 */
extern const char g_str_zero[];        /* "0" */
/* The twelve months, indexed from one; entry 0 is "?" and entry 13 is null. */
/* @0x1006a2e8 */
extern const tv_ref g_month_names[14];

/*
 * Opcode 0x60: is there a token this far off with one of these flags?
 *
 * Walks `count` tokens away from the one given -- `count` is the operand --
 * forward or back according to `dir`, treating a token flagged 0x51 as not
 * counting and stopping dead at one flagged 0x54.  Each step lands on the next
 * token that is not transparent, and that token's d1c is looked up in `want`.
 * On a match the number of steps taken goes into rule_trail, signed, so the
 * caller knows how far away it was.
 */
/* @0x100211b0 */
int32_t TV_THISCALL Rule_Op60(TextIn *self, const uint32_t *want, Token *t,
                              int32_t dir)
{
    int32_t count = (int32_t)*self->rule_ip;
    int16_t steps = 0;
    int32_t i;

    self->rule_trail = 0;
    self->rule_ip = self->rule_ip + 1;
    if (t == NULL)
        return 0;
    for (i = 0; i < count; i++) {
        int32_t transparent = 1;

        while (transparent) {
            if (dir == -1) {
                t = t->prev;
                if (t == NULL || self->head == t)
                    return 0;
                steps--;
            } else {
                t = t->next;
                if (t == NULL)
                    return 0;
                steps++;
            }
            if (!Bits_Test(0x51, t->bits))
                transparent = 0;
            if (Bits_Test(0x54, t->bits))
                return 0;
        }
        if (Bits_Test((int32_t)t->d1c, want)) {
            self->rule_trail = steps;
            return 1;
        }
    }
    return 0;
}

/*
 * Opcode 0x82: say the number as an ordinal, and make it plural if the token
 * before it counts more than one.
 */
/* @0x10020950 */
int32_t TV_THISCALL Rule_Op82(TextIn *self, Token *t, uint32_t v)
{
    char buf[0x100];

    Number_Words(t->text, buf, 3, 2);
    Bits_Set(0x32, t->bits);
    if (t->prev != NULL && t->prev->num > 1)
        strcat(buf, g_str_s);
    if (AllocString(&t->text2, (int32_t)strlen(buf) + 5) == -1)
        return TextIn_Error(self, 0);
    strcpy(t->text2, buf);
    if (t->trail == 0)
        Bits_Set(0x50, t->bits);
    t->trail = ' ';
    t->d1c = v;
    return 1;
}

/*
 * Opcode 0x78: a date.  "3 de enero del 1997".
 *
 * Three tokens at most: the day, which is "primero" when it is 1 and the
 * cardinal otherwise; the month, from the table above, with "de " in front;
 * and, when a number follows that could be a year, "de " and the full year for
 * four digits between 1001 and 2099, or "del " and the number for two digits
 * between 51 and 99.  The cursor is left on the last token taken.
 */
/* @0x10020a50 */
int32_t TV_THISCALL Rule_Op78(TextIn *self, Token **tp, uint32_t v)
{
    char buf[0x50];
    Token *t = *tp;
    int32_t style;

    Number_Words(t->text, buf, t->num == 1 ? 1 : 2, 0);
    if (AllocString(&t->text2, (int32_t)strlen(buf) + 5) == -1)
        return TextIn_Error(self, 0);
    strcpy(t->text2, buf);
    t->trail = ' ';
    t->d1c = v;

    t = t->next;
    if (t == NULL)
        return TextIn_Error(self, 0);
    if (t->num <= 0 || t->num >= 0xd)
        return 0;
    if (AllocString(&t->text2, 0xf) == -1)
        return TextIn_Error(self, 0);
    strcpy(t->text2, g_str_de);
    strcat(t->text2, TV_REF(char, g_month_names[t->num]));
    t->trail = ' ';
    t->d1c = v;
    *tp = t;

    t = t->next;
    if (t == NULL || t->is_number == 0)
        return 1;
    style = 0;
    if (t->len == 4 && t->num > 1000 && t->num < 0x834) {
        style = 4;
        Number_Words(t->text, buf, 2, 0);
    } else if (t->len == 2 && t->num > 50 && t->num < 100) {
        style = 2;
        Number_Words(t->text, buf, 2, 0);
    }
    if (style == 0)
        return 1;
    if (AllocString(&t->text2, (int32_t)strlen(buf) + 7) == -1)
        return TextIn_Error(self, 0);
    strcpy(t->text2, style == 4 ? g_str_de : g_str_del);
    strcat(t->text2, buf);
    t->d1c = v;
    *tp = t;
    return 1;
}

/*
 * Opcode 0x64: put one of the interpreter's own words in the token's place.
 *
 * The operand names the word and also goes into both of the token's int16
 * fields.  With `plural` set, and when the token before counts more than one --
 * or, for v == 0xd, the two either side between them -- the word takes an s or
 * an es depending on whether it ends in a vowel.
 */
/* @0x10021470 */
int32_t TV_THISCALL Rule_Op64(TextIn *self, Token *t, uint32_t v, int32_t n,
                              int32_t plural)
{
    const char *w = TV_REF(char, g_rule_words[n]);

    if (AllocString(&t->text2, (int32_t)strlen(w) + 5) == -1)
        return TextIn_Error(self, 0);
    strcpy(t->text2, w);
    t->w0a = (int16_t)n;
    t->w08 = (int16_t)n;
    t->d1c = v;
    if (t->prev != NULL && t->prev->trail == 0)
        t->prev->trail = ' ';
    if (t->trail == 0) {
        t->trail = ' ';
        Bits_Set(0x50, t->bits);
    } else if (t->trail == '.' && !Bits_Test(0x49, t->bits)) {
        t->trail = ' ';
    }
    if (plural != 0) {
        int32_t len = (int32_t)strlen(t->text2);

        if (t->prev != NULL && len > 0) {
            int32_t num = t->prev->num;

            if (v == 0xd && t->next != NULL)
                num += t->next->num;
            if (num > 1)
                strcat(t->text2,
                       tv_strchr(g_str_vowels_plain,
                                 (int32_t)(int8_t)t->text2[len - 1]) != NULL
                           ? g_str_s : g_str_es);
        }
    }
    return 1;
}

/*
 * Opcode 0x80: say the number, as a count of whatever follows.
 *
 * More than seven digits is spelled out instead.  Mode 2 is the plain reading
 * and mode 1 the one used when the token already carries flag 0x4a, or when
 * Rule_Scan finds that flag within two tokens ahead.
 */
/* @0x10021960 */
int32_t TV_THISCALL Rule_Op80(TextIn *self, Token *t, uint32_t v)
{
    uint32_t want[3];
    char buf[0x100];
    int32_t mode = 2;

    want[0] = 0;
    want[1] = 0;
    want[2] = 0;
    if (t == NULL || t->text == NULL)
        return 0;
    if (strlen(t->text) > 7)
        return Rule_SpellOut(self, t, v, 0);
    if (Bits_Test(0x4a, t->bits)) {
        mode = 1;
    } else {
        Bits_Set(0x4a, want);
        if (Rule_Scan(self, want, t, 1, 2, 1))
            mode = 1;
    }
    Number_Words(t->text, buf, 2, mode);
    if (AllocString(&t->text2, (int32_t)strlen(buf) + 5) == -1)
        return TextIn_Error(self, 0);
    strcpy(t->text2, buf);
    t->d1c = v;
    if (t->trail == 0) {
        t->trail = ' ';
        Bits_Set(0x50, t->bits);
    }
    return 1;
}

/*
 * Opcode 0x85: read a run of digits two at a time.
 *
 * Up to seventeen digits, taken in pairs and each pair said as a number with a
 * space after it, which is how a telephone number or a long reference is read
 * out.  A pair beginning with a zero says "cero" and then the second digit on
 * its own.
 */
/* @0x10021fb0 */
int32_t TV_THISCALL Rule_Op85(TextIn *self, Token *t, uint32_t v)
{
    /* The original reserves three bytes for the pair and then has
     * Number_Words write the words back over the same buffer, which reaches
     * into the 0x61 bytes of frame that follow it and are used for nothing
     * else.  The size here is that whole span, so the frame is laid out as the
     * original laid it out and nothing else moves. */
    char pair[0x64];
    char scratch[0x64];
    char out[400];
    int32_t pairs, at = 0;

    out[0] = 0;
    /* The original reads t->len before it tests t for null, so a null token
     * faults there rather than returning; the order is safe here instead. */
    if (t == NULL || t->text == NULL)
        return 0;
    if (strlen(t->text) > 0x11)
        return 0;
    pairs = (int32_t)t->len;
    if (pairs > 0) {
        pairs = (pairs + 1) >> 1;
        do {
            at += 2;
            pair[0] = t->text[at - 2];
            pair[2] = 0;
            pair[1] = t->text[at - 1];
            if (pair[0] == '0') {
                /* Number_Words truncates what it is given, which is why the
                 * original hands it the string in .data rather than a copy;
                 * kept as it was. */
                Number_Words((char *)g_str_zero, scratch, 2, 2);
                strcat(out, scratch);
                strcat(out, g_str_space);
                Number_Words(&pair[1], scratch, 2, 2);
                strcat(out, scratch);
            } else {
                Number_Words(pair, pair, 2, 2);
                strcat(out, pair);
            }
            strcat(out, g_str_space);
        } while (--pairs != 0);
    }
    if (AllocString(&t->text2, (int32_t)strlen(out) + 5) == -1)
        return TextIn_Error(self, 0);
    strcpy(t->text2, out);
    t->d1c = v;
    return 1;
}

/*
 * Opcode 0x77: join this token onto the one before it.
 *
 * What each of them says -- text2 if it has one, its own text otherwise --
 * goes end to end into the previous token, and where the first ends with the
 * same character the second begins with, that character is said once.  The
 * token this was called on keeps its text; the previous one now says both.
 */
/* @0x100221f0 */
int32_t TV_THISCALL Rule_Op77(TextIn *self, Token *t, uint32_t v)
{
    char first[255];
    char second[128];
    Token *prev;
    const char *s;

    memcpy(first, g_str_space, 2);
    memset(first + 2, 0, sizeof first - 2);
    memcpy(second, g_str_space, 2);
    memset(second + 2, 0, sizeof second - 2);

    prev = t->prev;
    if (prev == NULL || self->head == prev)
        return 0;
    s = prev->text2 != NULL ? prev->text2 : prev->text;
    if (s != NULL)
        strcpy(first, s);
    s = t->text2 != NULL ? t->text2 : t->text;
    if (s != NULL)
        strcpy(second, s);
    {
        /* When the first string came out empty the original reads the byte
         * before its buffer, which is the last byte of the second one and is
         * always zero: the buffers are cleared above and nothing fills either
         * of them to the end.  So an empty first string compares zero, which
         * is what the 0 below is. */
        size_t len = strlen(first);
        char last = len > 0 ? first[len - 1] : 0;

        strcat(first, last == second[0] ? second + 1 : second);
    }
    /* the original does not check this one */
    AllocString(&prev->text2, (int32_t)strlen(first) + 5);
    strcpy(prev->text2, first);
    if (prev->d1c == 0)
        prev->d1c = v;
    return 1;
}
