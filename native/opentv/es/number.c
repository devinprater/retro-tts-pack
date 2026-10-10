/*
 * Spanish numbers in words.
 *
 * Six of the rule interpreter's handlers end at this function's door, all
 * through the four-argument thunk in rule.c.  It takes a string of digits
 * and writes the words for it, recursing once per three-digit group.
 *
 * The recursion is the shape worth understanding.  A call with more than
 * three digits cuts the last three off, calls itself on what is left with
 * the group level raised by one, puts the three digits back, appends the
 * scale word for *its own* level, and then says its own group.  So the most
 * significant group is spoken first, by the innermost call, and the scale
 * words fall out in the right places on the way back up:
 *
 *     1234567  ->  level 0 cuts "567", recurses on "1234"
 *                  level 1 cuts "234", recurses on "1"
 *                  level 2 says "un", returns 1
 *                  level 1 appends scale[1] "millon", says "doscientos ..."
 *                  level 0 appends scale[0] "mil", says "quinientos ..."
 *
 * The scale tables are indexed by level % 4, not % 3, which is what gives
 * the Spanish long scale its two thousands: mil, millon, mil, billon.  Ten
 * to the ninth is "mil millones", and that is where the odd-looking extra
 * append at level 2 comes from.
 *
 * The return value is the number the call parsed: the whole number at level
 * 0 and the group's value below that.  Its caller uses the first for
 * Token.num and the second to choose singular or plural scale words.
 *
 * Four styles and two genders:
 *
 *     style 1   ordinals -- primero, decimo, vigesimo, centesimo
 *     style 2   cardinals, but never the standalone "uno"
 *     style 3   fractions -- medio, tercio, and -avo above twenty
 *     style 4   cardinals
 *     mode 1    feminine, which rewrites the last letter of a word to "a"
 *
 * The feminine rewrites are in-place edits of the output, each at a fixed
 * distance from the end, and they are reproduced at those distances.  The
 * hundreds one is the interesting case: it steps back over a trailing "s"
 * first, so "doscientos " becomes "doscientas ".
 *
 * The tables are the shipped ones and several are misspelled: "quarto" for
 * cuarto, "quatroscientos" for cuatrocientos, "setescientos",
 * "ochoscientos", "novescientos", "cuadragstimo" for cuadragesimo.  There is
 * also no "y" between twenty and the unit -- the tens entry is "veinti " and
 * the unit follows it with a space, so twenty-one comes out "veinti un".
 * All of it is audible and all of it stays.
 */
#include "es_engine.h"

/* @0x1006a110 */
extern const tv_ref g_num_units[20];
/* @0x1006a160 */
extern const tv_ref g_num_units_ord[20];
/* @0x1006a1b0 */
extern const tv_ref g_num_frac[22];
/* @0x1006a208 */
extern const tv_ref g_num_tens[10];
/* @0x1006a230 */
extern const tv_ref g_num_tens_ord[10];
/* @0x1006a258 */
extern const tv_ref g_num_hund[10];
/* @0x1006a280 */
extern const tv_ref g_num_hund_ord[10];
/* @0x1006a2a8 */
extern const tv_ref g_num_scale[4];
/* @0x1006a2b8 */
extern const tv_ref g_num_scale_pl[4];
/* @0x1006a2c8 */
extern const tv_ref g_num_scale_ord[4];
/* @0x1006a2d8 */
extern const tv_ref g_num_scale_ord_pl[4];

/* @0x1006ae2c */
extern const char g_str_una[];
/* @0x1006a874 */
extern const char g_str_uno[];
/* @0x10069bc0 */
extern const char g_str_un[];
/* @0x1006ae24 */
extern const char g_str_veinte[];
/* @0x1006ae1c */
extern const char g_str_cien[];
/* @0x1006ae14 */
extern const char g_str_cero[];
/* @0x1006ae10 */
extern const char g_str_y[];
/* @0x1006ae0c */
extern const char g_str_avo[];

/* @0x100200d0 */
int32_t TV_CDECL Number_WordsEx(char *digits, char *out, int32_t style,
                                int32_t mode, int32_t level, int32_t flags)
{
    char group[8];
    int32_t value;              /* what this call reports */
    int32_t n;                  /* the value of this call's own group */
    int32_t tens, sty;
    int32_t only_group = 1;     /* no recursion happened: clear the output */
    size_t len = strlen(digits);

    if (len > 3) {
        char *tail = digits + len - 3;
        int32_t above;

        only_group = 0;
        memcpy(group, tail, strlen(tail) + 1);
        value = tv_atoi(group);
        if (value < 0)
            return -1;
        *tail = 0;
        above = Number_WordsEx(digits, out, style, mode, level + 1,
                               value != 0 ? (flags | 1) : flags);
        strcat(digits, group);          /* put back what was cut off */
        sty = style;
        /* The plural scale word is used for anything above one -- and
         * also at level 3 whatever is above it, which is the one place the
         * index running 0..3 rather than 0..2 shows through. */
        if (sty == 1) {
            if (above == 1)
                strcat(out, TV_REF(char, g_num_scale_ord[level % 4]));
            else if (above > 1 || level % 4 == 3)
                strcat(out, TV_REF(char, g_num_scale_ord_pl[level % 4]));
            /* "mil millonesimos".  Unlike the cardinal case below, this one
             * does not ask whether there was anything above it. */
            if (level == 2 && value == 0)
                strcat(out, TV_REF(char, g_num_scale_ord_pl[1]));
        } else {
            if (above == 1)
                strcat(out, TV_REF(char, g_num_scale[level % 4]));
            else if (above > 1 || level % 4 == 3)
                strcat(out, TV_REF(char, g_num_scale_pl[level % 4]));
            if (level == 2 && above != 0 && value == 0)
                strcat(out, TV_REF(char, g_num_scale_pl[1]));      /* "mil millones" */
        }
    } else {
        memcpy(group, digits, len + 1);
        sty = style;
    }

    if (only_group)
        out[0] = 0;
    value = tv_atoi(group);
    n = value;
    if (level == 0)
        value = tv_atoi(digits);        /* the whole number, for the caller */

    /* A fraction of a hundred or more, or one below the units group, is
     * named as an ordinal instead. */
    if (sty == 3 && (n >= 100 || level > 0))
        sty = 1;

    if (sty == 2 || sty == 4) {
        if (n == 1) {
            if (level % 2 == 1)
                return 1;
            if (level <= 0 && sty != 2) {
                strcat(out, mode == 1 ? g_str_una : g_str_uno);
                return value;
            }
        } else if (n == 0x14) {
            strcat(out, g_str_veinte);
            return value;
        } else if (n == 0x64) {
            strcat(out, g_str_cien);
            return value;
        }
    } else if (sty == 3) {
        if (n < 0x15) {
            strcat(out, TV_REF(char, g_num_frac[n]));
            return value;
        }
    } else if (sty == 1) {
        if (level % 2 == 1)
            return 1;
    }

    if (n / 100 != 0) {
        strcat(out, (sty == 1 || sty == 3) ? TV_REF(char, g_num_hund_ord[n / 100])
                                           : TV_REF(char, g_num_hund[n / 100]));
        if (mode == 1) {
            char *p = out + strlen(out) - 2;
            if (*p == 's')
                p--;
            if (*p == 'o')
                *p = 'a';
        }
    }

    tens = n % 100;
    if (tens < 0x14) {
        /* "cero" only for a number that is a single group of zero */
        if (n == 0 && level == 0 && only_group != 0)
            strcat(out, g_str_cero);
        if (sty == 1) {
            strcat(out, TV_REF(char, g_num_units_ord[tens]));
            if (mode == 1)
                out[strlen(out) - 1] = 'a';
            return value;
        }
        if (n == 1 && mode == 1)
            strcat(out, g_str_una);
        else
            strcat(out, TV_REF(char, g_num_units[tens]));
        return value;
    }

    if (sty == 1) {
        strcat(out, TV_REF(char, g_num_tens_ord[tens / 10]));
        if (mode == 1)
            out[strlen(out) - 2] = 'a';
        strcat(out, TV_REF(char, g_num_units_ord[tens % 10]));
        if (mode == 1)
            out[strlen(out) - 1] = 'a';
        return value;
    }

    strcat(out, TV_REF(char, g_num_tens[tens / 10]));
    if (tens > 0x1e && tens % 10 != 0)
        strcat(out, g_str_y);
    if (tens % 10 == 1) {
        if (mode == 1)
            strcat(out, g_str_una);
        else if (sty == 4 && level == 0)
            strcat(out, g_str_uno);
        else
            strcat(out, g_str_un);
    } else {
        strcat(out, TV_REF(char, g_num_units[tens % 10]));
    }
    if (sty == 3) {
        char *p = out + strlen(out) - 1;
        if (*p == 'a' || *p == ' ')
            *p = 0;
        strcat(out, g_str_avo);
    }
    return value;
}

/*
 * A Roman numeral's value, or -1 if the string is not one.
 *
 * One letter at a time, with a look at the next so that the subtractive pairs
 * come out right: CD and CM, IV and IX, XL and XC.  Every letter also has a
 * set of letters it is allowed to be followed by, and anything outside that
 * set -- or outside C D I L M V X at all -- makes the whole string not a
 * numeral.  So IL and VX are refused rather than added up, and the check is
 * what makes "MIX" a number and "MIXED" a word.
 *
 * It does not check that the letters descend, so IXIX comes out as 18.
 */
/* @0x10022380 */
int32_t TV_CDECL Roman_Value(const char *s)
{
    int32_t total = 0;
    int32_t i = 1;
    uint8_t c = (uint8_t)s[0];

    for (;;) {
        int32_t next = (int32_t)(int8_t)s[i];

        /* the original bounds every look-ahead at 'X' before its table */
        if (c == 'C' || c == 'D' || c == 'I' || c == 'L' || c == 'M' ||
            c == 'X') {
            if ((uint32_t)next > 0x58u)
                return -1;
        }
        switch (c) {
        case 'C':
            if (next == 'D') {
                total += 400;
                i++;
            } else if (next == 'M') {
                total += 900;
                i++;
            } else if (next == 0 || next == 'C' || next == 'I' ||
                       next == 'L' || next == 'V' || next == 'X') {
                total += 100;
            } else {
                return -1;
            }
            break;
        case 'D':
            if (next == 0 || next == 'C' || next == 'I' || next == 'L' ||
                next == 'V' || next == 'X')
                total += 500;
            else
                return -1;
            break;
        case 'I':
            if (next == 'V') {
                total += 4;
                i++;
            } else if (next == 'X') {
                total += 9;
                i++;
            } else if (next == 0 || next == 'I') {
                total += 1;
            } else {
                return -1;
            }
            break;
        case 'L':
            if (next == 0 || next == 'I' || next == 'V' || next == 'X')
                total += 50;
            else
                return -1;
            break;
        case 'M':
            if (next == 0 || next == 'C' || next == 'D' || next == 'I' ||
                next == 'L' || next == 'M' || next == 'V' || next == 'X')
                total += 1000;
            else
                return -1;
            break;
        case 'V':
            if (next == 0 || next == 'I')
                total += 5;
            else
                return -1;
            break;
        case 'X':
            if (next == 'C') {
                total += 90;
                i++;
            } else if (next == 'L') {
                total += 40;
                i++;
            } else if (next == 0 || next == 'I' || next == 'V' ||
                       next == 'X') {
                total += 10;
            } else {
                return -1;
            }
            break;
        default:
            return -1;
        }
        c = (uint8_t)s[i];
        if (c == 0)
            return total;
        i++;
    }
}
