try:
    from .parser_tables import (
        PHONEME_NAME_TABLE, STRESS_TABLE, PHONEME_FLAGS,
        COMBINED_PHONEME_LENGTH_TABLE,
        FLAG_VOWEL, FLAG_CONSONANT, FLAG_DIPHTHONG, FLAG_VOICED,
        FLAG_STOPCONS, FLAG_UNVOICED_STOPCONS, FLAG_PUNCT,
        FLAG_FRICATIVE, FLAG_LIQUID, FLAG_NASAL, FLAG_ALVEOLAR
    )
except ImportError:
    from parser_tables import (
        PHONEME_NAME_TABLE, STRESS_TABLE, PHONEME_FLAGS,
        COMBINED_PHONEME_LENGTH_TABLE,
        FLAG_VOWEL, FLAG_CONSONANT, FLAG_DIPHTHONG, FLAG_VOICED,
        FLAG_STOPCONS, FLAG_UNVOICED_STOPCONS, FLAG_PUNCT,
        FLAG_FRICATIVE, FLAG_LIQUID, FLAG_NASAL, FLAG_ALVEOLAR
    )
pR = 23  # R* phoneme index
pD = 57  # D* phoneme index
pT = 69  # T* phoneme index
FLAG_DIP_YX = 0x0020  # Diphthong ending with YX
FLAG_0008 = 0x0008    # Stop consonant related flag
def phoneme_has_flag(phoneme, flag):
    """Check if a phoneme has a specific flag."""
    if phoneme is None or phoneme < 0 or phoneme >= len(PHONEME_FLAGS):
        return False
    return (PHONEME_FLAGS[phoneme] & flag) != 0
def matches_bitmask(flags, mask):
    """Check if flags match a bitmask."""
    return (flags & mask) != 0
def full_match(sign1, sign2):
    """Match two character phoneme."""
    target = sign1 + sign2
    for i, name in enumerate(PHONEME_NAME_TABLE):
        if name == target and name[1] != '*':
            return i
    return False
def single_match(sign1):
    """Match single character phoneme."""
    target = sign1 + '*'
    for i, name in enumerate(PHONEME_NAME_TABLE):
        if name == target:
            return i
    return False
def parser1(input_str, add_phoneme, add_stress):
    """
    Parse phoneme string into phoneme indices.
    The input buffer contains phonemes and stress markers like:
        DHAX KAET IHZ AH5GLIY.
    Args:
        input_str: The phoneme string
        add_phoneme: Callback to add a phoneme index
        add_stress: Callback to add stress value
    """
    src_pos = 0
    while src_pos < len(input_str):
        sign1 = input_str[src_pos]
        sign2 = input_str[src_pos + 1] if src_pos + 1 < len(input_str) else ''
        match = full_match(sign1, sign2)
        if match is not False:
            src_pos += 2  # Skip both characters
            add_phoneme(match)
            continue
        match = single_match(sign1)
        if match is not False:
            src_pos += 1
            add_phoneme(match)
            continue
        match = len(STRESS_TABLE) - 1
        while match > 0 and sign1 != STRESS_TABLE[match]:
            match -= 1
        if match == 0:
            raise ValueError(f"Could not parse char {sign1}")
        add_stress(match)
        src_pos += 1
def parser2(insert_phoneme, set_phoneme, get_phoneme, get_stress):
    """
    Apply phoneme rewriting rules.
    Rules include:
    - <DIPHTHONG ENDING WITH WX> -> <DIPHTHONG ENDING WITH WX> WX
    - <DIPHTHONG NOT ENDING WITH WX> -> <DIPHTHONG NOT ENDING WITH WX> YX
    - UL -> AX L, UM -> AX M, UN -> AX N
    - T R -> CH R, D R -> J R
    - <VOWEL> R -> <VOWEL> RX
    - And more...
    """
    def handle_uw_ch_j(phoneme, pos):
        """Handle special cases for UW, CH, J."""
        if phoneme == 53:  # UW
            if phoneme_has_flag(get_phoneme(pos - 1), FLAG_ALVEOLAR):
                set_phoneme(pos, 16)  # UX
        elif phoneme == 42:  # CH
            insert_phoneme(pos + 1, 43, get_stress(pos))  # **
        elif phoneme == 44:  # J*
            insert_phoneme(pos + 1, 45, get_stress(pos))  # **
    def change_ax(position, suffix):
        """Change phoneme to AX + suffix."""
        set_phoneme(position, 13)  # AX
        insert_phoneme(position + 1, suffix, get_stress(position))
    pos = -1
    while True:
        pos += 1
        phoneme = get_phoneme(pos)
        if phoneme is None:
            break
        if phoneme == 0:
            continue
        if phoneme_has_flag(phoneme, FLAG_DIPHTHONG):
            suffix = 21 if phoneme_has_flag(phoneme, FLAG_DIP_YX) else 20
            insert_phoneme(pos + 1, suffix, get_stress(pos))
            handle_uw_ch_j(phoneme, pos)
            continue
        if phoneme == 78:
            change_ax(pos, 24)
            continue
        if phoneme == 79:
            change_ax(pos, 27)
            continue
        if phoneme == 80:
            change_ax(pos, 28)
            continue
        if phoneme_has_flag(phoneme, FLAG_VOWEL) and get_stress(pos):
            if get_phoneme(pos + 1) == 0:
                next_phoneme = get_phoneme(pos + 2)
                if next_phoneme is not None and phoneme_has_flag(next_phoneme, FLAG_VOWEL):
                    if get_stress(pos + 2):
                        insert_phoneme(pos + 2, 31, 0)  # Q
            continue
        prior_phoneme = get_phoneme(pos - 1) if pos > 0 else None
        if phoneme == pR:
            if prior_phoneme == pT:
                set_phoneme(pos - 1, 42)  # T R -> CH R
            elif prior_phoneme == pD:
                set_phoneme(pos - 1, 44)  # D R -> J R
            elif phoneme_has_flag(prior_phoneme, FLAG_VOWEL):
                set_phoneme(pos, 18)  # <VOWEL> R -> <VOWEL> RX
            continue
        if phoneme == 24 and phoneme_has_flag(prior_phoneme, FLAG_VOWEL):
            set_phoneme(pos, 19)  # <VOWEL> L -> <VOWEL> LX
            continue
        if prior_phoneme == 60 and phoneme == 32:
            set_phoneme(pos, 38)
            continue
        if phoneme == 60:
            next_phoneme = get_phoneme(pos + 1)
            if not phoneme_has_flag(next_phoneme, FLAG_DIP_YX) and next_phoneme is not None:
                set_phoneme(pos, 63)  # GX
            continue
        if phoneme == 72:
            next_phoneme = get_phoneme(pos + 1)
            if not phoneme_has_flag(next_phoneme, FLAG_DIP_YX) or next_phoneme is None:
                set_phoneme(pos, 75)  # KX
                phoneme = 75
        if phoneme_has_flag(phoneme, FLAG_UNVOICED_STOPCONS) and prior_phoneme == 32:
            set_phoneme(pos, phoneme - 12)
        elif not phoneme_has_flag(phoneme, FLAG_UNVOICED_STOPCONS):
            handle_uw_ch_j(phoneme, pos)
        if phoneme == 69 or phoneme == 57:
            if pos > 0 and phoneme_has_flag(get_phoneme(pos - 1), FLAG_VOWEL):
                next_ph = get_phoneme(pos + 1)
                if next_ph == 0:
                    next_ph = get_phoneme(pos + 2)
                if phoneme_has_flag(next_ph, FLAG_VOWEL) and not get_stress(pos + 1):
                    set_phoneme(pos, 30)  # DX
            continue
def copy_stress(get_phoneme, get_stress, set_stress):
    """
    Copy stress value from following vowel to consonant.
    Example: LOITER (LOY5TER) - stress 5 on OY copies to L as stress 6.
    """
    position = 0
    while True:
        phoneme = get_phoneme(position)
        if phoneme is None:
            break
        if phoneme_has_flag(phoneme, FLAG_CONSONANT):
            next_phoneme = get_phoneme(position + 1)
            if next_phoneme is not None and phoneme_has_flag(next_phoneme, FLAG_VOWEL):
                stress = get_stress(position + 1)
                if stress != 0 and stress < 0x80:
                    set_stress(position, stress + 1)
        position += 1
def set_phoneme_length(get_phoneme, get_stress, set_length):
    """Set phoneme length based on stress level."""
    position = 0
    while True:
        phoneme = get_phoneme(position)
        if phoneme is None:
            break
        stress = get_stress(position)
        if stress == 0 or stress > 0x7F:
            length = COMBINED_PHONEME_LENGTH_TABLE[phoneme] & 0xFF
        else:
            length = COMBINED_PHONEME_LENGTH_TABLE[phoneme] >> 8
        set_length(position, length)
        position += 1
def adjust_lengths(get_phoneme, set_length, get_length):
    """
    Apply various rules that adjust phoneme lengths.
    Rules:
    - Lengthen <!FRICATIVE> or <VOICED> between <VOWEL> and <PUNCTUATION> by 1.5
    - <VOWEL> <RX | LX> <CONSONANT> - decrease <VOWEL> length by 1
    - <VOWEL> <UNVOICED PLOSIVE> - decrease vowel by 1/8th
    - <VOWEL> <VOICED CONSONANT> - increase vowel by 1/4 + 1
    - <NASAL> <STOP CONSONANT> - set nasal = 5, consonant = 6
    - <STOP CONSONANT> {silence} <STOP CONSONANT> - shorten both to 1/2 + 1
    - <STOP CONSONANT> <LIQUID> - decrease <LIQUID> by 2
    """
    position = 0
    while get_phoneme(position) is not None:
        if not phoneme_has_flag(get_phoneme(position), FLAG_PUNCT):
            position += 1
            continue
        loop_index = position
        position -= 1
        while position > 1 and not phoneme_has_flag(get_phoneme(position), FLAG_VOWEL):
            position -= 1
        if position == 0:
            break
        vowel = position
        while position < loop_index:
            phoneme = get_phoneme(position)
            if not phoneme_has_flag(phoneme, FLAG_FRICATIVE) or phoneme_has_flag(phoneme, FLAG_VOICED):
                length = get_length(position)
                set_length(position, (length >> 1) + length + 1)
            position += 1
        position = loop_index + 1
    loop_index = -1
    while True:
        loop_index += 1
        phoneme = get_phoneme(loop_index)
        if phoneme is None:
            break
        position = loop_index
        if phoneme_has_flag(phoneme, FLAG_VOWEL):
            position += 1
            next_phoneme = get_phoneme(position)
            if not phoneme_has_flag(next_phoneme, FLAG_CONSONANT):
                if next_phoneme in (18, 19):
                    position += 1
                    if phoneme_has_flag(get_phoneme(position), FLAG_CONSONANT):
                        set_length(loop_index, get_length(loop_index) - 1)
                continue
            flags = PHONEME_FLAGS[next_phoneme] if next_phoneme is not None else (FLAG_CONSONANT | FLAG_UNVOICED_STOPCONS)
            if not matches_bitmask(flags, FLAG_VOICED):
                if matches_bitmask(flags, FLAG_UNVOICED_STOPCONS):
                    length = get_length(loop_index)
                    set_length(loop_index, length - (length >> 3))
                continue
            length = get_length(loop_index)
            set_length(loop_index, (length >> 2) + length + 1)
            continue
        if phoneme_has_flag(phoneme, FLAG_NASAL):
            position += 1
            next_phoneme = get_phoneme(position)
            if next_phoneme is not None and phoneme_has_flag(next_phoneme, FLAG_STOPCONS):
                set_length(position, 6)
                set_length(position - 1, 5)
            continue
        if phoneme_has_flag(phoneme, FLAG_STOPCONS):
            position += 1
            while get_phoneme(position) == 0:
                position += 1
            next_phoneme = get_phoneme(position)
            if next_phoneme is not None and phoneme_has_flag(next_phoneme, FLAG_STOPCONS):
                set_length(position, (get_length(position) >> 1) + 1)
                set_length(loop_index, (get_length(loop_index) >> 1) + 1)
            continue
        if position > 0 and phoneme_has_flag(phoneme, FLAG_LIQUID):
            if phoneme_has_flag(get_phoneme(position - 1), FLAG_STOPCONS):
                set_length(position, get_length(position) - 2)
def prolong_plosive_stop_consonants(get_phoneme, insert_phoneme, get_stress):
    """
    Makes plosive stop consonants longer by inserting following phonemes.
    """
    pos = -1
    while True:
        pos += 1
        index = get_phoneme(pos)
        if index is None:
            break
        if not phoneme_has_flag(index, FLAG_STOPCONS):
            continue
        if phoneme_has_flag(index, FLAG_UNVOICED_STOPCONS):
            x = pos
            while True:
                x += 1
                next_non_empty = get_phoneme(x)
                if next_non_empty != 0:
                    break
            if next_non_empty is not None:
                if phoneme_has_flag(next_non_empty, FLAG_0008) or next_non_empty in (36, 37):
                    continue
        length1 = COMBINED_PHONEME_LENGTH_TABLE[index + 1] & 0xFF if index + 1 < len(COMBINED_PHONEME_LENGTH_TABLE) else 0
        length2 = COMBINED_PHONEME_LENGTH_TABLE[index + 2] & 0xFF if index + 2 < len(COMBINED_PHONEME_LENGTH_TABLE) else 0
        insert_phoneme(pos + 1, index + 1, get_stress(pos), length1)
        insert_phoneme(pos + 2, index + 2, get_stress(pos), length2)
        pos += 2
def parse(input_str):
    """
    Parse phoneme string and return processed phoneme data.
    Args:
        input_str: The phoneme string from the reciter
    Returns:
        List of [phoneme_index, length, stress] tuples, or False on failure
    """
    if not input_str:
        return False
    stress = []
    phoneme_length = []
    phoneme_index = []
    def get_phoneme(pos):
        if pos < 0 or pos >= len(phoneme_index):
            return None
        return phoneme_index[pos]
    def set_phoneme(pos, value):
        phoneme_index[pos] = value
    def insert_phoneme(pos, value, stress_value, length=0):
        phoneme_index.insert(pos, value)
        phoneme_length.insert(pos, length)
        stress.insert(pos, stress_value)
    def get_stress_val(pos):
        return stress[pos] if 0 <= pos < len(stress) else 0
    def set_stress(pos, value):
        stress[pos] = value
    def get_length(pos):
        return phoneme_length[pos] if 0 <= pos < len(phoneme_length) else 0
    def set_length(pos, value):
        phoneme_length[pos] = value
    pos = [0]  # Use list for closure
    def add_phoneme(value):
        stress.append(0)
        phoneme_length.append(0)
        phoneme_index.append(value)
    def add_stress(value):
        if stress:
            stress[-1] = value
    try:
        parser1(input_str, add_phoneme, add_stress)
    except (ValueError, Exception):
        return False
    parser2(insert_phoneme, set_phoneme, get_phoneme, get_stress_val)
    copy_stress(get_phoneme, get_stress_val, set_stress)
    set_phoneme_length(get_phoneme, get_stress_val, set_length)
    adjust_lengths(get_phoneme, set_length, get_length)
    prolong_plosive_stop_consonants(get_phoneme, insert_phoneme, get_stress_val)
    result = []
    for i, phoneme in enumerate(phoneme_index):
        if phoneme:
            result.append([phoneme, phoneme_length[i], stress[i]])
    return result
