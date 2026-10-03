"""Real-Git bootstrap regressions for pins retained only by hidden PR refs."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import sandbox


@unittest.skipUnless(shutil.which("git"), "Git is required for acquisition regressions")
class AcquireTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="sandbox acquire ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.project = self.root / "project"
        (self.project / "config").mkdir(parents=True)
        work = self.root / "engine work"
        work.mkdir()
        self.git(work, "init", "--initial-branch=main")
        (work / "init.sh").write_text("#!/bin/sh\n# base\n")
        self.git(work, "add", "init.sh")
        self.git(work, "commit", "-m", "Base engine")
        self.base = self.git(work, "rev-parse", "HEAD")
        self.git(work, "checkout", "-b", "feature")
        (work / "init.sh").write_text("#!/bin/sh\n# checked dynamics\n")
        self.git(work, "commit", "-am", "Feature engine")
        self.pin = self.git(work, "rev-parse", "HEAD")
        self.git(work, "checkout", "main")
        self.git(work, "merge", "--squash", "feature")
        self.git(work, "commit", "-m", "Squash feature")
        self.main = self.git(work, "rev-parse", "HEAD")
        self.git(work, "branch", "-D", "feature")
        self.git(work, "tag", "engine-release")
        remote = self.root / "remote.git"
        self.git(self.root, "clone", "--bare", str(work), str(remote))
        self.git(remote, "update-ref", "refs/pull/64/head", self.pin)
        self.git(remote, "config", "transfer.hideRefs", "refs/pull")
        self.git(remote, "config", "uploadpack.allowReachableSHA1InWant", "true")
        self.remote_url = remote.as_uri()
        self.patch = patch.object(sandbox, "REPOSITORY_URL", self.remote_url)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        self.set_pin(self.pin)

    @staticmethod
    def git(cwd, *args):
        result = subprocess.run(
            ["git", "-c", "user.name=Bootstrap Test", "-c", "user.email=test@example.invalid",
             "-c", "commit.gpgSign=false", "-c", "core.hooksPath=/dev/null", *args],
            cwd=cwd, text=True, capture_output=True, check=True,
        )
        return result.stdout.strip()

    def set_pin(self, revision):
        (self.project / "config/ludus-version.txt").write_text(revision + "\n")

    def clone_without_pin(self):
        source = self.project / "out/ludus/source"
        source.parent.mkdir(parents=True)
        self.git(self.root, "clone", self.remote_url, str(source))
        result = subprocess.run(["git", "cat-file", "-e", self.pin], cwd=source,
                                capture_output=True)
        self.assertNotEqual(result.returncode, 0, "Normal clone must omit the hidden PR commit")
        return source

    def test_fresh_clone_fetches_exact_pin_after_squash_and_branch_deletion(self):
        source = sandbox.acquire_ludus(self.project, None)
        self.assertEqual(self.git(source, "rev-parse", "HEAD"), self.pin)
        self.assertEqual(self.git(source, "rev-parse", "--abbrev-ref", "HEAD"), "HEAD")

    def test_existing_clone_fetches_missing_pin(self):
        source = self.clone_without_pin()
        self.assertEqual(sandbox.acquire_ludus(self.project, None), source)
        self.assertEqual(self.git(source, "rev-parse", "HEAD"), self.pin)

    def test_repeated_acquisition_honors_changed_pin(self):
        source = sandbox.acquire_ludus(self.project, None)
        sandbox.acquire_ludus(self.project, None)
        self.set_pin(self.base)
        sandbox.acquire_ludus(self.project, None)
        self.assertEqual(self.git(source, "rev-parse", "HEAD"), self.base)

    def test_branch_pin_uses_fetched_head_instead_of_stale_local_branch(self):
        source = self.clone_without_pin()
        self.git(source, "reset", "--hard", self.base)
        self.set_pin("main")
        sandbox.acquire_ludus(self.project, None)
        self.assertEqual(self.git(source, "rev-parse", "HEAD"), self.main)

    def test_tag_pin_is_supported(self):
        self.set_pin("engine-release")
        source = sandbox.acquire_ludus(self.project, None)
        self.assertEqual(self.git(source, "rev-parse", "HEAD"), self.main)

    def test_unavailable_pin_fails_without_substituting_main(self):
        source = self.clone_without_pin()
        self.set_pin("f" * 40)
        with self.assertRaises(subprocess.CalledProcessError):
            sandbox.acquire_ludus(self.project, None)
        self.assertEqual(self.git(source, "rev-parse", "HEAD"), self.main)

    def test_local_override_does_not_fetch_or_checkout(self):
        source = self.root / "custom engine"
        source.mkdir()
        (source / "init.sh").touch()
        with patch.object(sandbox, "run") as run:
            self.assertEqual(sandbox.acquire_ludus(self.project, str(source)), source)
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
