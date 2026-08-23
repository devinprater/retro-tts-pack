"""Compatibility wrapper for Panthera's Leopard generation."""

from ..pantheraspeech import preload as _preload
from ..pantheraspeech import stream_pcm as _stream_pcm
from ..pantheraspeech import text_to_wav as _text_to_wav


def preload() -> None:
    _preload("leopardspeech")


def stream_pcm(*args, **kwargs):
    kwargs["generation"] = "leopardspeech"
    return _stream_pcm(*args, **kwargs)


def text_to_wav(*args, **kwargs):
    kwargs["generation"] = "leopardspeech"
    return _text_to_wav(*args, **kwargs)


__all__ = ["preload", "stream_pcm", "text_to_wav"]
