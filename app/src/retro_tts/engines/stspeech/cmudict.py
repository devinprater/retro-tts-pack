# cmudict.py - CMU Pronouncing Dictionary lookup
# Ported from engine/src/cmudict.c

_dict = None


def _load():
    global _dict
    if _dict is not None:
        return
    try:
        from .cmudict_data import CMUDICT
        _dict = CMUDICT
    except ImportError:
        _dict = {}


def cmudict_lookup(word):
    """Look up a word in the CMU dictionary.

    Args:
        word: English word (case-insensitive).

    Returns:
        Phoneme string, or None if not found.
    """
    _load()
    return _dict.get(word.upper())
