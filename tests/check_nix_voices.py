"""Render all Nix-packaged Panthera/Microsoft voices without playback.

Usage: python3 tests/check_nix_voices.py result
Requires the host's i686 execution support for Panthera.
"""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import wave


def check(package):
    data = Path(package).resolve() / "share/retro-tts-pack"
    environment = dict(os.environ, PYTHONPATH=str(data / "app/src"))
    for engine, executable in (("MSSAM", "sam_say"), ("SAPI4", "sapi4_speak"),
                               ("ONECORE", "zira_say"), ("BESTSPEECH", "bst_cli")):
        environment[f"RETRO_TTS_{engine}_CLI"] = str(data / "bin" / executable)
        environment[f"RETRO_TTS_{engine}_DATA"] = str(data / "assets" / engine.lower())
    for prefix, engine in (("TIGER", "tigerspeech"), ("LEOPARD", "leopardspeech"),
                          ("LION", "lionspeech")):
        environment[f"RETRO_TTS_{prefix}_HOST"] = str(data / "bin/panthera_host")
        environment[f"RETRO_TTS_{prefix}_BACKEND"] = "native"
        environment[f"RETRO_TTS_{prefix}_TREE"] = str(data / "assets" / engine / (engine + "-data"))
    count = 0
    with tempfile.TemporaryDirectory(prefix="retro-nix-voice-test-") as temporary:
        for engine in ("tigerspeech", "leopardspeech", "lionspeech", "sapi4", "mssam", "onecore", "bestspeech"):
            config = (data / "config/modules" / (engine + "-generic.conf")).read_text()
            voices = re.findall(r'^AddVoice "[^"]+" "[^"]+" "([^"]+)"', config, re.MULTILINE)
            # BestSpeech is tested here as a native-build smoke test; its
            # language/personality regression suite is separate.
            if engine == "bestspeech":
                voices = ["fred"]
            assert voices, engine
            for voice in voices:
                output = Path(temporary) / "voice.wav"
                output.unlink(missing_ok=True)
                result = subprocess.run(
                    [sys.executable, "-m", "retro_tts.cli", "--engine", engine,
                     "--voice", voice, "--text", "Testing speech. This is a second sentence.",
                     "--output", str(output)],
                    env=environment, cwd=temporary, capture_output=True, text=True, timeout=90,
                )
                assert result.returncode == 0, (engine, voice, result.stderr)
                with wave.open(str(output)) as audio:
                    assert audio.getnframes() > 100, (engine, voice)
                    assert any(audio.readframes(audio.getnframes())), (engine, voice)
                count += 1
            print(f"{engine}: {len(voices)} voices passed", flush=True)
    print(f"All {count} packaged voice renders passed.")


if __name__ == "__main__":
    check(sys.argv[1] if len(sys.argv) > 1 else "result")
