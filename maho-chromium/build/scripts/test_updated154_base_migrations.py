import hashlib
import os
import subprocess
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import patch

from apply_chromium_src_overrides import (
    _UPDATED154_FOCUS_SHA256,
    _UPDATED154_INCREMENTAL_SHA256,
    _UPDATED154_LAYOUT_SHA256,
    _UPDATED154_OLD_SHA256,
    _UPDATED154_REVISION,
    apply_base_patch,
    default_chromium_src,
    repo_root,
)


class Updated154BaseMigrationTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.enclosing = Path(self.tmp.name)
        subprocess.run(["git", "init", "--quiet", str(self.enclosing)],
                       check=True, capture_output=True)
        self.src = self.enclosing / "fixture"
        subprocess.run(["git", "init", "--quiet", str(self.src)],
                       check=True, capture_output=True)
        self.upstream = Path(os.environ.get(
            "MAHO_CHROMIUM_SRC", str(default_chromium_src())))

    def prepare(self, relative_path, *, incremental=False, focus=False, layout=False):
        target = self.src / relative_path
        target.parent.mkdir(parents=True, exist_ok=True)
        pristine = subprocess.run(
            ["git", "-C", str(self.upstream), "show",
             f"{_UPDATED154_REVISION}:{relative_path}"],
            check=True, capture_output=True).stdout
        target.write_bytes(pristine)
        base = (repo_root() / "build" / "chromium_base_patches" /
                _UPDATED154_REVISION / f"{relative_path}.patch")
        directory = ("chromium_base_incremental_migrations" if incremental
                     else "chromium_base_migrations")
        expected = (_UPDATED154_INCREMENTAL_SHA256 if incremental
                    else _UPDATED154_OLD_SHA256)
        if focus:
            directory = "chromium_base_focus_migrations"
            expected = _UPDATED154_FOCUS_SHA256
        if layout:
            directory = "chromium_base_layout_migrations"
            expected = _UPDATED154_LAYOUT_SHA256
        migration = (repo_root() / "build" / directory /
                     _UPDATED154_REVISION / f"{relative_path}.patch")
        self.git_apply(base)
        current = target.read_bytes()
        self.git_apply(migration, "--reverse")
        old = target.read_bytes()
        self.assertEqual(hashlib.sha256(old).hexdigest(),
                         expected[relative_path])
        return target, base, pristine, current, old

    def git_apply(self, patch, *options):
        try:
            subprocess.run(
                ["git", "-C", str(self.src), "apply", *options, str(patch)],
                check=True, capture_output=True)
        except subprocess.CalledProcessError as error:
            error.add_note(
                f"stdout:\n{(error.stdout or b'').decode(errors='replace')}\n"
                f"stderr:\n{(error.stderr or b'').decode(errors='replace')}")
            raise

    def test_registered_paths_match_shipped_migrations(self):
        directory = (repo_root() / "build" / "chromium_base_migrations" /
                     _UPDATED154_REVISION)
        shipped = {path.relative_to(directory).as_posix().removesuffix(".patch")
                   for path in directory.rglob("*.patch")}
        self.assertEqual(shipped, set(_UPDATED154_OLD_SHA256))
        incremental_directory = (repo_root() / "build" /
                                 "chromium_base_incremental_migrations" /
                                 _UPDATED154_REVISION)
        incremental_shipped = {
            path.relative_to(incremental_directory).as_posix().removesuffix(".patch")
            for path in incremental_directory.rglob("*.patch")}
        self.assertEqual(incremental_shipped, set(_UPDATED154_INCREMENTAL_SHA256))
        focus_directory = (repo_root() / "build" /
                           "chromium_base_focus_migrations" / _UPDATED154_REVISION)
        self.assertEqual(
            {path.relative_to(focus_directory).as_posix().removesuffix(".patch")
             for path in focus_directory.rglob("*.patch")},
            set(_UPDATED154_FOCUS_SHA256))
        layout_directory = (repo_root() / "build" /
                            "chromium_base_layout_migrations" / _UPDATED154_REVISION)
        self.assertEqual(
            {path.relative_to(layout_directory).as_posix().removesuffix(".patch")
             for path in layout_directory.rglob("*.patch")},
            set(_UPDATED154_LAYOUT_SHA256))

    def drift_sources(self, relative_path, old, directory):
        migration = (repo_root() / "build" / directory /
                     _UPDATED154_REVISION / f"{relative_path}.patch")
        lines = migration.read_bytes().splitlines(keepends=True)
        removed = [line[1:] for line in lines
                   if line.startswith(b"-") and not line.startswith(b"---")
                   and line[1:].strip()]
        context = [line[1:] for line in lines
                   if line.startswith(b" ") and line[1:].strip()]
        anchor = (removed or context)[0]
        self.assertIn(anchor, old)
        sources = (
            old + b"\n// unrelated edit\n",
            old.replace(b"#include", b"# include", 1),
            old.replace(anchor, b"MAHO_DRIFT " + anchor, 1),
        )
        for source in sources:
            self.assertNotEqual(source, old, relative_path)
        return sources

    def test_focus_stage_converges_and_rejects_drift(self):
        for relative_path in _UPDATED154_FOCUS_SHA256:
            target, base, _, current, old = self.prepare(relative_path, focus=True)
            copied = self.src / "unregistered.patch"
            copied.write_bytes(base.read_bytes())
            for dry_run in (True, False):
                target.write_bytes(old)
                self.assertEqual(apply_base_patch(
                    self.src, relative_path, base, dry_run), "patched")
                self.assertEqual(target.read_bytes(), old if dry_run else current)
            self.assertEqual(apply_base_patch(
                self.src, relative_path, base, False), "already")
            cases = [(source, base) for source in self.drift_sources(
                relative_path, old, "chromium_base_focus_migrations")]
            for source, candidate in (*cases, (old, copied)):
                for dry_run in (True, False):
                    target.write_bytes(source)
                    with self.assertRaises(RuntimeError):
                        apply_base_patch(self.src, relative_path, candidate, dry_run)
                    self.assertEqual(target.read_bytes(), source)

    def test_layout_stage_converges_and_rejects_drift(self):
        for relative_path in _UPDATED154_LAYOUT_SHA256:
            target, base, pristine, current, old = self.prepare(relative_path, layout=True)
            copied = self.src / "unregistered.patch"
            copied.write_bytes(base.read_bytes())
            for source, expected in ((pristine, "patched"), (old, "patched"),
                                     (current, "already")):
                for dry_run in (True, False):
                    target.write_bytes(source)
                    self.assertEqual(apply_base_patch(
                        self.src, relative_path, base, dry_run), expected)
                    self.assertEqual(target.read_bytes(), source if dry_run else current)
            self.git_apply(base, "--reverse", "--check")
            self.assertEqual(apply_base_patch(
                self.src, relative_path, base, False), "already")
            cases = [(source, base) for source in self.drift_sources(
                relative_path, old, "chromium_base_layout_migrations")]
            for source, candidate in (*cases, (old, copied)):
                for dry_run in (True, False):
                    target.write_bytes(source)
                    with self.assertRaises(RuntimeError):
                        apply_base_patch(self.src, relative_path, candidate, dry_run)
                    self.assertEqual(target.read_bytes(), source)

    def test_incremental_stage_reaches_current_and_is_idempotent(self):
        for relative_path in _UPDATED154_INCREMENTAL_SHA256:
            target, base, _, current, old = self.prepare(relative_path, incremental=True)
            for dry_run in (True, False):
                with self.subTest(path=relative_path, dry_run=dry_run):
                    target.write_bytes(old)
                    self.assertEqual(apply_base_patch(
                        self.src, relative_path, base, dry_run), "patched")
                    self.assertEqual(target.read_bytes(), old if dry_run else current)
            self.git_apply(base, "--reverse", "--check")
            self.assertEqual(apply_base_patch(
                self.src, relative_path, base, False), "already")
            self.assertEqual(target.read_bytes(), current)

    def test_incremental_stage_drift_and_noncanonical_patch_are_rejected(self):
        for relative_path in _UPDATED154_INCREMENTAL_SHA256:
            target, base, _, _, old = self.prepare(relative_path, incremental=True)
            copied = self.src / "unregistered.patch"
            copied.write_bytes(base.read_bytes())
            cases = [(source, base) for source in self.drift_sources(
                relative_path, old, "chromium_base_incremental_migrations")]
            for source, candidate in (*cases, (old, copied)):
                for dry_run in (True, False):
                    with self.subTest(path=relative_path, dry_run=dry_run,
                                      digest=hashlib.sha256(source).hexdigest(),
                                      canonical=candidate == base):
                        target.write_bytes(source)
                        with self.assertRaises(RuntimeError):
                            apply_base_patch(self.src, relative_path, candidate, dry_run)
                        self.assertEqual(target.read_bytes(), source)

    def test_invalid_incremental_migration_is_rejected_before_source_changes(self):
        for relative_path in _UPDATED154_INCREMENTAL_SHA256:
            target, base, _, _, old = self.prepare(relative_path, incremental=True)
            overlay = self.src / "overlay"
            copied_base = (overlay / "build" / "chromium_base_patches" /
                           _UPDATED154_REVISION / f"{relative_path}.patch")
            copied_migration = (overlay / "build" / "chromium_base_incremental_migrations" /
                                _UPDATED154_REVISION / f"{relative_path}.patch")
            copied_base.parent.mkdir(parents=True, exist_ok=True)
            copied_migration.parent.mkdir(parents=True, exist_ok=True)
            copied_base.write_bytes(base.read_bytes())
            migration = (repo_root() / "build" / "chromium_base_incremental_migrations" /
                         _UPDATED154_REVISION / f"{relative_path}.patch")
            lines = migration.read_text().splitlines(keepends=True)
            for index, line in enumerate(lines):
                if line.startswith("+") and not line.startswith("+++"):
                    lines[index] = line.rstrip("\n") + " // invalid transition\n"
                    break
            invalid_transition = "".join(lines)
            for contents in ("invalid patch\n", invalid_transition):
                copied_migration.write_bytes(contents.encode("utf-8"))
                self.assertNotIn(b"\r\n", copied_migration.read_bytes())
                for dry_run in (True, False):
                    with self.subTest(path=relative_path, dry_run=dry_run,
                                      malformed=contents == "invalid patch\n"):
                        with patch("apply_chromium_src_overrides.repo_root", return_value=overlay):
                            with self.assertRaises(RuntimeError):
                                apply_base_patch(self.src, relative_path, copied_base, dry_run)
                        self.assertEqual(target.read_bytes(), old)

    def test_migration_staging_inside_enclosing_repository(self):
        relative_path = "chrome/browser/ui/views/side_panel/side_panel.cc"
        target, base, _, current, old = self.prepare(relative_path)
        sentinel = self.enclosing / relative_path
        sentinel.parent.mkdir(parents=True)
        sentinel.write_bytes(old)
        subprocess.run(["git", "-C", str(self.enclosing), "add", relative_path],
                       check=True, capture_output=True)
        index_before = (self.enclosing / ".git" / "index").read_bytes()
        temporary_directory = tempfile.TemporaryDirectory

        @contextmanager
        def nested_directory(*args, **kwargs):
            with temporary_directory(*args, dir=self.enclosing, **kwargs) as staged:
                yield staged
                self.assertEqual((Path(staged) / relative_path).read_bytes(),
                                 current)

        with patch("apply_chromium_src_overrides.tempfile.TemporaryDirectory",
                   side_effect=nested_directory):
            self.assertEqual(apply_base_patch(
                self.src, relative_path, base, dry_run=True), "patched")
            self.assertEqual(target.read_bytes(), old)
            self.assertEqual(apply_base_patch(
                self.src, relative_path, base, dry_run=False), "patched")
            self.assertEqual(target.read_bytes(), current)
            self.assertEqual(apply_base_patch(
                self.src, relative_path, base, dry_run=False), "already")

        self.assertEqual(sentinel.read_bytes(), old)
        self.assertEqual((self.enclosing / ".git" / "index").read_bytes(),
                         index_before)

    def test_pristine_current_known_old_and_idempotence(self):
        for relative_path in _UPDATED154_OLD_SHA256:
            target, base, pristine, current, old = self.prepare(relative_path)
            for name, source, expected in (
                ("pristine", pristine, "patched"),
                ("current", current, "already"),
                ("known_old", old, "patched"),
            ):
                with self.subTest(path=relative_path, state=name):
                    target.write_bytes(source)
                    self.assertEqual(apply_base_patch(
                        self.src, relative_path, base, dry_run=True), expected)
                    self.assertEqual(target.read_bytes(), source)
                    self.assertEqual(apply_base_patch(
                        self.src, relative_path, base, dry_run=False), expected)
                    self.assertEqual(target.read_bytes(), current)
                    self.git_apply(base, "--reverse", "--check")
                    self.assertEqual(apply_base_patch(
                        self.src, relative_path, base, dry_run=False), "already")
                    self.assertEqual(target.read_bytes(), current)

    def test_unknown_old_drift_is_rejected_without_writes(self):
        for relative_path in _UPDATED154_OLD_SHA256:
            target, base, _, _, old = self.prepare(relative_path)
            for source in self.drift_sources(
                    relative_path, old, "chromium_base_migrations"):
                for dry_run in (True, False):
                    with self.subTest(path=relative_path, dry_run=dry_run,
                                      digest=hashlib.sha256(source).hexdigest()):
                        target.write_bytes(source)
                        with self.assertRaises(RuntimeError):
                            apply_base_patch(self.src, relative_path, base, dry_run)
                        self.assertEqual(target.read_bytes(), source)

    def test_unrelated_edits_outside_current_hunks_are_preserved(self):
        for relative_path in _UPDATED154_OLD_SHA256:
            target, base, pristine, current, _ = self.prepare(relative_path)
            for source in (pristine, current):
                with self.subTest(path=relative_path, pristine=source == pristine):
                    suffix = b"\n// unrelated edit\n"
                    target.write_bytes(source + suffix)
                    apply_base_patch(self.src, relative_path, base, dry_run=False)
                    self.assertEqual(target.read_bytes(), current + suffix)

    def test_noncanonical_patch_cannot_enable_migration(self):
        for relative_path in _UPDATED154_OLD_SHA256:
            target, base, _, _, old = self.prepare(relative_path)
            copied = self.src / "unregistered.patch"
            copied.write_bytes(base.read_bytes())
            with self.subTest(path=relative_path):
                with self.assertRaises(RuntimeError):
                    apply_base_patch(self.src, relative_path, copied, dry_run=False)
                self.assertEqual(target.read_bytes(), old)

    def test_invalid_migration_is_rejected_before_source_changes(self):
        for relative_path in _UPDATED154_OLD_SHA256:
            target, base, _, _, old = self.prepare(relative_path)
            overlay = self.src / "overlay"
            copied_base = (overlay / "build" / "chromium_base_patches" /
                           _UPDATED154_REVISION / f"{relative_path}.patch")
            copied_migration = (overlay / "build" / "chromium_base_migrations" /
                                _UPDATED154_REVISION / f"{relative_path}.patch")
            copied_base.parent.mkdir(parents=True, exist_ok=True)
            copied_migration.parent.mkdir(parents=True, exist_ok=True)
            copied_base.write_bytes(base.read_bytes())
            migration = (repo_root() / "build" / "chromium_base_migrations" /
                         _UPDATED154_REVISION / f"{relative_path}.patch")
            lines = migration.read_text().splitlines(keepends=True)
            for index, line in enumerate(lines):
                if line.startswith("+") and not line.startswith("+++"):
                    lines[index] = line.rstrip("\n") + " // invalid transition\n"
                    break
            else:
                for index, line in enumerate(lines):
                    if line.startswith(" ") and line[1:].strip():
                        lines[index] = (
                            "-" + line[1:] +
                            "+" + line[1:].rstrip("\n") +
                            " // invalid transition\n")
                        break
            invalid_transition = "".join(lines)
            self.assertNotEqual(invalid_transition, migration.read_text())
            copied_migration.write_bytes(invalid_transition.encode("utf-8"))
            self.assertNotIn(b"\r\n", copied_migration.read_bytes())
            self.git_apply(copied_migration, "--check")
            for contents in ("invalid patch\n", invalid_transition):
                copied_migration.write_bytes(contents.encode("utf-8"))
                self.assertNotIn(b"\r\n", copied_migration.read_bytes())
                with self.subTest(path=relative_path, malformed=contents == "invalid patch\n"):
                    with patch("apply_chromium_src_overrides.repo_root", return_value=overlay):
                        with self.assertRaises(RuntimeError):
                            apply_base_patch(self.src, relative_path, copied_base, False)
                    self.assertEqual(target.read_bytes(), old)


if __name__ == "__main__":
    unittest.main()
