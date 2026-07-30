import math
try:
    from .renderer_tables import (
        FREQUENCY_DATA, AMPLITUDE_DATA, SAMPLED_CONSONANT_FLAGS,
        STRESS_PITCH_TABLE, BLEND_RANK, OUT_BLEND_LENGTH, IN_BLEND_LENGTH,
        SAMPLED_CONSONANT_VALUES0, SAMPLE_TABLE, AMPLITUDE_RESCALE, sinus
    )
except ImportError:
    from renderer_tables import (
        FREQUENCY_DATA, AMPLITUDE_DATA, SAMPLED_CONSONANT_FLAGS,
        STRESS_PITCH_TABLE, BLEND_RANK, OUT_BLEND_LENGTH, IN_BLEND_LENGTH,
        SAMPLED_CONSONANT_VALUES0, SAMPLE_TABLE, AMPLITUDE_RESCALE, sinus
    )
PHONEME_PERIOD = 1
PHONEME_QUESTION = 2
RISING_INFLECTION = 255
FALLING_INFLECTION = 1
class OutputBuffer:
    """Manages the audio output buffer."""
    TIMETABLE = [
        [162, 167, 167, 127, 128],   # formants synth
        [226, 60, 60, 0, 0],         # unvoiced sample 0
        [225, 60, 59, 0, 0],         # unvoiced sample 1
        [200, 0, 0, 54, 55],         # voiced sample 0
        [199, 0, 0, 54, 54]          # voiced sample 1
    ]
    def __init__(self, buffersize):
        self.buffer = bytearray(buffersize)
        self.bufferpos = 0
        self.old_timetable_index = 0
    def write(self, index, value):
        """Scale by 16 and write five times."""
        scaled = (value & 15) * 16
        self.write_array(index, [scaled, scaled, scaled, scaled, scaled])
    def write_array(self, index, array):
        """Write the five given values."""
        self.bufferpos += self.TIMETABLE[self.old_timetable_index][index]
        pos = self.bufferpos // 50
        if pos > len(self.buffer):
            raise RuntimeError("Buffer overflow")
        self.old_timetable_index = index
        for k in range(5):
            if pos + k < len(self.buffer):
                self.buffer[pos + k] = array[k]
    def get(self):
        """Return the filled portion of the buffer."""
        return bytes(self.buffer[:self.bufferpos // 50])
def set_mouth_throat(mouth, throat):
    """
    SAM's voice can be altered by changing the frequencies of the
    mouth formant (F1) and the throat formant (F2). Only the
    vowel/diphthong and sonorant phonemes (5-29 and 48-53) are altered.
    Args:
        mouth: valid values 0-255
        throat: valid values 0-255
    Returns:
        Three frequency arrays [F1, F2, F3]
    """
    def trans(factor, initial_frequency):
        return (((factor * initial_frequency) >> 8) & 0xFF) << 1
    freqdata = [[], [], []]
    for i, v in enumerate(FREQUENCY_DATA):
        freqdata[0].append(v & 0xFF)
        freqdata[1].append((v >> 8) & 0xFF)
        freqdata[2].append((v >> 16) & 0xFF)
    for pos in range(5, 30):
        freqdata[0][pos] = trans(mouth, freqdata[0][pos])
        freqdata[1][pos] = trans(throat, freqdata[1][pos])
    for pos in range(48, 54):
        freqdata[0][pos] = trans(mouth, freqdata[0][pos])
        freqdata[1][pos] = trans(throat, freqdata[1][pos])
    return freqdata
def create_frames(pitch, tuples, frequency_data, inflection_amount=50):
    """
    CREATE FRAMES
    The length parameter in the list corresponds to the number of frames
    to expand the phoneme to. At the default speed, each frame represents
    about 10 milliseconds of time.
    Args:
        pitch: Base pitch value
        tuples: List of [phoneme, length, stress] tuples
        frequency_data: Three frequency arrays from set_mouth_throat
        inflection_amount: Inflection level 0-100 (0=monotone, 50=normal, 100=dramatic)
    Returns:
        [pitches, frequency, amplitude, sampled_consonant_flag]
    """
    scale = inflection_amount / 50.0
    rising_inflection = int(RISING_INFLECTION * scale) if scale > 0 else 0
    falling_inflection = int(FALLING_INFLECTION * scale) if scale > 0 else 0
    rising_inflection = max(0, min(255, rising_inflection))
    falling_inflection = max(0, min(255, falling_inflection))
    def add_inflection(inflection, pos, pitches):
        """Create a rising or falling inflection 30 frames prior to index."""
        end = pos
        if pos < 30:
            pos = 0
        else:
            pos -= 30
        while pos < len(pitches) and pitches[pos] == 127:
            pos += 1
        while pos != end and pos < len(pitches):
            a = pitches[pos]
            a += inflection
            pitches[pos] = a & 0xFF
            pos += 1
            while pos != end and pos < len(pitches) and pitches[pos] == 255:
                pos += 1
    pitches = []
    frequency = [[], [], []]
    amplitude = [[], [], []]
    sampled_consonant_flag = []
    x = 0
    for i in range(len(tuples)):
        phoneme = tuples[i][0]
        if phoneme == PHONEME_PERIOD:
            add_inflection(falling_inflection, x, pitches)
        elif phoneme == PHONEME_QUESTION:
            add_inflection(rising_inflection, x, pitches)
        phase1 = STRESS_PITCH_TABLE[tuples[i][2]] if tuples[i][2] < len(STRESS_PITCH_TABLE) else 0
        phase1 = int(phase1 * scale)
        frames = tuples[i][1]
        for _ in range(frames):
            frequency[0].append(frequency_data[0][phoneme])  # F1 frequency
            frequency[1].append(frequency_data[1][phoneme])  # F2 frequency
            frequency[2].append(frequency_data[2][phoneme])  # F3 frequency
            amp = AMPLITUDE_DATA[phoneme] if phoneme < len(AMPLITUDE_DATA) else 0
            amplitude[0].append(amp & 0xFF)          # F1 amplitude
            amplitude[1].append((amp >> 8) & 0xFF)   # F2 amplitude
            amplitude[2].append((amp >> 16) & 0xFF)  # F3 amplitude
            scf = SAMPLED_CONSONANT_FLAGS[phoneme] if phoneme < len(SAMPLED_CONSONANT_FLAGS) else 0
            sampled_consonant_flag.append(scf)
            pitches.append((pitch + phase1) & 0xFF)
            x += 1
    return [pitches, frequency, amplitude, sampled_consonant_flag]
def create_transitions(pitches, frequency, amplitude, tuples):
    """
    CREATE TRANSITIONS
    Linear transitions are now created to smoothly connect each
    phoneme. This transition is spread between the ending frames
    of the old phoneme (outBlendLength), and the beginning frames
    of the new phoneme (inBlendLength).
    Args:
        pitches: Pitch array
        frequency: Three frequency arrays
        amplitude: Three amplitude arrays
        tuples: List of [phoneme, length, stress] tuples
    Returns:
        Total frame count
    """
    tables = [pitches, frequency[0], frequency[1], frequency[2],
              amplitude[0], amplitude[1], amplitude[2]]
    def read(table, pos):
        if pos < 0 or pos >= len(tables[table]):
            return 0
        return tables[table][pos]
    def interpolate(width, table, frame, change):
        """Linearly interpolate values."""
        if width == 0:
            return
        sign = change < 0
        remainder = abs(change) % width
        div = int(change / width)
        error = 0
        pos = width
        while pos > 1:
            pos -= 1
            val = read(table, frame) + div
            error += remainder
            if error >= width:
                error -= width
                if sign:
                    val -= 1
                elif val:
                    val += 1
            frame += 1
            if frame < len(tables[table]):
                tables[table][frame] = val
    boundary = 0
    for pos in range(len(tuples) - 1):
        phoneme = tuples[pos][0]
        next_phoneme = tuples[pos + 1][0]
        next_rank = BLEND_RANK[next_phoneme] if next_phoneme < len(BLEND_RANK) else 0
        rank = BLEND_RANK[phoneme] if phoneme < len(BLEND_RANK) else 0
        if rank == next_rank:
            out_blend_frames = OUT_BLEND_LENGTH[phoneme] if phoneme < len(OUT_BLEND_LENGTH) else 0
            in_blend_frames = OUT_BLEND_LENGTH[next_phoneme] if next_phoneme < len(OUT_BLEND_LENGTH) else 0
        elif rank < next_rank:
            out_blend_frames = IN_BLEND_LENGTH[next_phoneme] if next_phoneme < len(IN_BLEND_LENGTH) else 0
            in_blend_frames = OUT_BLEND_LENGTH[next_phoneme] if next_phoneme < len(OUT_BLEND_LENGTH) else 0
        else:
            out_blend_frames = OUT_BLEND_LENGTH[phoneme] if phoneme < len(OUT_BLEND_LENGTH) else 0
            in_blend_frames = IN_BLEND_LENGTH[phoneme] if phoneme < len(IN_BLEND_LENGTH) else 0
        boundary += tuples[pos][1]
        trans_end = boundary + in_blend_frames
        trans_start = boundary - out_blend_frames
        trans_length = out_blend_frames + in_blend_frames
        if ((trans_length - 2) & 128) == 0:
            cur_width = tuples[pos][1] >> 1
            next_width = tuples[pos + 1][1] >> 1
            pitch_end = boundary + next_width
            pitch_start = boundary - cur_width
            if pitch_end < len(pitches) and pitch_start >= 0:
                pitch_diff = pitches[pitch_end] - pitches[pitch_start]
                interpolate(cur_width + next_width, 0, trans_start, pitch_diff)
            for table in range(1, 7):
                value = read(table, trans_end) - read(table, trans_start)
                interpolate(trans_length, table, trans_start, value)
    if tuples:
        return boundary + tuples[-1][1]
    return boundary
def prepare_frames(phonemes, pitch, mouth, throat, singmode, inflection=50):
    """
    Prepare frames for rendering.
    Args:
        phonemes: List of [phoneme, length, stress] tuples
        pitch: Base pitch (0-255)
        mouth: Mouth parameter (0-255)
        throat: Throat parameter (0-255)
        singmode: Boolean for sing mode
        inflection: Inflection level 0-100 (0=monotone, 50=normal, 100=dramatic)
    Returns:
        [frame_count, frequency, pitches, amplitude, sampled_consonant_flag]
    """
    freqdata = set_mouth_throat(mouth, throat)
    pitches, frequency, amplitude, sampled_consonant_flag = create_frames(
        pitch, phonemes, freqdata, inflection
    )
    t = create_transitions(pitches, frequency, amplitude, phonemes)
    if not singmode:
        scale = inflection / 50.0
        for i in range(len(pitches)):
            if i < len(frequency[0]):
                f1_adjust = int((frequency[0][i] >> 1) * scale)
                pitches[i] = (pitches[i] - f1_adjust) & 0xFF
    for i in range(len(amplitude[0]) - 1, -1, -1):
        if amplitude[0][i] < len(AMPLITUDE_RESCALE):
            amplitude[0][i] = AMPLITUDE_RESCALE[amplitude[0][i]]
        if amplitude[1][i] < len(AMPLITUDE_RESCALE):
            amplitude[1][i] = AMPLITUDE_RESCALE[amplitude[1][i]]
        if amplitude[2][i] < len(AMPLITUDE_RESCALE):
            amplitude[2][i] = AMPLITUDE_RESCALE[amplitude[2][i]]
    return [t, frequency, pitches, amplitude, sampled_consonant_flag]
def render_sample(output, last_sample_offset, consonant_flag, pitch):
    """
    Render a sampled consonant.
    Args:
        output: OutputBuffer instance
        last_sample_offset: Previous sample offset
        consonant_flag: Consonant type flag
        pitch: Current pitch value
    Returns:
        Updated sample offset
    """
    kind = (consonant_flag & 7) - 1
    sample_page = (kind * 256) & 0xFFFF
    off = consonant_flag & 248
    def render_sample_inner(index1, value1, index0, value0):
        nonlocal off
        sample_idx = sample_page + off
        if sample_idx >= len(SAMPLE_TABLE):
            return
        sample = SAMPLE_TABLE[sample_idx]
        bit = 8
        while bit > 0:
            if (sample & 128) != 0:
                output.write(index1, value1)
            else:
                output.write(index0, value0)
            sample = (sample << 1) & 0xFF
            bit -= 1
    if off == 0:
        phase1 = ((pitch >> 4) ^ 255) & 0xFF
        off = last_sample_offset & 0xFF
        while True:
            render_sample_inner(3, 26, 4, 6)
            off = (off + 1) & 0xFF
            phase1 = (phase1 + 1) & 0xFF
            if phase1 == 0:
                break
        return off
    off = (off ^ 255) & 0xFF
    value0 = SAMPLED_CONSONANT_VALUES0[kind] & 0xFF if kind < len(SAMPLED_CONSONANT_VALUES0) else 0
    while True:
        render_sample_inner(2, 5, 1, value0)
        off = (off + 1) & 0xFF
        if off == 0:
            break
    return last_sample_offset
def process_frames(output, frame_count, speed, frequency, pitches, amplitude, sampled_consonant_flag):
    """
    PROCESS THE FRAMES
    In traditional vocal synthesis, the glottal pulse drives filters, which
    are attenuated to the frequencies of the formants.
    SAM generates these formants directly with sine and rectangular waves.
    To simulate them being driven by the glottal pulse, the waveforms are
    reset at the beginning of each glottal pulse.
    Args:
        output: OutputBuffer instance
        frame_count: Number of frames to process
        speed: Speed parameter
        frequency: Three frequency arrays
        pitches: Pitch array
        amplitude: Three amplitude arrays
        sampled_consonant_flag: Sampled consonant flags
    """
    speedcounter = speed
    phase1 = 0
    phase2 = 0
    phase3 = 0
    last_sample_offset = 0
    pos = 0
    if not pitches:
        return
    glottal_pulse = pitches[0]
    mem38 = int(glottal_pulse * 0.75)
    while frame_count > 0:
        if pos >= len(sampled_consonant_flag):
            break
        flags = sampled_consonant_flag[pos]
        if (flags & 248) != 0:
            pitch_val = pitches[pos & 0xFF] if (pos & 0xFF) < len(pitches) else 0
            last_sample_offset = render_sample(output, last_sample_offset, flags, pitch_val)
            pos += 2
            frame_count -= 2
            speedcounter = speed
        else:
            ary = []
            p1 = phase1 * 256  # Fixed point
            p2 = phase2 * 256
            p3 = phase3 * 256
            for k in range(5):
                sp1 = sinus((p1 >> 8) & 0xFF)
                sp2 = sinus((p2 >> 8) & 0xFF)
                rp3 = -0x70 if ((p3 >> 8) & 0xFF) < 129 else 0x70
                amp0 = amplitude[0][pos] & 0x0F if pos < len(amplitude[0]) else 0
                amp1 = amplitude[1][pos] & 0x0F if pos < len(amplitude[1]) else 0
                amp2 = amplitude[2][pos] & 0x0F if pos < len(amplitude[2]) else 0
                sin1 = sp1 * amp0
                sin2 = sp2 * amp1
                rect = rp3 * amp2
                mux = sin1 + sin2 + rect
                mux = mux / 32
                mux = int(mux) + 128  # Go from signed to unsigned
                ary.append(max(0, min(255, mux)))
                freq0 = frequency[0][pos] if pos < len(frequency[0]) else 0
                freq1 = frequency[1][pos] if pos < len(frequency[1]) else 0
                freq2 = frequency[2][pos] if pos < len(frequency[2]) else 0
                p1 += int(freq0 * 256 / 4)
                p2 += int(freq1 * 256 / 4)
                p3 += int(freq2 * 256 / 4)
            output.write_array(0, ary)
            speedcounter -= 1
            if speedcounter == 0:
                pos += 1
                frame_count -= 1
                if frame_count == 0:
                    return
                speedcounter = speed
            glottal_pulse -= 1
            if glottal_pulse != 0:
                mem38 -= 1
                if mem38 != 0 or flags == 0:
                    freq0 = frequency[0][pos] if pos < len(frequency[0]) else 0
                    freq1 = frequency[1][pos] if pos < len(frequency[1]) else 0
                    freq2 = frequency[2][pos] if pos < len(frequency[2]) else 0
                    phase1 = phase1 + freq0
                    phase2 = phase2 + freq1
                    phase3 = phase3 + freq2
                    continue
                pitch_val = pitches[pos & 0xFF] if (pos & 0xFF) < len(pitches) else 0
                last_sample_offset = render_sample(output, last_sample_offset, flags, pitch_val)
            if pos < len(pitches):
                glottal_pulse = pitches[pos]
            else:
                glottal_pulse = 0
            mem38 = int(glottal_pulse * 0.75)
            phase1 = 0
            phase2 = 0
            phase3 = 0
def render(phonemes, pitch=64, mouth=128, throat=128, speed=72, singmode=False, inflection=50):
    """
    Main renderer function.
    Args:
        phonemes: List of [phoneme, length, stress] tuples from parser
        pitch: Pitch parameter (0-255, default 64)
        mouth: Mouth parameter (0-255, default 128)
        throat: Throat parameter (0-255, default 128)
        speed: Speed parameter (0-255, default 72)
        singmode: Enable sing mode (default False)
        inflection: Inflection level 0-100 (0=monotone, 50=normal, 100=dramatic)
    Returns:
        Audio data as bytes (8-bit unsigned PCM, 22050 Hz mono)
    """
    pitch = pitch & 0xFF
    mouth = mouth & 0xFF
    throat = throat & 0xFF
    speed = (speed or 72) & 0xFF
    t, frequency, pitches, amplitude, sampled_consonant_flag = prepare_frames(
        phonemes, pitch, mouth, throat, singmode, inflection
    )
    total_frames = sum(p[1] for p in phonemes)
    buffersize = int(176.4 * total_frames * speed)
    if buffersize == 0:
        return bytes()
    output = OutputBuffer(buffersize)
    process_frames(output, t, speed, frequency, pitches, amplitude, sampled_consonant_flag)
    return output.get()
