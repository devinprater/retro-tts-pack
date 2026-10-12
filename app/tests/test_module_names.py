"""Installer migration keeps engine keys but replaces old module configs."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class ModuleNameMigrationTests(unittest.TestCase):
    def test_installer_replaces_old_microsoft_entries(self):
        root = Path(__file__).resolve().parents[2]
        script = (root / 'install.sh').read_text()
        start = script.index('for module in $available_modules; do')
        end = script.index('\ndone', start) + len('\ndone')
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            for old in ('mssam', 'onecore'):
                (destination / (old + '-generic.conf')).write_text('Old module config\n')
            subprocess.run(['sh', '-c', script[start:end]], check=True,
                           env=dict(os.environ, available_modules='mssam onecore',
                                    INSTALL_DIR=str(root), MODULE_DIR=temporary,
                                    SPEECHD_CLIENT='/test/bin/retro-tts'))
            for old, new in [('mssam', 'microsoft-sapi5'),
                             ('onecore', 'microsoft-speech-platform')]:
                self.assertFalse((destination / (old + '-generic.conf')).exists())
                config = (destination / (new + '-generic.conf')).read_text()
                self.assertIn('/test/bin/retro-tts', config)
                self.assertIn('--engine ' + old, config)


if __name__ == '__main__':
    unittest.main()
