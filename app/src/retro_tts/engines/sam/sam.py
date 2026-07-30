import struct
try:
    from .reciter import text_to_phonemes
    from .parser import parse
    from .renderer import render
except ImportError:
    from reciter import text_to_phonemes
    from parser import parse
    from renderer import render
VOICE_PRESETS = {
    'sam': {'pitch': 64, 'speed': 72, 'mouth': 128, 'throat': 128},
    'elf': {'pitch': 64, 'speed': 72, 'mouth': 110, 'throat': 160},
    'little_robot': {'pitch': 60, 'speed': 92, 'mouth': 190, 'throat': 190},
    'stuffy_guy': {'pitch': 72, 'speed': 82, 'mouth': 110, 'throat': 105},
    'little_old_lady': {'pitch': 32, 'speed': 72, 'mouth': 145, 'throat': 145},
    'extra_terrestrial': {'pitch': 64, 'speed': 100, 'mouth': 150, 'throat': 200},
}
def text_to_audio(text, pitch=64, speed=72, mouth=128, throat=128, singmode=False, phonetic=False, inflection=50):
    """
    Convert text to audio data.
    Args:
        text: Input text to synthesize (or phoneme string if phonetic=True)
        pitch: Pitch parameter (0-255, default 64)
        speed: Speed parameter (0-255, default 72)
        mouth: Mouth parameter (0-255, default 128)
        throat: Throat parameter (0-255, default 128)
        singmode: Enable sing mode (default False)
        phonetic: If True, text is already phoneme data (default False)
        inflection: Inflection level 0-100 (0=monotone, 50=normal, 100=dramatic)
    Returns:
        Audio data as bytes (8-bit unsigned PCM, 22050 Hz mono), or None on failure
    """
    if phonetic:
        phoneme_string = text.upper()
    else:
        phoneme_string = text_to_phonemes(text)
        if phoneme_string is False:
            return None
    phoneme_list = parse(phoneme_string)
    if phoneme_list is False or not phoneme_list:
        return None
    audio_data = render(phoneme_list, pitch, mouth, throat, speed, singmode, inflection)
    return audio_data
def text_to_wav(text, pitch=64, speed=72, mouth=128, throat=128, singmode=False, phonetic=False, inflection=50):
    """
    Convert text to WAV file data.
    Args:
        text: Input text to synthesize (or phoneme string if phonetic=True)
        pitch: Pitch parameter (0-255, default 64)
        speed: Speed parameter (0-255, default 72)
        mouth: Mouth parameter (0-255, default 128)
        throat: Throat parameter (0-255, default 128)
        singmode: Enable sing mode (default False)
        phonetic: If True, text is already phoneme data (default False)
        inflection: Inflection level 0-100 (0=monotone, 50=normal, 100=dramatic)
    Returns:
        WAV file data as bytes, or None on failure
    """
    audio_data = text_to_audio(text, pitch, speed, mouth, throat, singmode, phonetic, inflection)
    if audio_data is None:
        return None
    return audio_to_wav(audio_data)
def audio_to_wav(audio_data):
    """
    Convert raw audio data to WAV format.
    Args:
        audio_data: Raw 8-bit unsigned PCM audio data
    Returns:
        WAV file data as bytes
    """
    if not audio_data:
        return None
    sample_rate = 22050
    num_channels = 1
    bits_per_sample = 8
    byte_rate = sample_rate * num_channels * bits_per_sample // 8
    block_align = num_channels * bits_per_sample // 8
    data_size = len(audio_data)
    wav_header = b'RIFF'
    wav_header += struct.pack('<I', data_size + 36)  # ChunkSize
    wav_header += b'WAVE'
    wav_header += b'fmt '
    wav_header += struct.pack('<I', 16)  # Subchunk1Size (16 for PCM)
    wav_header += struct.pack('<H', 1)   # AudioFormat (1 = PCM)
    wav_header += struct.pack('<H', num_channels)
    wav_header += struct.pack('<I', sample_rate)
    wav_header += struct.pack('<I', byte_rate)
    wav_header += struct.pack('<H', block_align)
    wav_header += struct.pack('<H', bits_per_sample)
    wav_header += b'data'
    wav_header += struct.pack('<I', data_size)
    return wav_header + bytes(audio_data)
class SAM:
    """
    SAM (Software Automatic Mouth) Text-to-Speech Synthesizer.
    A Python port of the 1982 Software Automatic Mouth synthesizer.
    """
    def __init__(self, pitch=64, speed=72, mouth=128, throat=128, singmode=False, inflection=50):
        """
        Initialize SAM with voice parameters.
        Args:
            pitch: Pitch parameter (0-255, default 64)
            speed: Speed parameter (0-255, default 72)
            mouth: Mouth parameter (0-255, default 128)
            throat: Throat parameter (0-255, default 128)
            singmode: Enable sing mode (default False)
            inflection: Inflection level 0-100 (0=monotone, 50=normal, 100=dramatic)
        """
        self.pitch = pitch
        self.speed = speed
        self.mouth = mouth
        self.throat = throat
        self.singmode = singmode
        self.inflection = inflection
    @classmethod
    def with_preset(cls, preset_name):
        """
        Create a SAM instance with a preset voice.
        Args:
            preset_name: One of 'sam', 'elf', 'little_robot', 'stuffy_guy',
                        'little_old_lady', 'extra_terrestrial'
        Returns:
            SAM instance configured with the preset
        """
        preset = VOICE_PRESETS.get(preset_name.lower(), VOICE_PRESETS['sam'])
        return cls(**preset)
    def set_voice(self, pitch=None, speed=None, mouth=None, throat=None):
        """Update voice parameters."""
        if pitch is not None:
            self.pitch = pitch
        if speed is not None:
            self.speed = speed
        if mouth is not None:
            self.mouth = mouth
        if throat is not None:
            self.throat = throat
    def set_preset(self, preset_name):
        """Set voice to a preset."""
        preset = VOICE_PRESETS.get(preset_name.lower(), VOICE_PRESETS['sam'])
        self.pitch = preset['pitch']
        self.speed = preset['speed']
        self.mouth = preset['mouth']
        self.throat = preset['throat']
    def convert(self, text):
        """
        Convert text to phoneme string.
        Args:
            text: Input text
        Returns:
            Phoneme string or False on failure
        """
        return text_to_phonemes(text)
    def speak_phonemes(self, phonemes):
        """
        Convert phoneme string to audio.
        Args:
            phonemes: Phoneme string
        Returns:
            Audio data as bytes (8-bit unsigned PCM, 22050 Hz mono)
        """
        return text_to_audio(
            phonemes,
            pitch=self.pitch,
            speed=self.speed,
            mouth=self.mouth,
            throat=self.throat,
            singmode=self.singmode,
            phonetic=True,
            inflection=self.inflection
        )
    def speak(self, text, phonetic=False):
        """
        Convert text to audio.
        Args:
            text: Input text (or phoneme string if phonetic=True)
            phonetic: If True, text is already phoneme data
        Returns:
            Audio data as bytes (8-bit unsigned PCM, 22050 Hz mono)
        """
        return text_to_audio(
            text,
            pitch=self.pitch,
            speed=self.speed,
            mouth=self.mouth,
            throat=self.throat,
            singmode=self.singmode,
            phonetic=phonetic,
            inflection=self.inflection
        )
    def wav(self, text, phonetic=False):
        """
        Convert text to WAV format audio.
        Args:
            text: Input text (or phoneme string if phonetic=True)
            phonetic: If True, text is already phoneme data
        Returns:
            WAV file data as bytes
        """
        return text_to_wav(
            text,
            pitch=self.pitch,
            speed=self.speed,
            mouth=self.mouth,
            throat=self.throat,
            singmode=self.singmode,
            phonetic=phonetic,
            inflection=self.inflection
        )
    def save_wav(self, text, filename, phonetic=False):
        """
        Convert text to audio and save as WAV file.
        Args:
            text: Input text (or phoneme string if phonetic=True)
            filename: Output filename
            phonetic: If True, text is already phoneme data
        Returns:
            True on success, False on failure
        """
        wav_data = self.wav(text, phonetic)
        if wav_data is None:
            return False
        with open(filename, 'wb') as f:
            f.write(wav_data)
        return True
