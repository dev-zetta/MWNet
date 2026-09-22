#!/usr/bin/env python3
"""Exercise local-launcher arguments with isolated stub client/server processes."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class LocalLauncherTests(unittest.TestCase):
    def run_launcher(self, credentials):
        with tempfile.TemporaryDirectory(prefix="tes3mp-launcher-") as temporary:
            root = Path(temporary)
            shutil.copy2(Path(__file__).resolve().parents[1] / "run-tes3mp-local.sh", root)
            for directory in ("build", "profile", "data"):
                (root / directory).mkdir()
            for name in ("Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm",
                         "Morrowind.bsa", "tribunal.bsa", "Bloodmoon.bsa"):
                (root / "data" / name).touch()
            scripts = {
                "build/tes3mp": "#!/bin/sh\nexit 0\n",
                "build/tes3mp-server": "#!/bin/sh\nexit 0\n",
                "run-tes3mp-server.sh": "#!/bin/sh\nexec sleep 60\n",
                "run-tes3mp.sh": "#!/usr/bin/env python3\nimport json, os, sys\n"
                    "open(os.environ['TEST_CAPTURE'], 'w').write(json.dumps(sys.argv[1:]))\n",
            }
            for name, source in scripts.items():
                path = root / name
                path.write_text(source)
                path.chmod(0o755)
            capture = root / "arguments.json"
            env = dict(os.environ, TES3MP_BUILD_DIR=str(root / "build"),
                       TES3MP_TEST_ROOT=str(root / "state"), TEST_CAPTURE=str(capture))
            subprocess.run(["bash", str(root / "run-tes3mp-local.sh"),
                            "--data-dir", str(root / "data"), "--client-profile",
                            str(root / "profile"), "--", *credentials],
                           env=env, check=True, capture_output=True, timeout=15)
            return json.loads(capture.read_text())

    def test_no_credentials_opens_ui_with_content(self):
        args = self.run_launcher([])
        self.assertFalse(any(arg.startswith("--connect") for arg in args))
        self.assertNotIn("--skip-menu", args)
        self.assertEqual([args[i + 1] for i, arg in enumerate(args) if arg == "--content"],
                         ["Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"])
        self.assertEqual([args[i + 1] for i, arg in enumerate(args) if arg == "--fallback-archive"],
                         ["Morrowind.bsa", "tribunal.bsa", "Bloodmoon.bsa"])

    def test_password_file_enables_local_direct_connect(self):
        for credentials in (["--account", "Tester", "--account-password-file", "/private/password"],
                            ["--account=Tester", "--account-password-file=/private/password"]):
            with self.subTest(credentials=credentials):
                args = self.run_launcher(credentials)
                self.assertIn("--connect=127.0.0.1:25565", args)
                self.assertIn("--skip-menu", args)
                self.assertEqual(args[-len(credentials):], credentials)


if __name__ == "__main__":
    unittest.main()
