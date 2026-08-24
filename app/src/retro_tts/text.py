from __future__ import annotations

import re
import unicodedata


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
        # Modern UI breadcrumb/prompt chevrons have no useful CP1252 form.
        # Passing them through "replace" makes Centigram speak the resulting
        # question-mark byte as an "uh"-like vocalization.
        "\u2039": " ",
        "\u203a": " ",
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


def legacy_bytes(text: str, encoding: str = "cp1252") -> bytes:
    """Encode text without leaking Unicode replacement marks to old engines.

    Vintage engines commonly vocalize `?` or misread UTF-8's individual bytes
    when handed a character outside their codepage. Preserve letters supported
    by the requested language, fold decomposable Latin letters, and turn other
    non-ASCII punctuation/symbols into word boundaries rather than phonemes.
    """
    output = bytearray()
    for character in normalize_text(text):
        category = unicodedata.category(character)
        if ord(character) > 127 and category[:1] in ("P", "S"):
            output.extend(b" ")
            continue
        try:
            output.extend(character.encode(encoding, "strict"))
        except UnicodeEncodeError:
            folded = unicodedata.normalize("NFKD", character)
            encoded = bytearray()
            for component in folded:
                if unicodedata.category(component).startswith("M"):
                    continue
                try:
                    encoded.extend(component.encode(encoding, "strict"))
                except UnicodeEncodeError:
                    encoded.clear()
                    break
            if encoded:
                output.extend(encoded)
            elif category[:1] != "M":
                output.extend(b" ")
    return re.sub(rb"[ \t]+", b" ", bytes(output)).strip()


__all__ = ["legacy_bytes", "normalize_text"]
