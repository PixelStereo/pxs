import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from build_id import display_version  # noqa: E402


def git(root, *args):
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid", *args], cwd=root, check=True,
                   capture_output=True)


class BuildIdTests(unittest.TestCase):
    def test_release_and_commits_since(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            git(root, "init", "-q")
            (root / "f").write_text("1")
            git(root, "add", "f")
            git(root, "commit", "-qm", "one")
            git(root, "tag", "v0.2.1")
            self.assertEqual(display_version(root), "0.2.1")
            self.assertEqual(display_version(root, tag="v0.3.0"), "0.3.0")
            (root / "f").write_text("2")
            git(root, "commit", "-qam", "two")
            self.assertEqual(display_version(root), "0.2.1+1")
            (root / "f").write_text("3")
            self.assertEqual(display_version(root), "0.2.1+1+")
            git(root, "checkout", "-q", "f")
            git(root, "checkout", "-q", "v0.2.1")
            (root / "new").write_text("x")
            self.assertEqual(display_version(root), "0.2.1+0+")

    def test_no_git_uses_the_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(display_version(Path(tmp), fallback="1.2.3"), "1.2.3")


if __name__ == "__main__":
    unittest.main()
