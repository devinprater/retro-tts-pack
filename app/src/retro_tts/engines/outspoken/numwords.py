# -*- coding: utf-8 -*-
"""Say numbers as words, because none of these engines can.

Named `numwords` and not `numbers`: `tools/` and the add-on's engine folder
both go on `sys.path`, so a module called `numbers` shadows the standard
library's for the whole process. It does not fail where you can see it -- it
failed here as sixty unrelated test collections at once.

MacinTalk's own rules spell digits out one at a time and stop there.  `RULZ`
bucket 26 is the digits, and it holds nothing but the ten names, so `30` comes
out as "three zero" and `25` as "two five".  The Amiga narrator behaves the
same way; where the Amiga *add-on* does better it is because its author put a
modern dictionary in front of the engine, not because the 1985 engine knew how
to count.

So this is an addition, not a restoration, and it belongs in front of the
letter-to-sound rules rather than inside them:

    text -> normalise() -> NRL rules -> phonemes -> engine

It is deliberately free of engine data.  Nothing here depends on MacinTalk,
which is why MacinTalk 2 and Pro get it for the same price.

**Predictable beats clever.**  A screen reader user hears these thousands of
times a day and needs to know what a given string will sound like, so there is
no year heuristic and no attempt to guess that `1984` is a date rather than a
count.  `read_year` exists for a caller that knows better.

    py -3 tools/numwords.py "you owe me 1,234.50 on the 3rd"
"""
import re
import sys

ONES = ("zero one two three four five six seven eight nine ten eleven twelve "
        "thirteen fourteen fifteen sixteen seventeen eighteen nineteen").split()
TENS = ("_ _ twenty thirty forty fifty sixty seventy eighty ninety").split()

#: Short scale, which is what English screen readers expect.
SCALES = [(10 ** 12, "trillion"), (10 ** 9, "billion"),
          (10 ** 6, "million"), (1000, "thousand")]

#: Irregular ordinals; the rest are formed by suffixing the cardinal.
ORDINALS = {"one": "first", "two": "second", "three": "third",
            "five": "fifth", "eight": "eighth", "nine": "ninth",
            "twelve": "twelfth"}

MINUS = "minus"
POINT = "point"


def cardinal(n):
    """0 -> 'zero', 1234 -> 'one thousand two hundred thirty four'.

    No "and" before the tens: American convention, which is what the phoneme
    rules and the voices were built around.  No hyphens either -- the rules
    treat a hyphen as punctuation and it would become a pause.
    """
    if n < 0:
        return MINUS + " " + cardinal(-n)
    if n < 20:
        return ONES[n]
    if n < 100:
        t, r = divmod(n, 10)
        return TENS[t] + (" " + ONES[r] if r else "")
    if n < 1000:
        h, r = divmod(n, 100)
        return ONES[h] + " hundred" + (" " + cardinal(r) if r else "")
    for value, name in SCALES:
        if n >= value:
            q, r = divmod(n, value)
            return cardinal(q) + " " + name + (" " + cardinal(r) if r else "")
    # Unreachable: anything at or above 1000 matches the thousand scale. Past
    # a trillion the recursion says "one thousand trillion" rather than
    # inventing "quadrillion", which is the right call for a screen reader.
    raise AssertionError("cardinal fell through for %d" % n)


def ordinal(n):
    """1 -> 'first', 22 -> 'twenty second'."""
    words = cardinal(n).split()
    last = words[-1]
    if last in ORDINALS:
        words[-1] = ORDINALS[last]
    elif last.endswith("y"):
        words[-1] = last[:-1] + "ieth"
    else:
        words[-1] = last + "th"
    return " ".join(words)


def read_year(n):
    """1984 -> 'nineteen eighty four'.

    Never applied automatically -- only a caller that knows the number is a
    year should ask, because `1984 items` is not `nineteen eighty four items`.
    """
    if not (1100 <= n <= 9999) or n % 1000 == 0:
        return cardinal(n)
    hi, lo = divmod(n, 100)
    if lo == 0:
        return cardinal(hi) + " hundred"
    if lo < 10:
        return cardinal(hi) + " oh " + ONES[lo]
    return cardinal(hi) + " " + cardinal(lo)


def digits(s):
    """'2024' -> 'two zero two four', for when spelling out is wanted."""
    return " ".join(ONES[int(c)] if c.isdigit() else c for c in s)


#: A signed number with optional thousands separators and an optional
#: fractional part, or an ordinal like `3rd`.  Ordered so the ordinal
#: alternative is tried first -- otherwise `3rd` matches as `3` and leaves a
#: stray `rd` behind.
#: The trailing `(?!\.\d)` is what keeps `192.168.0.1` whole.  Without it the
#: leading `192.168` matches as a decimal and the address is read as "one
#: hundred ninety two point one six eight.0.1" -- half converted, which is
#: worse than either extreme.  A bare `.5` is deliberately not matched either,
#: because it cannot be told apart from the end of a sentence.
_NUMBER = re.compile(r"""
    (?P<ord>  (?<![\w.])  \d+ (?:st|nd|rd|th)  \b )
  | (?P<num>  (?<![\w.])  -? \d{1,3}(?:,\d{3})+ (?:\.\d+)? (?![\w.])
            | (?<![\w.])  -? \d+  \.\d+                    (?![\w.])
            | (?<![\w.])  -? \d+                           (?![\w])  (?!\.\d) )
""", re.VERBOSE | re.IGNORECASE)


def normalise(text, spell_out=False):
    """Replace numbers in `text` with words.

    `spell_out` gives the engine's own behaviour, digit by digit, for a user
    who prefers it -- that is the setting worth exposing in the driver, since
    long identifiers and phone numbers really are easier to follow that way.
    """
    def sub(mo):
        # An ordinal always becomes a word.  Spelling `3rd` out character by
        # character says "three r d", which helps nobody, so `spell_out` is
        # about long numbers and does not reach here.
        if mo.group("ord"):
            return ordinal(int(mo.group("ord")[:-2]))

        raw = mo.group("num")
        neg = raw.startswith("-")
        raw = raw.lstrip("-").replace(",", "")
        whole, _, frac = raw.partition(".")
        say = digits if spell_out else (lambda s: cardinal(int(s)))
        out = say(whole or "0")
        if frac:
            # The fractional part is always digit by digit either way: nobody
            # reads .50 as "fifty".
            out += " " + POINT + " " + digits(frac)
        return (MINUS + " " + out) if neg else out

    return _NUMBER.sub(sub, text)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    for t in args:
        print("%r\n  -> %s" % (t, normalise(t)))
        print("  -> %s   (spelled out)" % normalise(t, spell_out=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
