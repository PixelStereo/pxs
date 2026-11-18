import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release_downloads as rd


def release(tag, names, published="2026-01-01T00:00:00Z", **extra):
    return {"tag_name": tag, "published_at": published, "id": 1, "draft": False,
            "assets": [{"name": n, "state": "uploaded", "size": 10} for n in names], **extra}


NEW = ["App-macos-arm64-v1.2.0.zip", "App-macos-x64-v1.2.0.zip", "App-windows-x64-v1.2.0-Setup.exe",
       "App-windows-x64-v1.2.0.zip", "App-linux-x64-v1.2.0.AppImage", "App-linux-x64-v1.2.0.tar.gz"]


class ReleaseDownloads(unittest.TestCase):
    def test_prefers_arm64_installer_appimage(self):
        _, urls = rd.select([release("v1.2.0", NEW)], "App", "o/App", ["mac", "win", "linux"])
        self.assertTrue(urls["mac"].endswith("App-macos-arm64-v1.2.0.zip"))
        self.assertTrue(urls["win"].endswith("-Setup.exe"))
        self.assertTrue(urls["linux"].endswith(".AppImage"))

    def test_incomplete_release_is_skipped(self):
        old = release("v1.1.0", [n.replace("1.2.0", "1.1.0") for n in NEW], "2026-01-01T00:00:00Z")
        new = release("v1.2.0", NEW[:2], "2026-02-01T00:00:00Z")
        chosen, _ = rd.select([old, new], "App", "o/App", ["mac", "win"])
        self.assertEqual(chosen["tag_name"], "v1.1.0")

    def test_legacy_names(self):
        legacy = ["App-macOS-arm64-v0.2.2.zip", "App-Windows-x64-v0.2.2-Setup.exe"]
        _, urls = rd.select([release("v0.2.2", legacy)], "App", "o/App", ["mac", "win"])
        self.assertEqual(len(urls), 2)

    def test_other_app_files_are_ignored(self):
        _, urls = rd.select([release("v1.2.0", NEW + ["App2-macos-arm64-v1.2.0.zip"])], "App", "o/App", ["mac"])
        self.assertIn("App-macos-arm64", urls["mac"])

    def test_update_rewrites_block_and_version(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "README.md").write_text(f"# App\n{rd.START}\nold\n{rd.END}\nrest\n")
            tag = rd.update(root, [release("v1.2.0", NEW)], "App", "o/App", ["mac", "win"], "https://x/")
            text = (root / "README.md").read_text()
            self.assertEqual(tag, "v1.2.0")
            self.assertIn("img.shields.io/badge/macOS-Download", text)
            self.assertIn("documentation", text)
            self.assertTrue(text.startswith("# App\n") and text.endswith("rest\n"))
            self.assertEqual((root / "VERSION").read_text(), "1.2.0\n")

    def test_markers_required(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "README.md").write_text("no markers")
            with self.assertRaises(ValueError):
                rd.update(Path(d), [release("v1.2.0", NEW)], "App", "o/App", ["mac"])


if __name__ == "__main__":
    unittest.main()
