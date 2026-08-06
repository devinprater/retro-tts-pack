from __future__ import annotations

import re


_TRANSLATION = str.maketrans(
    {
        "\u2018": "'",
        "\u2019": "'",
        "\u02bc": "'",
        "\u201c": '"',
        "\u201d": '"',
        "\u2013": " - ",
        "\u2014": " - ",
        "\u2026": "...",
        "\u00a0": " ",
    }
)


def normalize_text(text: str) -> str:
    """Make modern punctuation predictable for the vintage engines.

    Internal apostrophes are deliberately retained: contractions and
    possessives are words, not punctuation separators.  Quotation marks are
    removed because the reciter-based engines otherwise try to pronounce them
    as part of the adjacent word.
    """
    text = text.translate(_TRANSLATION)
    text = text.replace('"', " ")
    text = re.sub(r"(?<![A-Za-z0-9])'|'(?![A-Za-z0-9])", " ", text)
    text = re.sub(r"[ \t]+", " ", text)
    return text.strip()


__all__ = ["normalize_text"]
