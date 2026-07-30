# synth.py - Phoneme synthesis pipeline
# Ported from engine/src/synth.c
#
# Pipeline stages:
# 1. Parse phoneme string -> phoneme index buffer
# 2. Phoneme substitution/expansion (context-dependent)
# 3. Duration adjustment
# 4. Amplitude scaling
# 5. Coarticulation (formant blending)
# 6. Frame-by-frame parameter generation
# 7. Pitch contour (stress-based + punctuation bends + drift)

from .synth_tables import (
    tabstart, NUM_PHONEMES, PH_ENTRY_SIZE, FRAME_SIZE, MAX_FRAMES,
    PH_NAME1, PH_NAME2, PH_DUR_BASE, PH_DUR_MOD,
    PH_F1_FREQ_H, PH_F1_FREQ_L, PH_F1_BW_H, PH_F1_BW_L,
    PH_F2_FREQ_H, PH_F2_FREQ_L, PH_F2_BW_H, PH_F2_BW_L,
    PH_F3_FREQ_H, PH_F3_FREQ_L, PH_F3_BW_H, PH_F3_BW_L,
    PH_SYNTH_TYPE, PH_VOICING, PH_ASPIRATION1, PH_COARTIC_A, PH_COARTIC_B,
    PH_AMP1, PH_AMP2, PH_AMP3, PH_AMP4, PH_AMP5, PH_AMP6,
    PH_VOICE_RAW, PH_RESERVED2, PH_BLEND_MODE,
    PH_PITCH1, PH_PITCH2, PH_VOICE_FLAG1, PH_VOICE_FLAG2,
    FR_SYNTH_CNT, FR_VOICE_CTRL, FR_NOISE_PERIOD,
    FR_F1_FREQ, FR_F1_AMP, FR_F2_FREQ, FR_F2_AMP,
    FR_F3_FREQ, FR_F3_AMP,
)

MAX_SLOTS = 256
SLOT_END = 0xFFFF


def _lookup_phoneme(c1, c2):
    """Look up a phoneme name in tabstart. Returns byte offset or -1."""
    for i in range(NUM_PHONEMES):
        if tabstart[i][PH_NAME1] == (c1 & 0xFF):
            n2 = tabstart[i][PH_NAME2]
            if n2 == 0x20:
                return i * PH_ENTRY_SIZE
            if n2 == (c2 & 0xFF):
                return i * PH_ENTRY_SIZE
    return -1


def _parse_phonemes(phonemes):
    """Parse phoneme string into slot list.

    Returns list of [phoneme_idx, stress, duration] lists, or None on error.
    """
    slots = []
    p = 0

    while p < len(phonemes) and len(slots) < MAX_SLOTS - 2:
        ch = ord(phonemes[p])
        p += 1

        # Stress/duration modifiers
        if ord('1') <= ch <= ord('9'):
            if slots:
                slots[-1][1] = ch - ord('0')
            continue

        # Pitch modifiers
        if ch == ord('>'):
            if slots:
                slots[-1][1] = 0x80
            continue
        if ch == ord('<'):
            if slots:
                slots[-1][1] = 0x40
            continue

        # Convert to uppercase
        if ord('a') <= ch <= ord('z'):
            ch -= 0x20

        c1 = ch
        c2 = ord(phonemes[p]) if p < len(phonemes) else 0
        if ord('a') <= c2 <= ord('z'):
            c2 -= 0x20

        idx = _lookup_phoneme(c1, c2)
        if idx >= 0:
            if tabstart[idx // PH_ENTRY_SIZE][PH_NAME2] != 0x20 and c2 != 0:
                p += 1  # consume second character
            slots.append([idx, 0, 0])
        else:
            return None  # Unknown phoneme

    # End marker: silence (phoneme 67)
    slots.append([67 * PH_ENTRY_SIZE, 0, 0])
    # Sentinel
    slots.append([SLOT_END, 0xFF, 0xFF])

    return slots


def _expand_phonemes(slots):
    """Stage 1: Expand compound phonemes and apply substitution rules."""
    i = 0
    while i < len(slots) and slots[i][0] != SLOT_END:
        idx = slots[i][0]
        ph = idx // PH_ENTRY_SIZE

        # Diphthong expansion
        if 0 <= ph <= 6:
            if ph <= 2:
                glide_ph = 8    # EY,AY,OY -> YX
            elif ph <= 5:
                glide_ph = 7    # AW,OW,UW -> WX
            else:
                glide_ph = 11   # AR -> ER

            slots.insert(i + 1, [glide_ph * PH_ENTRY_SIZE, slots[i][1], 0])
            i += 1  # skip inserted glide

        # Compound phonemes: UL(68), UM(69), UN(70), IL(71), IM(72), IN(73)
        if 68 <= ph <= 73:
            vowel_idx = (tabstart[ph][4] << 8) | tabstart[ph][5]
            cons_idx = (tabstart[ph][8] << 8) | tabstart[ph][9]
            slots[i][0] = vowel_idx
            slots.insert(i + 1, [cons_idx, slots[i][1], 0])

        # Refresh after possible modification
        idx = slots[i][0]
        ph = idx // PH_ENTRY_SIZE

        # Vowel-vowel: insert short silence
        if ph <= 21:
            if i + 1 < len(slots) and slots[i + 1][0] != SLOT_END:
                next_ph = slots[i + 1][0] // PH_ENTRY_SIZE
                if next_ph <= 21 and ph not in (6, 10, 11):
                    slots.insert(i + 1, [40 * PH_ENTRY_SIZE, 0, 0])

        # Unvoiced stop context rules (P:41-43, T:44-46, K:47-49)
        if ph in (41, 44, 47):
            if i + 1 < len(slots) and slots[i + 1][0] != SLOT_END:
                next_ph = slots[i + 1][0] // PH_ENTRY_SIZE
                if next_ph >= 63:
                    slots[i][0] = (ph + 1) * PH_ENTRY_SIZE
            else:
                slots[i][0] = (ph + 1) * PH_ENTRY_SIZE

        # Voiced stop context rules (B:50-52, D:53-55, G:56-58)
        if ph in (50, 53, 56):
            if i + 1 < len(slots) and slots[i + 1][0] != SLOT_END:
                next_ph = slots[i + 1][0] // PH_ENTRY_SIZE
                if next_ph >= 63:
                    slots[i][0] = (ph + 1) * PH_ENTRY_SIZE
            else:
                slots[i][0] = (ph + 1) * PH_ENTRY_SIZE

        i += 1


def _propagate_stress(slots):
    """Stage 2: Copy stress from stressed vowels to preceding consonants."""
    for i in range(len(slots)):
        if slots[i][0] == SLOT_END:
            break
        ph = slots[i][0] // PH_ENTRY_SIZE
        if 21 < ph < 63:
            # Consonant: check if next is a stressed vowel
            if i + 1 < len(slots) and slots[i + 1][0] != SLOT_END:
                next_ph = slots[i + 1][0] // PH_ENTRY_SIZE
                stress = slots[i + 1][1]
                if next_ph <= 21 and 0 < stress < 0xFF:
                    slots[i][1] = stress


def _process_flags(slots):
    """Stage 3: Handle phoneme flags for doubled consonants."""
    i = 0
    while i < len(slots) and slots[i][0] != SLOT_END:
        idx = slots[i][0]
        ph = idx // PH_ENTRY_SIZE
        if ph < NUM_PHONEMES:
            if tabstart[ph][32] & 0x80:
                # Double: insert next variant
                next_idx = idx + PH_ENTRY_SIZE
                slots.insert(i + 1, [next_idx, slots[i][1], 0])
        i += 1


def _calc_durations(slots):
    """Stage 4: Set duration based on base duration, stress, and context."""
    for i in range(len(slots)):
        if slots[i][0] == SLOT_END:
            break
        ph = slots[i][0] // PH_ENTRY_SIZE
        if ph >= NUM_PHONEMES:
            continue

        dur = tabstart[ph][PH_DUR_MOD]
        stress = slots[i][1]

        if stress & 0x80:
            extra = (dur >> 1) + 1
            dur += extra
        elif stress & 0x40:
            dur = (dur >> 1) + 1
        elif 0 < stress <= 9:
            dur = tabstart[ph][PH_DUR_BASE]

        slots[i][1] &= 0x3F
        slots[i][2] = dur


def _boost_durations(slots):
    """Stage 5: Increase duration of consonants before stressed vowels."""
    for i in range(len(slots)):
        if slots[i][0] == SLOT_END:
            break
        ph = slots[i][0] // PH_ENTRY_SIZE
        if ph >= NUM_PHONEMES:
            continue

        if ph > 63:
            # Scan backward for preceding consonants
            for j in range(i - 1, -1, -1):
                if slots[j][0] == SLOT_END:
                    break
                prev_ph = slots[j][0] // PH_ENTRY_SIZE
                if prev_ph >= NUM_PHONEMES:
                    break
                if prev_ph > 63:
                    break
                if prev_ph <= 21:
                    break
                voiced = tabstart[prev_ph][32] & 0x20
                stop = tabstart[prev_ph][32] & 0x40
                if not voiced or stop:
                    extra = (slots[j][2] >> 1) + 1
                    slots[j][2] += extra


def _scale_freq(val):
    """Scale 16-bit word value to 8-bit: (val + 0x10) >> 5."""
    return ((val + 0x10) >> 5) & 0xFF


def _scale_amp(val):
    """Scale amplitude: 3*x - 89, clamped to 0, >> 2."""
    v = val * 3 - 0x59
    if v < 0:
        v = 0
    return (v >> 2) & 0xFF


def _coartic_scale(value, mode):
    """Scale subordinate's value by dominant's blend mode."""
    if mode == 0:
        return 0
    if mode == 1:
        return value >> 1
    return value


def _interp_freq_channel(spchbuff, max_frames, start_frame, num_frames,
                         start_val, target_val, byte_offset):
    """Interpolate a frequency channel with monotonic constraint."""
    if num_frames <= 0:
        return
    step = (target_val - start_val) // num_frames
    val = start_val + step // 2
    for f in range(num_frames):
        pos = start_frame + f
        if 0 <= pos < max_frames:
            clamped = max(0, val)
            scaled = _scale_freq(clamped)
            fi = pos * FRAME_SIZE + byte_offset
            if spchbuff[fi] == 0:
                spchbuff[fi] = scaled
            elif step > 0 and scaled > spchbuff[fi]:
                spchbuff[fi] = scaled
            elif step < 0 and scaled < spchbuff[fi]:
                spchbuff[fi] = scaled
        val += step


def _interp_amp_channel(spchbuff, max_frames, start_frame, num_frames,
                        start_val, target_val, byte_offset):
    """Interpolate an amplitude channel with monotonic constraint."""
    if num_frames <= 0:
        return
    start_fp = start_val << 8
    target_fp = target_val << 8
    step_fp = (target_fp - start_fp) // num_frames
    val_fp = start_fp + step_fp // 2
    for f in range(num_frames):
        pos = start_frame + f
        if 0 <= pos < max_frames:
            raw = val_fp >> 8
            raw = max(0, min(255, raw))
            scaled = _scale_amp(raw)
            fi = pos * FRAME_SIZE + byte_offset
            if spchbuff[fi] == 0:
                spchbuff[fi] = scaled
            elif step_fp > 0 and scaled > spchbuff[fi]:
                spchbuff[fi] = scaled
            elif step_fp < 0 and scaled < spchbuff[fi]:
                spchbuff[fi] = scaled
        val_fp += step_fp


def _generate_speech_frames(slots, spchbuff, max_frames, monotone, syllable_pitch):
    """Stage 6: Generate speech parameter frames with dominance coarticulation."""
    frame_idx = 0
    vowel_count = 0

    # Count real phonemes (before sentinel)
    real_count = 0
    for s in slots:
        if s[0] == SLOT_END:
            break
        real_count += 1
    if real_count == 0:
        return 0

    blend_A = 0
    blend_B = 0
    prev_freq = [0, 0, 0]
    prev_amp = [0, 0, 0]
    prev_vc = 0

    for i in range(real_count):
        idx = slots[i][0]
        ph = idx // PH_ENTRY_SIZE
        if ph >= NUM_PHONEMES:
            continue

        entry = tabstart[ph]
        dur = slots[i][2]
        if dur <= 0:
            dur = entry[PH_DUR_MOD]
        if dur <= 0:
            dur = 1

        # Load phoneme's own values
        target_freq = [
            (entry[PH_F1_FREQ_H] << 8) | entry[PH_F1_FREQ_L],
            (entry[PH_F2_FREQ_H] << 8) | entry[PH_F2_FREQ_L],
            (entry[PH_F3_FREQ_H] << 8) | entry[PH_F3_FREQ_L],
        ]
        target_amp = [entry[PH_AMP1], entry[PH_AMP3], entry[PH_AMP5]]
        target_vc = entry[PH_VOICE_RAW]

        # Pitch period
        stress = slots[i][1] & 0x3F
        if not monotone and 0 < stress <= 9:
            p = 0x42 - stress * 3
            if p < 1:
                p = 1
            synth_cnt = p
        else:
            synth_cnt = 0x42

        # Syllable pitch
        if syllable_pitch and ph <= 21:
            vowel_count += 1
            if vowel_count % 3 == 0:
                p = synth_cnt - 4
                if p < 1:
                    p = 1
                synth_cnt = p

        # Clamp carried blend counts
        blend_in_A = min(blend_A, dur)
        blend_in_B = min(blend_B, dur)

        # BLEND-IN
        for ch in range(3):
            _interp_freq_channel(spchbuff, max_frames,
                                 frame_idx, blend_in_A,
                                 prev_freq[ch], target_freq[ch],
                                 FR_F1_FREQ + ch * 2)
        for ch in range(3):
            _interp_amp_channel(spchbuff, max_frames,
                                frame_idx, blend_in_B,
                                prev_amp[ch], target_amp[ch],
                                FR_F1_AMP + ch * 2)
        _interp_amp_channel(spchbuff, max_frames,
                            frame_idx, blend_in_B,
                            prev_vc, target_vc,
                            FR_VOICE_CTRL)

        cur_freq = list(target_freq)
        cur_amp = list(target_amp)
        cur_vc = target_vc

        # COARTICULATE with next phoneme
        coartic_freq = list(cur_freq)
        coartic_amp = list(cur_amp)
        coartic_vc = cur_vc
        blend_out_A = 0
        carry_A = 0
        blend_out_B = 0
        carry_B = 0

        if i + 1 < real_count:
            nph = slots[i + 1][0] // PH_ENTRY_SIZE
            if nph < NUM_PHONEMES:
                nentry = tabstart[nph]
                cur_st = entry[PH_SYNTH_TYPE]
                nxt_st = nentry[PH_SYNTH_TYPE]

                if cur_st >= nxt_st:
                    # Current is dominant
                    dom = entry
                    sub_f = [
                        (nentry[PH_F1_FREQ_H] << 8) | nentry[PH_F1_FREQ_L],
                        (nentry[PH_F2_FREQ_H] << 8) | nentry[PH_F2_FREQ_L],
                        (nentry[PH_F3_FREQ_H] << 8) | nentry[PH_F3_FREQ_L],
                    ]
                    sub_a = [nentry[PH_AMP1], nentry[PH_AMP3], nentry[PH_AMP5]]
                    sub_vc = nentry[PH_VOICE_RAW]
                    blend_out_A = entry[PH_COARTIC_B]
                    carry_A = entry[PH_COARTIC_A]
                    blend_out_B = entry[PH_PITCH2]
                    carry_B = entry[PH_PITCH1]
                else:
                    # Next is dominant
                    dom = nentry
                    sub_f = list(cur_freq)
                    sub_a = [entry[PH_AMP1], entry[PH_AMP3], entry[PH_AMP5]]
                    sub_vc = entry[PH_VOICE_RAW]
                    blend_out_A = nentry[PH_COARTIC_A]
                    carry_A = nentry[PH_COARTIC_B]
                    blend_out_B = nentry[PH_PITCH1]
                    carry_B = nentry[PH_PITCH2]

                # Frequency coartic targets
                dom_bw = [
                    (dom[PH_F1_BW_H] << 8) | dom[PH_F1_BW_L],
                    (dom[PH_F2_BW_H] << 8) | dom[PH_F2_BW_L],
                    (dom[PH_F3_BW_H] << 8) | dom[PH_F3_BW_L],
                ]
                coartic_freq[0] = _coartic_scale(sub_f[0], dom[PH_VOICING]) + dom_bw[0]
                coartic_freq[1] = _coartic_scale(sub_f[1], dom[PH_VOICING]) + dom_bw[1]
                coartic_freq[2] = _coartic_scale(sub_f[2], dom[PH_ASPIRATION1]) + dom_bw[2]

                # Amplitude coartic targets
                coartic_amp[0] = _coartic_scale(sub_a[0], dom[PH_BLEND_MODE]) + dom[PH_AMP2]
                coartic_amp[1] = _coartic_scale(sub_a[1], dom[PH_BLEND_MODE]) + dom[PH_AMP4]
                coartic_amp[2] = _coartic_scale(sub_a[2], dom[PH_BLEND_MODE]) + dom[PH_AMP6]
                coartic_vc = _coartic_scale(sub_vc, dom[PH_BLEND_MODE]) + dom[PH_RESERVED2]

                if blend_out_A > dur:
                    blend_out_A = dur
                if blend_out_B > dur:
                    blend_out_B = dur

        # STEADY freq frames
        sf_start = frame_idx + blend_in_A
        sf_count = dur - blend_in_A - blend_out_A
        if sf_count < 0:
            sf_count = 0
        for f in range(sf_count):
            pos = sf_start + f
            if pos >= max_frames:
                break
            fi = pos * FRAME_SIZE
            spchbuff[fi + FR_F1_FREQ] = _scale_freq(cur_freq[0])
            spchbuff[fi + FR_F2_FREQ] = _scale_freq(cur_freq[1])
            spchbuff[fi + FR_F3_FREQ] = _scale_freq(cur_freq[2])

        # STEADY amp frames + synth_cnt
        sa_start = frame_idx + blend_in_B
        sa_count = dur - blend_in_B - blend_out_B
        if sa_count < 0:
            sa_count = 0
        for f in range(sa_count):
            pos = sa_start + f
            if pos >= max_frames:
                break
            fi = pos * FRAME_SIZE
            spchbuff[fi + FR_F1_AMP] = _scale_amp(cur_amp[0])
            spchbuff[fi + FR_F2_AMP] = _scale_amp(cur_amp[1])
            spchbuff[fi + FR_F3_AMP] = _scale_amp(cur_amp[2])
            spchbuff[fi + FR_VOICE_CTRL] = _scale_amp(cur_vc)

        # Write synth_cnt to ALL frames
        for f in range(dur):
            pos = frame_idx + f
            if pos >= max_frames:
                break
            spchbuff[pos * FRAME_SIZE + FR_SYNTH_CNT] = synth_cnt

        # BLEND-OUT
        bo_freq_start = frame_idx + dur - blend_out_A
        for ch in range(3):
            _interp_freq_channel(spchbuff, max_frames,
                                 bo_freq_start, blend_out_A,
                                 cur_freq[ch], coartic_freq[ch],
                                 FR_F1_FREQ + ch * 2)
        bo_amp_start = frame_idx + dur - blend_out_B
        for ch in range(3):
            _interp_amp_channel(spchbuff, max_frames,
                                bo_amp_start, blend_out_B,
                                cur_amp[ch], coartic_amp[ch],
                                FR_F1_AMP + ch * 2)
        _interp_amp_channel(spchbuff, max_frames,
                            bo_amp_start, blend_out_B,
                            cur_vc, coartic_vc,
                            FR_VOICE_CTRL)

        # Carry for next phoneme
        blend_A = carry_A
        blend_B = carry_B
        prev_freq = list(coartic_freq)
        prev_amp = list(coartic_amp)
        prev_vc = coartic_vc

        frame_idx += dur

    # Noise period pass
    fi = 0
    for i in range(real_count):
        if fi >= frame_idx:
            break
        ph = slots[i][0] // PH_ENTRY_SIZE
        if ph >= NUM_PHONEMES:
            continue
        e = tabstart[ph]
        dur = slots[i][2]
        if dur <= 0:
            dur = e[PH_DUR_MOD]
        if dur <= 0:
            dur = 1
        noise = e[PH_VOICE_FLAG2]
        for f in range(dur):
            if fi >= frame_idx:
                break
            spchbuff[fi * FRAME_SIZE + FR_NOISE_PERIOD] = noise
            fi += 1

    return frame_idx


def _apply_pitch_contour(slots, spchbuff, num_frames):
    """Stage 7: Pitch contour — stress-based + punctuation bends + drift."""
    d0 = 0x42  # running pitch register
    frame_idx = 0

    for i in range(len(slots)):
        if slots[i][0] == SLOT_END:
            break
        ph = slots[i][0] // PH_ENTRY_SIZE
        if ph >= NUM_PHONEMES:
            continue

        entry = tabstart[ph]
        dur = slots[i][2]
        if dur <= 0:
            dur = entry[PH_DUR_MOD]
        if dur <= 0:
            dur = 1

        stress = slots[i][1] & 0x3F

        if ph > 62:
            # Punctuation: backward pitch bend
            if ph == 64:
                step, max_back = 1, 20    # Period: falling
            elif ph == 66:
                step, max_back = -1, 20   # Question: rising
            else:
                step, max_back = 1, 10    # Other: gentle fall

            bend = 0
            for f in range(frame_idx - 1, -1, -1):
                if frame_idx - 1 - f >= max_back:
                    break
                bend += step
                fi = f * FRAME_SIZE + FR_SYNTH_CNT
                val = spchbuff[fi] + bend
                val = max(1, min(255, val))
                spchbuff[fi] = val

            for f in range(dur):
                pos = frame_idx + f
                if pos >= num_frames:
                    break
                spchbuff[pos * FRAME_SIZE + FR_SYNTH_CNT] = 0x42

            d0 = 0x42

        elif 0 < stress <= 9:
            # Stressed: flat at 0x42 - stress*3
            p = 0x42 - stress * 3
            if p < 1:
                p = 1
            for f in range(dur):
                pos = frame_idx + f
                if pos >= num_frames:
                    break
                spchbuff[pos * FRAME_SIZE + FR_SYNTH_CNT] = p
            d0 = p

        else:
            # Unstressed/silence: write d0, drift toward 0x42
            for f in range(dur):
                pos = frame_idx + f
                if pos >= num_frames:
                    break
                spchbuff[pos * FRAME_SIZE + FR_SYNTH_CNT] = d0
                if d0 < 0x42:
                    d0 += 1
                elif d0 > 0x42:
                    d0 -= 1

        frame_idx += dur


def synth_generate_frames(phonemes, rate, pitch, monotone, syllable_pitch):
    """Parse phoneme string and generate frame buffer.

    Args:
        phonemes: Phoneme string (e.g. "EH2LOW")
        rate: Speech rate
        pitch: Speech pitch
        monotone: If true, disable pitch variation
        syllable_pitch: If true, enable syllable pitch variation

    Returns:
        bytes of frame data (num_frames * FRAME_SIZE), or None on error.
    """
    slots = _parse_phonemes(phonemes)
    if slots is None:
        return None

    _expand_phonemes(slots)
    _propagate_stress(slots)
    _process_flags(slots)
    _calc_durations(slots)
    _boost_durations(slots)

    max_frames = MAX_FRAMES
    spchbuff = bytearray(max_frames * FRAME_SIZE)

    num_frames = _generate_speech_frames(slots, spchbuff, max_frames,
                                         monotone, syllable_pitch)
    if num_frames <= 0:
        return None

    if not monotone:
        _apply_pitch_contour(slots, spchbuff, num_frames)

    return bytes(spchbuff[:num_frames * FRAME_SIZE])
