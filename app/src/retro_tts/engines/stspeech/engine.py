# STSPEECH engine - pure Python implementation
# Ported from engine/src/stspeech.c

import struct

from .synth import synth_generate_frames, FRAME_SIZE
from .audio import audio_render_pcm
from .reciter import reciter_text_to_phonemes
from .cmudict import cmudict_lookup
from .num2words import num2words

VERSION = "STSPEECH 2.0 (Python)"

# Known vowel phoneme names (indices 0-21, excluding glides WX/YX)
_VOWELS = {
    "EY", "AY", "OY", "AW", "OW", "UW", "AR", "AE", "IY", "ER",
    "AO", "UX", "UH", "AH", "AA", "OH", "AX", "IX", "IH", "EH",
}


def _make_wav(pcm_data, sample_rate=22050):
    """Convert raw 16-bit signed PCM bytes to WAV file bytes."""
    num_channels = 1
    bits_per_sample = 16
    byte_rate = sample_rate * num_channels * bits_per_sample // 8
    block_align = num_channels * bits_per_sample // 8
    data_size = len(pcm_data)

    header = b'RIFF'
    header += struct.pack('<I', data_size + 36)
    header += b'WAVE'
    header += b'fmt '
    header += struct.pack('<I', 16)
    header += struct.pack('<H', 1)  # PCM
    header += struct.pack('<H', num_channels)
    header += struct.pack('<I', sample_rate)
    header += struct.pack('<I', byte_rate)
    header += struct.pack('<H', block_align)
    header += struct.pack('<H', bits_per_sample)
    header += b'data'
    header += struct.pack('<I', data_size)

    return header + pcm_data


def _is_vowel_name(c1, c2):
    """Check if two characters form a vowel phoneme name."""
    return (c1 + c2) in _VOWELS


def _add_default_stress(phonemes):
    """Insert stress marker '1' after the first vowel phoneme."""
    result = []
    stressed = False
    i = 0

    while i < len(phonemes):
        c1 = phonemes[i].upper()
        c2 = phonemes[i + 1].upper() if i + 1 < len(phonemes) else ' '

        result.append(phonemes[i])
        i += 1

        if not stressed and i < len(phonemes) and _is_vowel_name(c1, c2):
            result.append(phonemes[i])  # copy second char of vowel
            i += 1
            result.append('1')  # insert stress
            stressed = True

    return ''.join(result)


def _word_to_phonemes(word):
    """Convert a single word to phonemes using cmudict then reciter fallback."""
    phonemes = cmudict_lookup(word)
    if phonemes:
        return phonemes
    recited = reciter_text_to_phonemes(word)
    if recited:
        return _add_default_stress(recited)
    return None


def _text_as_phonemes(text):
    """Convert space-separated words to phonemes."""
    result = []
    for word in text.split():
        if not word:
            continue
        ph = _word_to_phonemes(word)
        if ph:
            result.append(ph)
    return ''.join(result)


def text_to_phonemes(text):
    """Convert English text to phoneme string.

    Args:
        text: English text string.

    Returns:
        Phoneme string, or None on failure.
    """
    if not text:
        return None

    result = []
    i = 0
    punct_chars = set('.,?-!;:')

    while i < len(text):
        ch = text[i]

        # Skip whitespace
        if ch.isspace():
            i += 1
            continue

        # Punctuation → pause phonemes
        if ch in punct_chars:
            if ch in ('!', '.'):
                result.append('.')
            elif ch == '?':
                result.append('?')
            elif ch in (';', ':', ','):
                result.append(',')
            else:
                result.append('-')
            i += 1
            continue

        # Extract a word
        word_start = i
        while i < len(text) and not text[i].isspace() and text[i] not in punct_chars:
            i += 1
        word = text[word_start:i]

        if not word:
            i += 1
            continue

        # Numbers → English words → phonemes
        if any(c.isdigit() for c in word):
            expanded = num2words(word)
            if expanded:
                ph = _text_as_phonemes(expanded)
                if ph:
                    result.append(ph)
                continue

        # Normal word lookup
        ph = _word_to_phonemes(word)
        if ph:
            result.append(ph)

    phoneme_str = ''.join(result)
    return phoneme_str if phoneme_str else None


def text_to_wav(text, rate=0x4F, pitch=0x4D):
    """Convert English text to WAV file bytes.

    Args:
        text: English text to synthesize.
        rate: Speech rate (20-200, default 79/0x4F).
        pitch: Speech pitch (20-200, default 77/0x4D).

    Returns:
        WAV file data as bytes, or None on failure.
    """
    engine = STSpeech(rate=rate, pitch=pitch)
    return engine.wav(text)


def _parse_segments(text, default_rate, default_pitch):
    """Parse inline %<n> (rate) and !<n> (pitch) commands.

    Returns list of (text, rate, pitch) tuples.
    """
    segments = []
    rate = default_rate
    pitch = default_pitch
    seg_start = 0
    i = 0

    while i < len(text):
        if text[i] in ('%', '!') and i + 1 < len(text) and text[i + 1].isdigit():
            # End current segment
            seg_text = text[seg_start:i]
            if seg_text:
                segments.append((seg_text, rate, pitch))

            cmd = text[i]
            i += 1
            digits = ''
            while i < len(text) and text[i].isdigit() and len(digits) < 3:
                digits += text[i]
                i += 1
            val = int(digits)
            val = max(20, min(200, val))
            if cmd == '%':
                rate = val
            else:
                pitch = val
            seg_start = i
        else:
            i += 1

    # Final segment
    seg_text = text[seg_start:]
    if seg_text:
        segments.append((seg_text, rate, pitch))

    return segments


class STSpeech:
    """STSPEECH text-to-speech engine.

    Pure Python implementation. Produces 16-bit signed PCM at 22050 Hz mono.
    """

    def __init__(self, rate=0x4F, pitch=0x4D, subframe_factor=1):
        """Initialize engine with voice parameters.

        Args:
            rate: Speech rate (20-200, default 79).
            pitch: Speech pitch (20-200, default 77).
            subframe_factor: Frame upsampling factor (1=original, 2=2x, 4=4x).
        """
        self._rate = max(20, min(200, rate))
        self._pitch = max(20, min(200, pitch))
        self._subframe_factor = max(1, int(subframe_factor))
        self._monotone = False
        self._syllable_pitch = False

    @property
    def rate(self):
        return self._rate

    @rate.setter
    def rate(self, value):
        self._rate = max(20, min(200, int(value)))

    @property
    def pitch(self):
        return self._pitch

    @pitch.setter
    def pitch(self, value):
        self._pitch = max(20, min(200, int(value)))

    @property
    def monotone(self):
        return self._monotone

    @monotone.setter
    def monotone(self, value):
        self._monotone = bool(value)

    @property
    def subframe_factor(self):
        return self._subframe_factor

    @subframe_factor.setter
    def subframe_factor(self, value):
        self._subframe_factor = max(1, int(value))

    @property
    def syllable_pitch(self):
        return self._syllable_pitch

    @syllable_pitch.setter
    def syllable_pitch(self, value):
        self._syllable_pitch = bool(value)

    def text_to_phonemes(self, text):
        """Convert English text to phoneme string.

        Args:
            text: English text.

        Returns:
            Phoneme string, or None on failure.
        """
        return text_to_phonemes(text)

    def _render_segment(self, phonemes, seg_rate, seg_pitch):
        """Render a phoneme string to PCM bytes.

        Returns bytes (int16 LE), or None.
        """
        frame_data = synth_generate_frames(
            phonemes, seg_rate, seg_pitch,
            self._monotone, self._syllable_pitch)
        if frame_data is None:
            return None

        num_frames = len(frame_data) // FRAME_SIZE
        pcm = audio_render_pcm(frame_data, num_frames, seg_rate, seg_pitch,
                               self._subframe_factor)
        return pcm

    def speak(self, text):
        """Synthesize English text to PCM audio.

        Args:
            text: English text to synthesize.

        Returns:
            PCM audio as bytes (16-bit signed, 22050 Hz, mono),
            or None on failure.
        """
        segments = _parse_segments(text, self._rate, self._pitch)
        if not segments:
            return None

        all_pcm = []
        for seg_text, seg_rate, seg_pitch in segments:
            phoneme_str = text_to_phonemes(seg_text)
            if not phoneme_str:
                continue
            pcm = self._render_segment(phoneme_str, seg_rate, seg_pitch)
            if pcm is not None and len(pcm) > 0:
                all_pcm.append(pcm)

        if not all_pcm:
            return None

        return b''.join(all_pcm)

    def speak_phonemes(self, phonemes):
        """Synthesize phoneme string to PCM audio.

        Args:
            phonemes: Phoneme string (e.g. "EH2LOW").

        Returns:
            PCM audio as bytes (16-bit signed, 22050 Hz, mono),
            or None on failure.
        """
        segments = _parse_segments(phonemes, self._rate, self._pitch)
        if not segments:
            return None

        all_pcm = []
        for seg_text, seg_rate, seg_pitch in segments:
            pcm = self._render_segment(seg_text, seg_rate, seg_pitch)
            if pcm is not None and len(pcm) > 0:
                all_pcm.append(pcm)

        if not all_pcm:
            return None

        return b''.join(all_pcm)

    def wav(self, text):
        """Synthesize English text to WAV file bytes.

        Args:
            text: English text to synthesize.

        Returns:
            WAV file data as bytes, or None on failure.
        """
        pcm = self.speak(text)
        if pcm is None:
            return None
        return _make_wav(pcm)

    def wav_phonemes(self, phonemes):
        """Synthesize phoneme string to WAV file bytes.

        Args:
            phonemes: Phoneme string.

        Returns:
            WAV file data as bytes, or None on failure.
        """
        pcm = self.speak_phonemes(phonemes)
        if pcm is None:
            return None
        return _make_wav(pcm)

    def save_wav(self, text, filename):
        """Synthesize text and save as WAV file.

        Args:
            text: English text to synthesize.
            filename: Output file path.

        Returns:
            True on success, False on failure.
        """
        wav_data = self.wav(text)
        if wav_data is None:
            return False
        with open(filename, 'wb') as f:
            f.write(wav_data)
        return True

    @staticmethod
    def version():
        """Return engine version string."""
        return VERSION
