from array import array

from retro_tts.engines import lhtts, truevoice
from retro_tts.server import _lh_join_pause_ms, _phrase_chunks
from retro_tts.text import legacy_bytes, normalize_text


def test_centigram_catalog_has_all_recovered_voices():
    assert list(truevoice.VOICES) == [
        "Peter", "Sidney", "Eager Eddie", "Deep Douglas", "Biff",
        "Grandpa Amos", "Melvin", "Alex", "Wanda", "Julia",
    ]


def test_centigram_neutral_pitch_uses_original_per_voice_defaults():
    assert truevoice._native_pitch(50, truevoice.VOICES["Peter"]) == 85
    assert truevoice._native_pitch(50, truevoice.VOICES["Wanda"]) == 208
    assert truevoice._native_pitch(0, truevoice.VOICES["Peter"]) == 50
    assert truevoice._native_pitch(100, truevoice.VOICES["Peter"]) == 400


def test_lh_catalog_exposes_only_verified_voices():
    assert len(lhtts.VOICE_CATALOG) == 28
    assert lhtts._resolve_voice("Svetlana") == ("Russian", 2)
    assert lhtts._resolve_voice("Shin-Ah (Korean)") == ("Korean", 3)
    assert lhtts._resolve_voice("Peter") == lhtts.VOICE_CATALOG[lhtts.DEFAULT_VOICE]


def test_lh_time_scaling_changes_duration_without_changing_sample_format():
    pcm = array("h", (index % 2000 - 1000 for index in range(22_050))).tobytes()
    slow = lhtts._time_scale(pcm, 11_025, 0.70)
    fast = lhtts._time_scale(pcm, 11_025, 2.25)
    assert len(slow) > len(pcm) > len(fast)
    assert len(slow) % 2 == len(fast) % 2 == 0


def test_streaming_phrase_chunks_front_load_first_audio():
    chunks = _phrase_chunks(
        "Download Retro TTS Pack 1.1.1 "
        "https://github.com/devinprater/retro-tts-pack/releases/tag/v1.1.1"
    )
    assert chunks[0] == "Download Retro TTS Pack 1.1.1"
    assert " ".join(chunks).startswith("Download Retro TTS Pack 1.1.1")


def test_truevoice_keeps_short_bullets_together():
    assert _phrase_chunks("- 90 ms after commas") == ["- 90 ms after commas"]


def test_modern_chevrons_do_not_reach_legacy_codepages():
    assert normalize_text("Settings › Speech") == "Settings Speech"
    assert normalize_text("Back ‹ Home") == "Back Home"


def test_legacy_encoding_never_introduces_spoken_replacement_marks():
    assert legacy_bytes("before ─ after › done 🔊") == b"before after done"
    assert legacy_bytes("caf\N{LATIN SMALL LETTER E WITH ACUTE}") == b"caf\xe9"
    assert legacy_bytes("Привет", "cp1251").decode("cp1251") == "Привет"
    assert legacy_bytes("안녕", "cp949").decode("cp949") == "안녕"


def test_lh_streaming_respects_written_prosody():
    text = (
        "I haven't published another, release yet; it would be useful to "
        "confirm, that the lower pitch and phrase joins sound, right in Orca first."
    )
    assert _phrase_chunks(text, aggressive=False) == [
        "I haven't published another,",
        "release yet;",
        "it would be useful to confirm,",
        "that the lower pitch and phrase joins sound,",
        "right in Orca first.",
    ]
    assert _lh_join_pause_ms("live L&H path,") == 90
    assert _lh_join_pause_ms("release yet;") == 130
    assert _lh_join_pause_ms("first sentence.") == 170
