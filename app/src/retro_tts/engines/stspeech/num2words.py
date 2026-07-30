# num2words.py - Convert integer strings to English words
# Ported from engine/src/num2words.c

_ones = [
    "", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
    "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
    "sixteen", "seventeen", "eighteen", "nineteen",
]

_tens = [
    "", "", "twenty", "thirty", "forty", "fifty",
    "sixty", "seventy", "eighty", "ninety",
]

_scale = ["", "thousand", "million", "billion", "trillion"]


def _chunk_to_words(n):
    """Convert a number 1-999 to words."""
    parts = []
    if n >= 100:
        parts.append(_ones[n // 100])
        parts.append("hundred")
        n %= 100
    if n >= 20:
        parts.append(_tens[n // 10])
        n %= 10
        if n > 0:
            parts.append(_ones[n])
    elif n > 0:
        parts.append(_ones[n])
    return " ".join(parts)


def num2words(digits):
    """Convert an integer string to English words.

    Args:
        digits: String of digits, optionally with leading minus sign.

    Returns:
        English words string, or None on invalid input.
    """
    if not digits:
        return None

    p = 0
    negative = False

    if digits[0] == '-':
        negative = True
        p = 1

    if p >= len(digits):
        return None

    # Validate all remaining chars are digits
    rest = digits[p:]
    if not rest.isdigit():
        return None

    # Skip leading zeros (but keep last digit if all zeros)
    start = rest.lstrip('0') or '0'

    if start == '0':
        return "zero"

    if len(start) > 15:
        return None  # beyond trillions

    # Parse into groups of 3 from the right
    groups = []
    end = len(start)
    while end > 0:
        gstart = max(0, end - 3)
        groups.append(int(start[gstart:end]))
        end = gstart

    parts = []
    if negative:
        parts.append("minus")

    for i in range(len(groups) - 1, -1, -1):
        if groups[i] == 0:
            continue
        parts.append(_chunk_to_words(groups[i]))
        if i > 0 and _scale[i]:
            parts.append(_scale[i])

    return " ".join(parts)
