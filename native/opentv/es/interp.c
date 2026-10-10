/*
 * The rule interpreter.
 *
 * TextIn_Advance looks a token up, gets back a list of rules, and hands each
 * one to this function.  A rule is a stream of int16 words: one opcode per
 * word, operands in the words that follow, evaluated recursively.  The
 * opcode space runs 4..0x5b through an 88-entry jump table, with two values
 * handled outside it -- 3 is logical NOT and -31979 is a bare success.
 * Anything else is an error.
 *
 * Every call evaluates exactly one opcode and returns a flag.  The
 * combinators call back in for each of their operands, so a whole rule is
 * one call that unfolds into a tree.
 *
 *     3           not
 *     4..8        and, over two to six operands
 *     9..12       or, over two to five
 *     13, 14      true, false
 *     15..22      set, compare and range-test rule_trail
 *     23..28      move the cursor
 *     29          compare Token.w08 with an immediate
 *     33..38      test the token's flags
 *     39..59      scan for flags, backwards, forwards or both
 *     60..91      do something: say, spell, insert, remove, detach, move
 *
 * Three arguments come in besides the engine: `cursor`, which is where the
 * interpreter's idea of the current token lives and which most of the moving
 * opcodes write; `anchor`, which opcode 27 restores the cursor from and which
 * the removing opcodes keep out of the way of themselves; and `v`, a value
 * the handlers store into the token they act on.
 *
 * None of the combinators short-circuit.  Every operand of an `and` is
 * evaluated even after one has failed, which matters because operands have
 * side effects -- moving the cursor, setting rule_trail, rewriting a token.
 *
 * Two oddities are kept because they are the original's.  Opcode 28 walks
 * the list using the low half of the register holding `self`, which it gets
 * away with only because nothing reads `self` again before the return; here
 * that is just a loop counter.  And four of the scan opcodes ask Rule_Scan
 * for a `check` of 2, which makes it walk and always answer no.
 *
 * Eight of the handlers are still the original's, bound through the DLL:
 * nothing in the corpus reaches them, so there is nothing to check a
 * decompilation of them against yet.  They are named for the opcode that
 * calls them, which is the only thing established about them.
 */
#include "es_engine.h"

/* the next int16 of the rule stream, sign-extended */
static int32_t next_op(TextIn *self)
{
    int32_t v = *self->rule_ip;

    self->rule_ip++;
    return v;
}

static void take_bits(TextIn *self, uint32_t *set, int n)
{
    int i;

    for (i = 0; i < n; i++)
        Bits_Set(next_op(self), set);
}

/* Step over `n` tokens in the list, refusing to run off either end.  `sig`
 * asks for only the tokens that are not transparent, which is flag 0x51 --
 * the same flag Rule_Scan steps over. */
static int32_t walk(TextIn *self, Token **tp, int32_t n, int32_t back, int sig)
{
    Token *t = *tp;
    int16_t left = (int16_t)n;

    while (left != 0) {
        left--;
        for (;;) {
            Token *nx = back ? t->prev : t->next;
            if (nx == NULL)
                return 0;
            if (back && self->head == nx)
                return 0;
            t = nx;
            if (!sig || !Bits_Test(0x51, t->bits))
                break;
        }
    }
    *tp = t;
    return 1;
}

/* @0x1001e5d0 */
int32_t TV_THISCALL Rule_Eval(TextIn *self, Token **cursor, Token **anchor,
                              uint32_t v)
{
    uint32_t set[3];
    Token *t = *cursor;
    int32_t op = next_op(self);
    int16_t s0, s1, s2, s3, s4;
    int16_t saved;
    int32_t n, lo, hi;

    set[0] = set[1] = set[2] = 0;

    if (op <= 3) {
        if (op == 3)
            return Rule_Eval(self, cursor, anchor, v) == 0;
        if (op == -31979)
            return 1;
        return TextIn_Error(self, 0);
    }
    if ((uint32_t)(op - 4) > 0x57)
        return TextIn_Error(self, 0);

    switch (op) {
    /* ---- combinators.  Every operand runs; none of them short-circuit. */
    case 4:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 && s1 != 0;
    case 5:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 && s1 != 0 && s2 != 0;
    case 6:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s3 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 && s1 != 0 && s2 != 0 && s3 != 0;
    case 7:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s3 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s4 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 && s1 != 0 && s2 != 0 && s3 != 0 && s4 != 0;
    case 8: {
        int16_t s5;
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s3 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s4 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s5 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 && s1 != 0 && s2 != 0 && s3 != 0 && s4 != 0 && s5 != 0;
    }
    case 9:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 || s1 != 0;
    case 10:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 || s1 != 0 || s2 != 0;
    case 11:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s3 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 || s1 != 0 || s2 != 0 || s3 != 0;
    case 12:
        s0 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s1 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s2 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s3 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        s4 = (int16_t)Rule_Eval(self, cursor, anchor, v);
        return s0 != 0 || s1 != 0 || s2 != 0 || s3 != 0 || s4 != 0;

    case 13:
        return 1;
    case 14:
        return 0;

    /* ---- rule_trail, the interpreter's one register */
    case 15:
        self->rule_trail = (int16_t)next_op(self);
        return 1;
    case 16:
        return (int16_t)(self->rule_trail - (int16_t)next_op(self)) == 0;
    case 17:
        saved = self->rule_trail;
        Rule_Eval(self, cursor, anchor, v);
        return (int16_t)(self->rule_trail - saved) == 0;
    case 18:
        lo = next_op(self);
        hi = next_op(self);
        return (int16_t)lo <= self->rule_trail && (int16_t)hi >= self->rule_trail;
    case 19:
        return self->rule_trail < (int16_t)next_op(self);
    case 20:
        saved = self->rule_trail;
        Rule_Eval(self, cursor, anchor, v);
        return self->rule_trail > saved;
    case 21:
        return self->rule_trail > (int16_t)next_op(self);
    case 22:
        saved = self->rule_trail;
        Rule_Eval(self, cursor, anchor, v);
        return self->rule_trail < saved;

    /* ---- moving the cursor */
    case 23:
        if (!walk(self, &t, next_op(self), 1, 0))
            return 0;
        *cursor = t;
        return 1;
    case 24:
        if (!walk(self, &t, next_op(self), 1, 1))
            return 0;
        *cursor = t;
        return 1;
    case 25:
        if (!walk(self, &t, next_op(self), 0, 0))
            return 0;
        *cursor = t;
        return 1;
    case 26:
        if (!walk(self, &t, next_op(self), 0, 1))
            return 0;
        *cursor = t;
        return 1;
    case 27:
        *cursor = *anchor;
        return 1;
    case 28: {
        /* By rule_trail, either way, and without any of the checks the
         * opcodes above make: this one will walk off the end. */
        int16_t k = self->rule_trail;
        if (k < 0) {
            int16_t j = (int16_t)-k;
            while (j > 0) {
                t = t->prev;
                j--;
            }
        } else {
            while (k > 0) {
                t = t->next;
                k--;
            }
        }
        *cursor = t;
        return 1;
    }
    case 29:
        return (int16_t)(t->w08 - (int16_t)next_op(self)) == 0;

    /* ---- the token's flags */
    case 33:
        return Rule_MatchTrail(self, t);
    case 34:
        take_bits(self, set, 1);
        return Rule_TestBits(set, t, 2);
    case 35:
        take_bits(self, set, 2);
        return Rule_TestBits(set, t, 1);
    case 36:
        take_bits(self, set, 3);
        return Rule_TestBits(set, t, 1);
    case 37:
        take_bits(self, set, 2);
        return Rule_TestBits(set, t, 2);
    case 38:
        take_bits(self, set, 3);
        return Rule_TestBits(set, t, 2);

    /* ---- scanning for flags.  The count follows the flag numbers. */
    case 39: case 40: case 41: case 42: case 43:
        take_bits(self, set, op - 38);
        return Rule_Scan(self, set, t, -1, next_op(self), 1);
    case 44: case 45:
        take_bits(self, set, op - 42);
        return Rule_Scan(self, set, t, -1, next_op(self), 2);
    case 46: case 47: case 48: case 49: case 50:
        take_bits(self, set, op - 45);
        return Rule_Scan(self, set, t, 1, next_op(self), 1);
    case 51: case 52:
        take_bits(self, set, op - 49);
        return Rule_Scan(self, set, t, 1, next_op(self), 2);
    case 53: case 54: case 55: case 56: case 57:
        take_bits(self, set, op - 52);
        n = next_op(self);
        if (Rule_Scan(self, set, t, -1, n, 1))
            return 1;
        return Rule_Scan(self, set, t, 1, n, 1);
    case 58: case 59:
        take_bits(self, set, op - 56);
        n = next_op(self);
        if (Rule_Scan(self, set, t, -1, n, 2))
            return 1;
        /* Rewound so the second pass reads the same count again, which is
         * what Rule_Op60 below needs and what this one does anyway. */
        self->rule_ip--;
        return Rule_Scan(self, set, t, 1, n, 2);
    case 60: case 61:
        take_bits(self, set, op - 59);
        if (Rule_Op60(self, set, t, -1))
            return 1;
        self->rule_ip--;
        return Rule_Op60(self, set, t, 1);

    /* ---- the handlers */
    case 62:
        return Rule_SayRecord(self, t, v, 1);
    case 63:
        return Rule_SayRecord(self, t, v, 0);
    case 64:
        return Rule_Op64(self, t, v, next_op(self), 1);
    case 65:
        return Rule_Op64(self, t, v, next_op(self), 0);
    case 66:
        return Rule_SpellOut(self, t, v, 1);
    case 68:
        Bits_Set(next_op(self), t->bits);
        return 1;
    case 69:
        Bits_Clear(next_op(self), t->bits);
        return 1;
    case 70:
        return Rule_SetTrail(self, t, v);
    case 71:
        return Rule_InsertWord(self, t, v, 1);
    case 72:
        return Rule_InsertWord(self, t, v, -1);
    case 73:
        if (*anchor == t)
            *anchor = t->prev;
        *cursor = TextIn_RemoveToken(self, t, -1);
        return 1;
    case 74:
        if (*anchor == t)
            *anchor = t->prev;
        *cursor = TextIn_Detach(self, t);
        return 1;
    case 75:
        *cursor = TextIn_Reattach(self, t, -1);
        return 1;
    case 76:
        *cursor = TextIn_Reattach(self, t, 1);
        return 1;
    case 77:
        return Rule_Op77(self, t, v);
    case 78:
        if ((int16_t)Rule_Op78(self, &t, v) == 1)
            *cursor = t;
        return 1;
    case 79:
        return Rule_SayNumberText(self, t, v);
    case 80:
        return Rule_Op80(self, t, v);
    case 81:
        return Rule_SayNumberOrSpell(self, t, v);
    case 82:
        return Rule_Op82(self, t, v);
    case 83:
        n = (int16_t)Rule_SayGroupedNumber(self, &t, v, 0);
        *anchor = t;
        *cursor = t;
        return n;
    case 84:
        n = (int16_t)Rule_SayGroupedNumber(self, &t, v, 1);
        *anchor = t;
        *cursor = t;
        return n;
    case 85:
        return Rule_Op85(self, t, v);
    case 86:
        return Rule_SayNumber(self, t, v);
    case 87:
        return Rule_Acronym(self, t, v);

    /* ---- reading something about the token into rule_trail */
    case 88:
        if (t->text == NULL) {
            self->rule_trail = 0;
            return 1;
        }
        self->rule_trail = (int16_t)(int8_t)t->text[0];
        return 1;
    case 89: {
        int16_t len;
        if (t->text == NULL) {
            self->rule_trail = 0;
            return 1;
        }
        len = (int16_t)strlen(t->text);
        self->rule_trail = len;
        if (len == 0)
            return 1;
        self->rule_trail = (int16_t)(int8_t)t->text[len - 1];
        return 1;
    }
    case 90:
        self->rule_trail = t->len;
        return 1;
    case 91:
        self->rule_trail = (int16_t)t->num;
        return 1;

    default:                        /* 30, 31, 32 and 67 */
        return TextIn_Error(self, 0);
    }
}

/* The twenty-two rule tables, one per flag a token can carry.  Each is a run
 * of rules; each rule starts with its own length as an int16 and the bytecode
 * Rule_Eval walks follows. */
/* @0x10069b08 */
extern int16_t g_rules_02[];
/* @0x10068d40 */
extern int16_t g_rules_03[];
/* @0x10068dc8 */
extern int16_t g_rules_05[];
/* @0x10068e90 */
extern int16_t g_rules_06[];
/* @0x10069980 */
extern int16_t g_rules_09[];
/* @0x10068f10 */
extern int16_t g_rules_0a[];
/* @0x100690d8 */
extern int16_t g_rules_0b[];
/* @0x1006986c */
extern int16_t g_rules_0c[];
/* @0x10069248 */
extern int16_t g_rules_0d[];
/* @0x10069308 */
extern int16_t g_rules_0e[];
/* @0x100695f0 */
extern int16_t g_rules_0f[];
/* @0x10069378 */
extern int16_t g_rules_10[];
/* @0x100693e8 */
extern int16_t g_rules_11[];
/* @0x100694b8 */
extern int16_t g_rules_12[];
/* @0x100694d0 */
extern int16_t g_rules_13[];
/* @0x100696b0 */
extern int16_t g_rules_14[];
/* @0x10069870 */
extern int16_t g_rules_15[];
/* @0x10069a78 */
extern int16_t g_rules_16[];
/* @0x100698a8 */
extern int16_t g_rules_17[];
/* @0x10068db0 */
extern int16_t g_rules_18[];
/* @0x100694cc */
extern int16_t g_rules_1a[];

/*
 * Tell the SAPI object which tokenizer mode is in force.
 *
 * Only reached from the ESC[nX arm of Rule_Select, and only when the engine
 * has a SAPI object at all.
 */
/* @0x1001dcc0 */
void TV_THISCALL TextIn_PublishMode(TextIn *self)
{
    if (self->engine != NULL && self->engine->sapi != NULL)
        self->engine->sapi->textin_mode = self->mode;
}

/*
 * Which rule tables apply to this token.
 *
 * Every bit set in the token's 96-bit flag set is looked at in turn, and the
 * ones between 2 and 26 name a rule table.  The bit numbers come out in
 * `bits`, NUL-terminated, and the matching tables in `tables`; thirty is the
 * most it will collect.  The bit the token's own d18 record names comes first
 * and is then skipped when the walk reaches it, so a token's own rule is tried
 * before the general ones.
 *
 * Bit 0x52 is not a rule but the mark TextIn_ReadEscape leaves on an ESC[
 * token: ESC[nX sets the tokenizer's mode and tells the host, and ESC[nI sets
 * ti_04.  Bit 10's arm tests the mode and then does the same thing either way.
 */
/* @0x1001e160 */
int32_t TV_THISCALL Rule_Select(TextIn *self, Token *t, int32_t *bits,
                               int16_t **tables)
{
    /* the bits are collected into a frame of the original's own before the
     * two the caller passes are filled, so the walk and the filter do not
     * tread on each other */
    int32_t seen[0x60];
    int32_t n = 0;
    int32_t *out;
    int32_t bit = 0;
    int32_t i, k = 0;

    seen[0] = 0;
    if (t->d18 != NULL) {
        seen[0] = *(const int32_t *)((const uint8_t *)t->d18 + 0x24);
        if (seen[0] != 0)
            n = 1;
    }
    out = &seen[n];
    for (;;) {
        bit = (int32_t)(int16_t)Bits_Next(bit, t->bits);
        if (bit == 0)
            break;
        if (seen[0] == bit)
            continue;
        *out++ = bit;
        n++;
    }
    if (n == 0)
        return 0;
    if (n >= 0x1e)
        n = 0x1e;

    for (i = 0; i < n; i++) {
        int32_t b = seen[i];
        int16_t *tbl = NULL;

        switch (b) {
        case 0x02: tbl = g_rules_02; break;
        case 0x03: tbl = g_rules_03; break;
        case 0x05: tbl = g_rules_05; break;
        case 0x06: tbl = g_rules_06; break;
        case 0x09: tbl = g_rules_09; break;
        case 0x0a: tbl = g_rules_0a; break;
        case 0x0b: tbl = g_rules_0b; break;
        case 0x0c: tbl = g_rules_0c; break;
        case 0x0d: tbl = g_rules_0d; break;
        case 0x0e: tbl = g_rules_0e; break;
        case 0x0f: tbl = g_rules_0f; break;
        case 0x10: tbl = g_rules_10; break;
        case 0x11: tbl = g_rules_11; break;
        case 0x12: tbl = g_rules_12; break;
        case 0x13: tbl = g_rules_13; break;
        case 0x14: tbl = g_rules_14; break;
        case 0x15: tbl = g_rules_15; break;
        case 0x16: tbl = g_rules_16; break;
        case 0x17: tbl = g_rules_17; break;
        case 0x18: tbl = g_rules_18; break;
        case 0x1a: tbl = g_rules_1a; break;
        case 0x52: {
            const char *s = t->text;

            if (s[3] == 'X') {
                self->mode = (int32_t)(int8_t)s[2] - '0';
                TextIn_PublishMode(self);
            } else if (s[3] == 'I') {
                self->ti_04 = (int32_t)(int8_t)s[2] - '0';
            }
            break;
        }
        default:
            break;
        }
        if (tbl != NULL) {
            tables[k] = tbl;
            bits[k] = b;
            k++;
        }
    }
    bits[k] = 0;
    return 1;
}

/*
 * Run the rules for one token.
 *
 * Every table Rule_Select found is walked, rule by rule, and every rule is
 * handed to Rule_Eval one opcode at a time.  What ends a rule is the opcode
 * left in rule_ip when Rule_Eval stops: 0x8315 stops the whole run, 0x8316
 * carries on with the next rule, 2 stops the run but keeps stepping the
 * current rule, and 1 abandons the table and moves to the next flag.  A rule
 * whose run stopped and that left the anchor where it started records which
 * flag matched in the token's d1c.
 */
/* @0x1001e490 */
int32_t TV_THISCALL Rule_Run(TextIn *self, Token **tp)
{
    int32_t bits[0x1e];
    int16_t *tables[0x1e];
    int16_t *rule = NULL;
    const int32_t *bitp = NULL;
    Token *first = *tp;
    int32_t rulelen = 0;
    int32_t i = 0;
    int stop = 0;

    if (!Rule_Select(self, first, bits, tables))
        return 1;
    if (bits[0] == 0)
        return 1;

next_list:
    if (stop)
        return 1;
    self->rule_ip = tables[i];
    if (*self->rule_ip == 0)
        goto next_bit;

next_rule:
    if (stop)
        goto next_bit;
    rule = self->rule_ip;
    rulelen = (int32_t)*rule;
    self->rule_ip = rule + 1;
    bitp = &bits[i];

step:
    if (!Rule_Eval(self, &first, tp, (uint32_t)*bitp))
        goto done_rule;
    {
        int16_t op = *self->rule_ip;

        if (op == (int16_t)0x8315) {
            stop = 1;
            goto done_rule;
        }
        if (op == 2) {
            stop = 1;
            self->rule_ip = self->rule_ip + 1;
            goto step;
        }
        if (op == (int16_t)0x8316) {
            stop = 0;
            goto done_rule;
        }
        if (op != 1)
            goto step;
        first = *tp;
        goto next_bit;
    }

done_rule:
    if (stop && *tp == first)
        first->d1c = (uint32_t)*bitp;
    self->rule_ip = rule + rulelen;
    first = *tp;
    if (*self->rule_ip != 0)
        goto next_rule;

next_bit:
    i++;
    if (bits[i] == 0)
        return 1;
    goto next_list;
}
