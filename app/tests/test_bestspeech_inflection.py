"""Regression coverage for the DLL-preset to openbst CLI conversion."""

from array import array
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import wave

from retro_tts.engines import bestspeech


class BestSpeechInflectionTests(unittest.TestCase):
    def parameters(self, pitch=50, voice="fred", text="Testing speech."):
        commands = []

        def render(command, **kwargs):
            commands.append(command)
            output = command[command.index("--filename") + 1]
            with wave.open(output, "wb") as audio:
                audio.setparams((1, 2, 11025, 0, "NONE", ""))
                audio.writeframes(array("h", [1000, -1000] * 550).tobytes())
            return SimpleNamespace(returncode=0, stderr=b"")

        with patch.object(bestspeech.subprocess, "run", side_effect=render):
            self.assertGreater(len(bestspeech.text_to_wav(text, 90, pitch, voice)), 44)
        command = commands[0]
        self.assertFalse(Path(command[command.index("--filename") + 1]).exists())
        return {flag: command[command.index(flag) + 1]
                for flag in ("--pitch", "--top", "--rate", "--voice")}

    def test_fred_default_has_a_full_upper_contour(self):
        parameters = self.parameters()
        self.assertEqual(parameters["--pitch"], "80")
        self.assertEqual(parameters["--top"], "160")
        self.assertEqual(parameters["--rate"], "-61")

    def test_upper_contour_tracks_the_selected_pitch(self):
        for pitch, base, top in [(25, 60, 120), (75, 120, 240)]:
            with self.subTest(pitch=pitch):
                parameters = self.parameters(pitch=pitch)
                self.assertEqual(parameters["--pitch"], str(base))
                self.assertEqual(parameters["--top"], str(top))

    def test_personality_inflection_preserves_its_relative_range(self):
        for voice, top in [("sara", 144), ("alien", 120), ("martha", 240)]:
            with self.subTest(voice=voice):
                self.assertEqual(self.parameters(voice=voice)["--top"], str(top))

    def test_uppercase_echo_raises_both_pitch_and_contour(self):
        parameters = self.parameters(text="A")
        self.assertEqual(parameters["--pitch"], "112")
        self.assertEqual(parameters["--top"], "224")


if __name__ == "__main__":
    unittest.main()
