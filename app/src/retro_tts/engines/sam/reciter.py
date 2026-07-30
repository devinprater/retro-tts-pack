import re
try:
    from .reciter_tables import char_flags, rules, rules2
    from .constants import (
        FLAG_NUMERIC, FLAG_RULESET2, FLAG_VOICED, FLAG_0X08,
        FLAG_DIPHTHONG, FLAG_CONSONANT, FLAG_VOWEL_OR_Y, FLAG_ALPHA_OR_QUOT
    )
    from . import cmudict
except ImportError:
    from reciter_tables import char_flags, rules, rules2
    from constants import (
        FLAG_NUMERIC, FLAG_RULESET2, FLAG_VOICED, FLAG_0X08,
        FLAG_DIPHTHONG, FLAG_CONSONANT, FLAG_VOWEL_OR_Y, FLAG_ALPHA_OR_QUOT
    )
    try:
        import cmudict
    except ImportError:
        cmudict = None
def flags(c, flg):
    """Test if the char matches against the flags in the reciter table."""
    return (char_flags.get(c, 0) & flg) != 0
def flags_at(text, pos, flg):
    """Test flags at a specific position in the text."""
    if pos < 0 or pos >= len(text):
        return False
    return flags(text[pos], flg)
def is_one_of(c, lst):
    """Check if character is in the list."""
    return c in lst
class ReciterRule:
    """Generator for self processing rule instances."""
    TCS = ['T', 'C', 'S']
    EIY = ['E', 'I', 'Y']
    def __init__(self, rule_string):
        """
        Parse rule: 'xxx(yyy)zzz=foobar'
        xxx = prefix, yyy = match, zzz = suffix, foobar = target phonemes
        """
        parts = rule_string.split('=')
        self.target = parts[-1] if len(parts) > 1 else ''
        source = '='.join(parts[:-1]) if len(parts) > 1 else parts[0]
        if '(' in source and ')' in source:
            pre_part = source.split('(')
            self.pre = pre_part[0]
            rest = pre_part[1].split(')')
            self.match = rest[0]
            self.post = rest[1] if len(rest) > 1 else ''
        else:
            self.pre = ''
            self.match = source
            self.post = ''
        self.c = self.match[0] if self.match else ''
    def check_prefix(self, text, pos):
        """Test if the rule prefix matches."""
        for rule_pos in range(len(self.pre) - 1, -1, -1):
            rule_byte = self.pre[rule_pos]
            if not flags(rule_byte, FLAG_ALPHA_OR_QUOT):
                if rule_byte == ' ':
                    pos -= 1
                    if flags_at(text, pos, FLAG_ALPHA_OR_QUOT):
                        return False
                elif rule_byte == '#':
                    pos -= 1
                    if not flags_at(text, pos, FLAG_VOWEL_OR_Y):
                        return False
                elif rule_byte == '.':
                    pos -= 1
                    if not flags_at(text, pos, FLAG_0X08):
                        return False
                elif rule_byte == '&':
                    pos -= 1
                    if flags_at(text, pos, FLAG_DIPHTHONG):
                        pass
                    else:
                        pos -= 1
                        if pos >= 0 and text[pos:pos+2] in ['CH', 'SH']:
                            pass
                        else:
                            return False
                elif rule_byte == '@':
                    pos -= 1
                    if flags_at(text, pos, FLAG_VOICED):
                        pass
                    elif pos >= 0 and text[pos] == 'H':
                        if text[pos] not in self.TCS:
                            return False
                    else:
                        return False
                elif rule_byte == '^':
                    pos -= 1
                    if not flags_at(text, pos, FLAG_CONSONANT):
                        return False
                elif rule_byte == '+':
                    pos -= 1
                    if pos < 0 or text[pos] not in self.EIY:
                        return False
                elif rule_byte == ':':
                    while pos >= 0 and flags_at(text, pos - 1, FLAG_CONSONANT):
                        pos -= 1
                else:
                    return False
            else:
                pos -= 1
                if pos < 0 or text[pos] != rule_byte:
                    return False
        return True
    def check_suffix(self, text, pos):
        """Test if the rule suffix matches."""
        for rule_pos in range(len(self.post)):
            rule_byte = self.post[rule_pos]
            if not flags(rule_byte, FLAG_ALPHA_OR_QUOT):
                if rule_byte == ' ':
                    pos += 1
                    if flags_at(text, pos, FLAG_ALPHA_OR_QUOT):
                        return False
                elif rule_byte == '#':
                    pos += 1
                    if not flags_at(text, pos, FLAG_VOWEL_OR_Y):
                        return False
                elif rule_byte == '.':
                    pos += 1
                    if not flags_at(text, pos, FLAG_0X08):
                        return False
                elif rule_byte == '&':
                    pos += 1
                    if flags_at(text, pos, FLAG_DIPHTHONG):
                        pass
                    else:
                        pos += 1
                        if pos >= 2 and text[pos-1:pos+1] in ['HC', 'HS']:
                            pass
                        else:
                            return False
                elif rule_byte == '@':
                    pos += 1
                    if flags_at(text, pos, FLAG_VOICED):
                        pass
                    elif pos < len(text) and text[pos] == 'H':
                        if text[pos] not in self.TCS:
                            return False
                    else:
                        return False
                elif rule_byte == '^':
                    pos += 1
                    if not flags_at(text, pos, FLAG_CONSONANT):
                        return False
                elif rule_byte == '+':
                    pos += 1
                    if pos >= len(text) or text[pos] not in self.EIY:
                        return False
                elif rule_byte == ':':
                    while flags_at(text, pos + 1, FLAG_CONSONANT):
                        pos += 1
                elif rule_byte == '%':
                    if pos + 1 >= len(text):
                        return False
                    if text[pos + 1] != 'E':
                        if text[pos + 1:pos + 4] == 'ING':
                            pos += 3
                        else:
                            return False
                    else:
                        if not flags_at(text, pos + 2, FLAG_ALPHA_OR_QUOT):
                            pos += 1
                        elif pos + 2 < len(text) and text[pos + 2] in ['R', 'S', 'D']:
                            pos += 2
                        elif pos + 2 < len(text) and text[pos + 2] == 'L':
                            if pos + 3 < len(text) and text[pos + 3] == 'Y':
                                pos += 3
                            else:
                                return False
                        elif text[pos + 2:pos + 5] == 'FUL':
                            pos += 4
                        else:
                            return False
                else:
                    return False
            else:
                pos += 1
                if pos >= len(text) or text[pos] != rule_byte:
                    return False
        return True
    def matches(self, text, pos):
        """Test if the rule matches at the given position."""
        if not text[pos:].startswith(self.match):
            return False
        if not self.check_prefix(text, pos):
            return False
        return self.check_suffix(text, pos + len(self.match) - 1)
    def __call__(self, text, input_pos, callback):
        """Process the rule at the given position."""
        if self.matches(text, input_pos):
            callback(self.target, len(self.match))
            return True
        return False
def parse_rules(rules_string):
    """Parse rule string into organized dictionary."""
    rule_dict = {}
    for rule_str in rules_string.split('|'):
        if not rule_str:
            continue
        rule = ReciterRule(rule_str)
        c = rule.c
        if c not in rule_dict:
            rule_dict[c] = []
        rule_dict[c].append(rule)
    return rule_dict
def parse_rules_list(rules_string):
    """Parse rule string into a list."""
    rule_list = []
    for rule_str in rules_string.split('|'):
        if not rule_str:
            continue
        rule_list.append(ReciterRule(rule_str))
    return rule_list
_rules = parse_rules(rules)
_rules2 = parse_rules_list(rules2)
def _rule_based_phonemes(input_text):
    """
    Convert text to phoneme string using rule-based approach.
    Args:
        input_text: The input string to convert.
    Returns:
        The phoneme string, or False on failure.
    """
    text = ' ' + input_text.upper()
    input_pos = 0
    output = ''
    def success_callback(append, input_skip):
        nonlocal input_pos, output
        input_pos += input_skip
        output += append
    iteration_count = 0
    while input_pos < len(text) and iteration_count < 10000:
        iteration_count += 1
        current_char = text[input_pos]
        if current_char != '.' or flags_at(text, input_pos + 1, FLAG_NUMERIC):
            if flags(current_char, FLAG_RULESET2):
                for rule in _rules2:
                    if rule(text, input_pos, success_callback):
                        break
                continue
            if char_flags.get(current_char, 0) != 0:
                if not flags(current_char, FLAG_ALPHA_OR_QUOT):
                    return False
                if current_char in _rules:
                    for rule in _rules[current_char]:
                        if rule(text, input_pos, success_callback):
                            break
                continue
            output += ' '
            input_pos += 1
            continue
        output += '.'
        input_pos += 1
    return output
_ONES = ['', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine',
         'ten', 'eleven', 'twelve', 'thirteen', 'fourteen', 'fifteen', 'sixteen',
         'seventeen', 'eighteen', 'nineteen']
_TENS = ['', '', 'twenty', 'thirty', 'forty', 'fifty', 'sixty', 'seventy', 'eighty', 'ninety']
def _number_to_words(n):
    """Convert integer to words. Handles 0 to 999,999,999."""
    if n == 0:
        return 'zero'
    if n < 0:
        return 'negative ' + _number_to_words(-n)
    parts = []
    if n >= 1000000:
        parts.append(_number_to_words(n // 1000000) + ' million')
        n %= 1000000
    if n >= 1000:
        parts.append(_number_to_words(n // 1000) + ' thousand')
        n %= 1000
    if n >= 100:
        parts.append(_ONES[n // 100] + ' hundred')
        n %= 100
    if n >= 20:
        if n % 10:
            parts.append(_TENS[n // 10] + ' ' + _ONES[n % 10])
        else:
            parts.append(_TENS[n // 10])
    elif n > 0:
        parts.append(_ONES[n])
    return ' '.join(parts)
def expand_numbers(text):
    """
    Expand numbers in text to words.
    Examples:
        "60" -> "sixty"
        "123" -> "one hundred twenty three"
        "3.14" -> "three point one four"
    Args:
        text: Input text with numbers
    Returns:
        Text with numbers expanded to words
    """
    def replace_number(match):
        num_str = match.group(0)
        if '.' in num_str:
            parts = num_str.split('.')
            try:
                int_part = int(parts[0]) if parts[0] else 0
                result = _number_to_words(int_part) + ' point'
                for digit in parts[1]:
                    result += ' ' + _number_to_words(int(digit))
                return result
            except ValueError:
                return num_str
        try:
            n = int(num_str)
            return _number_to_words(n)
        except ValueError:
            return num_str
    return re.sub(r'-?\d+\.?\d*', replace_number, text)
def text_to_phonemes(input_text):
    """
    Convert text to phoneme string.
    Uses CMU Pronouncing Dictionary for known words, falls back to
    rule-based conversion for unknown words.
    Args:
        input_text: The input string to convert.
    Returns:
        The phoneme string, or False on failure.
    """
    if not input_text:
        return ''
    if cmudict is None:
        return _rule_based_phonemes(input_text)
    tokens = re.findall(r"[A-Za-z']+|[^A-Za-z']+", input_text)
    result_parts = []
    for token in tokens:
        if not token:
            continue
        if re.match(r"^[A-Za-z']+$", token):
            phonemes = cmudict.lookup(token)
            if phonemes:
                result_parts.append(phonemes)
            else:
                rule_result = _rule_based_phonemes(token)
                if rule_result and rule_result is not False:
                    result_parts.append(rule_result.strip())
                else:
                    pass
        else:
            rule_result = _rule_based_phonemes(token)
            if rule_result and rule_result is not False:
                result_parts.append(rule_result.strip())
    return ' '.join(result_parts)
