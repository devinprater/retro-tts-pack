from array import array

from retro_tts.engines import lhtts, truevoice


def test_centigram_catalog_has_all_recovered_voices():
    assert list(truevoice.VOICES) == [
        "Peter", "Sidney", "Eager Eddie", "Deep Douglas", "Biff",
        "Grandpa Amos", "Melvin", "Alex", "Wanda", "Julia",
    ]


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
