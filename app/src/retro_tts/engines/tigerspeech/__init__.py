"""Panthera-compatible Tiger generation."""

from ..pantheraspeech import stream_pcm as _stream_pcm
from ..pantheraspeech import text_to_wav as _text_to_wav


def stream_pcm(*args, **kwargs):
    kwargs["generation"] = "tigerspeech"
    return _stream_pcm(*args, **kwargs)


def text_to_wav(*args, **kwargs):
    kwargs["generation"] = "tigerspeech"
    return _text_to_wav(*args, **kwargs)


__all__ = ["stream_pcm", "text_to_wav"]
