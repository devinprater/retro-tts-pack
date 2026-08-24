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
