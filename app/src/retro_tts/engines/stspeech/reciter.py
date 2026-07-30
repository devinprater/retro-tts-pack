# reciter.py - Rule-based text-to-phoneme conversion
# Ported from engine/src/reciter.c
#
# Rule entry format (in all_rules bytes):
#   [match bytes]  - chars to match; high bit set on last byte
#   [prefix bytes] - context tests before match; high bit set on last
#   [suffix bytes] - context tests after match; high bit set on last
#   [output bytes] - phoneme output string, null-terminated
#
# Context test codes (lower 7 bits):
#   0:    no test (skip)
#   1-9:  context test function
#   >9:   literal character match

import struct
from .reciter_tables import (
    typetab, specials, pagea, letter_offsets,
    TYPE_LETTER, TYPE_VOWEL, TYPE_VOICED, TYPE_SIBILANT,
    TYPE_NASAL, TYPE_STOP,
)

# Combined rule data: specials + pagea laid out contiguously.
# letter_offsets are signed 16-bit offsets from position (len(all_rules)).
_all_rules = specials + pagea


def _get_rules_for_letter(idx):
    """Get rules start position for a given letter index (0=non-letter, 1-26=A-Z)."""
    if idx < 0 or idx >= 27:
        idx = 0
    # letter_offsets stores uint16 values; interpret as signed int16
    offset = struct.unpack('<h', struct.pack('<H', letter_offsets[idx]))[0]
    pos = len(_all_rules) + offset
    if pos < 0 or pos >= len(_all_rules):
        return None
    return pos


def _char_type(ch):
    """Get character type flags from typetab."""
    ch &= 0x7F
    if ch >= 96:
        return 0
    return typetab[ch]


def _ucase_copy(text):
    """Copy text converting to uppercase, stripping non-typed chars. Prepends space."""
    result = [' ']
    for c in text:
        ch = ord(c) & 0x7F
        if ch >= 0x60:
            ch &= 0x5F
        if typetab[ch] != 0:
            result.append(chr(ch))
    return ''.join(result)


def _check_prefix_rule(code, text, pos):
    """Check prefix context rule (tests backward from match start).

    Returns (matched: bool, new_pos: int).
    """
    if pos < 0:
        ch = 0
    else:
        ch = ord(text[pos]) & 0x7F

    if code == 1:  # NOT a letter
        return (not (_char_type(ch) & TYPE_LETTER), pos)

    elif code == 2:  # Is vowel
        return (bool(_char_type(ch) & TYPE_VOWEL), pos)

    elif code == 3:  # Is voiced consonant
        return (bool(_char_type(ch) & TYPE_VOICED), pos)

    elif code == 4:  # Is nasal/liquid
        return (bool(_char_type(ch) & TYPE_NASAL), pos)

    elif code == 5:  # Is sibilant, or H preceded by C/S
        if _char_type(ch) & TYPE_SIBILANT:
            return (True, pos)
        if ch == ord('H') and pos > 0:
            prev = ord(text[pos - 1]) & 0x7F
            if prev == ord('C') or prev == ord('S'):
                return (True, pos - 1)
        return (False, pos)

    elif code == 6:  # Is stop/fricative, or H preceded by C/S/T
        if _char_type(ch) & TYPE_STOP:
            return (True, pos)
        if ch == ord('H') and pos > 0:
            prev = ord(text[pos - 1]) & 0x7F
            if prev in (ord('C'), ord('S'), ord('T')):
                return (True, pos - 1)
        return (False, pos)

    elif code == 7:  # Is E, I, or Y
        return (ch in (ord('E'), ord('I'), ord('Y')), pos)

    elif code == 8:  # Skip back over voiced consonants
        p = pos
        while p >= 0 and (_char_type(ord(text[p]) & 0x7F) & TYPE_VOICED):
            p -= 1
        return (True, p)

    return (False, pos)


def _check_suffix_rule(code, text, pos):
    """Check suffix context rule (tests forward from match end).

    Returns (matched: bool, new_pos: int).
    """
    tlen = len(text)
    if pos >= tlen:
        ch = 0
    else:
        ch = ord(text[pos]) & 0x7F

    if code == 1:  # NOT a letter
        return (not (_char_type(ch) & TYPE_LETTER), pos + 1)

    elif code == 2:  # Is vowel
        return (bool(_char_type(ch) & TYPE_VOWEL), pos + 1)

    elif code == 3:  # Is voiced consonant
        return (bool(_char_type(ch) & TYPE_VOICED), pos + 1)

    elif code == 4:  # Is nasal/liquid
        return (bool(_char_type(ch) & TYPE_NASAL), pos + 1)

    elif code == 5:  # Is sibilant, or H followed by C/S
        if _char_type(ch) & TYPE_SIBILANT:
            return (True, pos + 1)
        if ch == ord('H') and pos + 1 < tlen:
            nxt = ord(text[pos + 1]) & 0x7F
            if nxt == ord('C') or nxt == ord('S'):
                return (True, pos + 2)
        return (False, pos + 1)

    elif code == 6:  # Is stop/fricative, or H followed by C/S/T
        if _char_type(ch) & TYPE_STOP:
            return (True, pos + 1)
        if ch == ord('H') and pos + 1 < tlen:
            nxt = ord(text[pos + 1]) & 0x7F
            if nxt in (ord('C'), ord('S'), ord('T')):
                return (True, pos + 2)
        return (False, pos + 1)

    elif code == 7:  # Is E, I, or Y
        return (ch in (ord('E'), ord('I'), ord('Y')), pos + 1)

    elif code == 8:  # Skip forward over voiced consonants
        p = pos
        while p < tlen and (_char_type(ord(text[p]) & 0x7F) & TYPE_VOICED):
            p += 1
        return (True, p)

    elif code == 9:  # ING suffix or ER/ES/ED/ELY/EFUL
        if ch == ord('I') and pos + 2 < tlen:
            if text[pos + 1] == 'N' and text[pos + 2] == 'G':
                return (True, pos + 3)
        if ch == ord('E') and pos + 1 < tlen:
            n = text[pos + 1]
            if n in ('R', 'S', 'D'):
                return (True, pos + 2)
            if n == 'L' and pos + 2 < tlen and text[pos + 2] == 'Y':
                return (True, pos + 3)
            if n == 'F' and pos + 2 < tlen and text[pos + 2] == 'U' \
                    and pos + 3 < tlen and text[pos + 3] == 'L':
                return (True, pos + 4)
        return (False, pos)

    return (False, pos)


def _convword(text, text_pos):
    """Try to match one rule entry against text at current position.

    Returns (phonemes: str, new_text_pos: int) on match, or (None, text_pos) on failure.
    """
    ch = text[text_pos] if text_pos < len(text) else '\0'
    if ch == '\0':
        return (None, text_pos)

    # Determine letter index for rule lookup
    idx = ord(ch) & 0x7F
    if _char_type(idx) & TYPE_LETTER:
        idx = idx - 0x40  # A=1 .. Z=26
    else:
        idx = 0
    if idx < 0 or idx >= 27:
        idx = 0

    rule_start = _get_rules_for_letter(idx)
    if rule_start is None:
        return (None, text_pos)

    rules = _all_rules
    rules_len = len(rules)
    r = rule_start

    while r < rules_len:
        # Stage 1: Match characters
        match_pos = text_pos
        ri = r
        matched = True

        while ri < rules_len:
            rb = rules[ri] & 0x7F
            is_last = rules[ri] & 0x80

            if match_pos >= len(text):
                matched = False
                break

            if rb != (ord(text[match_pos]) & 0x7F):
                matched = False
                break

            match_pos += 1
            ri += 1

            if is_last:
                break

        if not matched or ri >= rules_len:
            # Skip to next entry: find null terminator
            while ri < rules_len and rules[ri] != 0:
                ri += 1
            if ri < rules_len:
                ri += 1  # Skip null
            r = ri
            continue

        # Stage 2: Prefix context tests
        pre_pos = text_pos - 1
        prefix_ok = True

        while ri < rules_len:
            code = rules[ri] & 0x7F
            is_last = rules[ri] & 0x80
            ri += 1

            if code == 0:
                pass  # No test
            elif code <= 9:
                ok, pre_pos = _check_prefix_rule(code, text, pre_pos)
                if not ok:
                    prefix_ok = False
                    break
                if code != 8:
                    pre_pos -= 1
            else:
                # Literal char match backward
                if pre_pos < 0 or (ord(text[pre_pos]) & 0x7F) != code:
                    prefix_ok = False
                    break
                pre_pos -= 1

            if is_last:
                break

        if not prefix_ok:
            while ri < rules_len and rules[ri] != 0:
                ri += 1
            if ri < rules_len:
                ri += 1
            r = ri
            continue

        # Stage 3: Suffix context tests
        suf_pos = match_pos
        suffix_ok = True

        while ri < rules_len:
            code = rules[ri] & 0x7F
            is_last = rules[ri] & 0x80
            ri += 1

            if code == 0:
                pass
            elif code <= 9:
                ok, suf_pos = _check_suffix_rule(code, text, suf_pos)
                if not ok:
                    suffix_ok = False
                    break
            else:
                # Literal char match forward
                if suf_pos >= len(text) or (ord(text[suf_pos]) & 0x7F) != code:
                    suffix_ok = False
                    break
                suf_pos += 1

            if is_last:
                break

        if not suffix_ok:
            while ri < rules_len and rules[ri] != 0:
                ri += 1
            if ri < rules_len:
                ri += 1
            r = ri
            continue

        # Stage 4: Copy output phonemes
        output = []
        while ri < rules_len and rules[ri] != 0:
            output.append(chr(rules[ri]))
            ri += 1
        # Skip null
        if ri < rules_len:
            ri += 1

        return (''.join(output), match_pos)

    return (None, text_pos)


def reciter_text_to_phonemes(text):
    """Convert English text to phoneme string using rule-based reciter.

    Args:
        text: English text string.

    Returns:
        Phoneme string.
    """
    text_mode = _ucase_copy(text)
    result = []
    text_pos = 0

    while text_pos < len(text_mode):
        phonemes, new_pos = _convword(text_mode, text_pos)
        if phonemes is not None:
            result.append(phonemes)
            text_pos = new_pos
        else:
            text_pos += 1

    return ''.join(result)
