# STSPEECH - Atari ST Speech Synthesizer V2.0
# Pure Python engine with numpy audio rendering

from .engine import STSpeech, text_to_phonemes, text_to_wav

__all__ = ['STSpeech', 'text_to_phonemes', 'text_to_wav']
__version__ = '2.0.0'
