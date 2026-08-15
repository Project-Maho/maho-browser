#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from macos_vm_tart import TartConfig


_SCRIPT = Path(__file__).with_name("macos_vm_tart.py")
_SPEC = importlib.util.spec_from_file_location("macos_vm_tart_under_test", _SCRIPT)
assert _SPEC and _SPEC.loader
macos_vm_tart = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = macos_vm_tart
_SPEC.loader.exec_module(macos_vm_tart)


class FakeRunner:
    def __init__(self, responses=()):
        self.responses = list(responses)
        self.calls = []

    def run(self, command, *, input_text=None, timeout=None, env=None):
        self.calls.append((command, input_text, timeout, env))
        if not self.responses:
            raise AssertionError(f"unexpected command: {command.argv!r}")
        return self.responses.pop(0)


def completed(stdout="", returncode=0, stderr=""):
    return macos_vm_tart.CommandResult(returncode, stdout, stderr)


class MacosVmTartTest(unittest.TestCase):
    def config(self, root: Path) -> TartConfig:
        private_key = root / "id_ed25519"
        public_key = root / "id_ed25519.pub"
        private_key.write_text("private", encoding="utf-8")
        public_key.write_text("ssh-ed25519 AAAA test\n", encoding="utf-8")
        private_key.chmod(0o600)
        public_key.chmod(0o644)
        return macos_vm_tart.TartConfig(
            base_image="ghcr.io/cirruslabs/macos-tahoe-base:latest",
            golden_image="maho-e2e-golden",
            preparation_image="maho-e2e-golden-preparing",
            state_path=root / "state.json",
            private_key=private_key,
            public_key=public_key,
            minimum_free_bytes=50 * 1024**3,
        )

    def test_doctor_rejects_arch_and_disk_before_external_commands(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            runner = FakeRunner()
            report = macos_vm_tart.doctor(
                self.config(Path(temporary_dir)),
                runner,
                macos_version=(26, 0),
                architecture="x86_64",
                free_bytes=49 * 1024**3,
            )
        self.assertFalse(report.ok)
        self.assertEqual(
            [(check.name, check.ok) for check in report.checks[:3]],
            [("architecture", False), ("macos_version", True), ("disk_headroom", False)],
        )
        self.assertEqual(runner.calls, [])

    def test_parse_images_accepts_tart_2_32_title_case_fields(self):
        images = macos_vm_tart._parse_images(
            json.dumps(
                [
                    {
                        "Name": "ghcr.io/cirruslabs/macos-tahoe-base:latest",
                        "State": "stopped",
                        "Running": False,
                    }
                ]
            )
        )

        self.assertEqual(
            images,
            {"ghcr.io/cirruslabs/macos-tahoe-base:latest": "stopped"},
        )

    def test_doctor_builds_typed_tool_tap_and_running_image_commands(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            runner = FakeRunner(
                [
                    completed("2.26.0\n"),
                    completed("cirruslabs/cli\n"),
                    completed("/opt/homebrew/bin/sshpass\n"),
                    completed('[{"name":"other","state":"stopped"}]'),
                ]
            )
            report = macos_vm_tart.doctor(
                self.config(Path(temporary_dir)), runner,
                macos_version=(26, 1), architecture="arm64", free_bytes=60 * 1024**3,
            )
        self.assertTrue(report.ok)
        self.assertEqual(
            [call[0].argv for call in runner.calls],
            [
                ("tart", "--version"),
                ("brew", "tap"),
                ("command", "-v", "sshpass"),
                ("tart", "list", "--format", "json"),
            ],
        )

    def test_ssh_contract_is_key_only_and_never_accepts_host_password_fallback(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            config = self.config(Path(temporary_dir))
            command = macos_vm_tart.key_only_ssh_command(
                config, "192.0.2.10", ("node", "--version")
            )
        self.assertEqual(command.argv[:2], ("ssh", "-i"))
        self.assertIn("BatchMode=yes", command.argv)
        self.assertIn("PasswordAuthentication=no", command.argv)
        self.assertIn("KbdInteractiveAuthentication=no", command.argv)
        self.assertEqual(command.argv[-3:], ("admin@192.0.2.10", "node", "--version"))

    def test_prepare_is_noop_only_for_matching_complete_owned_state(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir)
            config = self.config(root)
            state = macos_vm_tart.PreparationState.complete_for(
                config, source_identity="sha256:approved"
            )
            macos_vm_tart.write_state_atomic(config.state_path, state)
            runner = FakeRunner([completed('[{"name":"maho-e2e-golden","state":"stopped"}]')])
            result = macos_vm_tart.prepare(config, runner, install_missing=False)
        self.assertEqual(result.disposition, macos_vm_tart.PrepareDisposition.ALREADY_PREPARED)
        self.assertEqual([call[0].argv for call in runner.calls], [("tart", "list", "--format", "json")])

    def test_prepare_refuses_unowned_golden_or_preparation_image(self):
        for image in ("maho-e2e-golden", "maho-e2e-golden-preparing"):
            with self.subTest(image=image), tempfile.TemporaryDirectory() as temporary_dir:
                config = self.config(Path(temporary_dir))
                runner = FakeRunner([completed(json.dumps([{"name": image, "state": "stopped"}]))])
                with self.assertRaises(macos_vm_tart.OwnershipError):
                    macos_vm_tart.prepare(config, runner, install_missing=False)
                self.assertEqual(len(runner.calls), 1)

    def test_command_error_redacts_sshpass_password_and_output(self):
        command = macos_vm_tart.Command(
            ("sshpass", "-p", "bootstrap-secret", "ssh", "admin@guest")
        )
        result = completed(
            stdout="bootstrap-secret",
            returncode=1,
            stderr="failed for bootstrap-secret",
        )

        error = macos_vm_tart.CommandError(command, result)

        self.assertNotIn("bootstrap-secret", str(error))
        self.assertNotIn("bootstrap-secret", repr(error.command.argv))
        self.assertNotIn("bootstrap-secret", error.result.stdout)
        self.assertNotIn("bootstrap-secret", error.result.stderr)
        self.assertIn("<redacted>", str(error))

    def test_pulled_source_identity_selects_only_new_matching_digest(self):
        repository = "ghcr.io/cirruslabs/macos-tahoe-base"
        before = {
            f"{repository}@sha256:aaa": "stopped",
            f"{repository}@sha256:zzz": "stopped",
            "ghcr.io/cirruslabs/other@sha256:new": "stopped",
        }
        after = {
            **before,
            f"{repository}@sha256:mmm": "stopped",
        }

        self.assertEqual(
            macos_vm_tart._pulled_source_identity(
                before,
                after,
                f"{repository}:latest",
            ),
            f"{repository}@sha256:mmm",
        )

    def test_pulled_source_identity_returns_none_when_no_digest_appears(self):
        repository = "ghcr.io/cirruslabs/macos-tahoe-base"
        images = {
            f"{repository}:latest": "stopped",
            f"{repository}@sha256:cached": "stopped",
        }

        self.assertIsNone(
            macos_vm_tart._pulled_source_identity(
                images,
                dict(images),
                f"{repository}:latest",
            )
        )

    def test_pulled_source_identity_rejects_multiple_new_matching_digests(self):
        repository = "ghcr.io/cirruslabs/macos-tahoe-base"
        before = {f"{repository}@sha256:old": "stopped"}
        after = {
            **before,
            f"{repository}@sha256:new-a": "stopped",
            f"{repository}@sha256:new-b": "stopped",
        }

        with self.assertRaisesRegex(
            macos_vm_tart.TartAdapterError,
            "multiple new base image digests",
        ):
            macos_vm_tart._pulled_source_identity(
                before,
                after,
                f"{repository}:latest",
            )

    def test_cached_source_identity_uses_tart_fqn_and_validates_mapping(self):
        repository = "ghcr.io/cirruslabs/macos-tahoe-base"
        base_image = f"{repository}:latest"
        digest = f"{repository}@sha256:cached"
        runner = FakeRunner([completed(f"{digest}\n")])

        identity = macos_vm_tart._cached_source_identity(
            runner,
            {base_image: "stopped", digest: "stopped"},
            base_image,
        )

        self.assertEqual(identity, digest)
        self.assertEqual(
            [call[0].argv for call in runner.calls],
            [("tart", "fqn", base_image)],
        )

    def test_cached_source_identity_fails_closed_for_unlisted_mapping(self):
        repository = "ghcr.io/cirruslabs/macos-tahoe-base"
        base_image = f"{repository}:latest"
        runner = FakeRunner(
            [completed(f"{repository}@sha256:not-listed\n")]
        )

        with self.assertRaisesRegex(
            macos_vm_tart.TartAdapterError,
            "did not resolve to a listed base image digest",
        ):
            macos_vm_tart._cached_source_identity(
                runner,
                {f"{repository}@sha256:cached": "stopped"},
                base_image,
            )

    def test_prepare_command_contract_rechecks_key_and_rejects_password_after_hardening(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir)
            config = self.config(root)
            runner = FakeRunner(
                [
                    completed("[]"),
                    completed(),
                    completed(
                        "ghcr.io/cirruslabs/"
                        "macos-tahoe-base@sha256:source\n"
                    ),
                    completed(),
                    completed('{"OS":"darwin","State":"stopped"}'),
                    completed(),
                    completed('{"Display":"1920x1080","State":"stopped"}'),
                    completed(),
                    completed("192.0.2.20\n"),
                    completed(),
                    completed("ready\n"),
                    completed("provisioned\n"),
                    completed("hardened\n"),
                    completed(returncode=255, stderr="Permission denied"),
                    completed(),
                    completed(),
                ]
            )
            result = macos_vm_tart.prepare(config, runner, install_missing=False)
            state = macos_vm_tart.load_state(config.state_path)

        argv = [call[0].argv for call in runner.calls]
        self.assertEqual(argv[1], ("tart", "pull", config.base_image))
        self.assertEqual(argv[2], ("tart", "fqn", config.base_image))
        self.assertEqual(argv[3], ("tart", "clone", config.base_image, config.preparation_image))
        self.assertEqual(
            argv[4],
            ("tart", "get", config.preparation_image, "--format", "json"),
        )
        self.assertEqual(
            argv[5],
            ("tart", "set", config.preparation_image, "--display", "1920x1080"),
        )
        self.assertEqual(
            argv[6],
            ("tart", "get", config.preparation_image, "--format", "json"),
        )
        self.assertEqual(argv[7], ("tart", "run", "--no-graphics", config.preparation_image))
        self.assertEqual(
            argv[8],
            ("tart", "ip", config.preparation_image, "--wait", "600"),
        )
        self.assertEqual(argv[9][0:4], ("sshpass", "-p", "admin", "ssh"))
        self.assertIn("BatchMode=yes", argv[10])
        self.assertIn("PasswordAuthentication=no", argv[10])
        self.assertIn("MAHO_TART_PROVISION_V1", runner.calls[11][1])
        self.assertIn(
            'export PATH="/opt/homebrew/bin:$PATH"',
            runner.calls[11][1],
        )
        self.assertNotIn("system_profiler", runner.calls[11][1])
        self.assertIn("BatchMode=yes", argv[12])
        self.assertIn("PasswordAuthentication=no", argv[12])
        self.assertEqual(argv[13][0:4], ("sshpass", "-p", "admin", "ssh"))
        self.assertIn("PasswordAuthentication=yes", argv[13])
        self.assertIn("PubkeyAuthentication=no", argv[13])
        self.assertEqual(argv[-2], ("tart", "stop", config.preparation_image))
        self.assertEqual(argv[-1], ("tart", "rename", config.preparation_image, config.golden_image))
        self.assertEqual(result.disposition, macos_vm_tart.PrepareDisposition.PREPARED)
        self.assertTrue(state.complete)
        self.assertEqual(
            state.source_identity,
            '{"base_image":"ghcr.io/cirruslabs/macos-tahoe-base:latest",'
            '"local_metadata":{"OS":"darwin","State":"stopped"},'
            '"resolved_reference":"ghcr.io/cirruslabs/'
            'macos-tahoe-base@sha256:source"}',
        )

    def test_prepare_refuses_to_promote_when_password_auth_still_succeeds(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir)
            config = self.config(root)
            runner = FakeRunner(
                [
                    completed("[]"),
                    completed(),
                    completed(
                        "ghcr.io/cirruslabs/"
                        "macos-tahoe-base@sha256:source\n"
                    ),
                    completed(),
                    completed('{"OS":"darwin","State":"stopped"}'),
                    completed(),
                    completed('{"Display":"1920x1080","State":"stopped"}'),
                    completed(),
                    completed("192.0.2.20\n"),
                    completed(),
                    completed("ready\n"),
                    completed("provisioned\n"),
                    completed("hardened\n"),
                    completed(),
                ]
            )
            with self.assertRaisesRegex(
                macos_vm_tart.TartAdapterError,
                "password authentication remains enabled",
            ):
                macos_vm_tart.prepare(config, runner, install_missing=False)
            state = macos_vm_tart.load_state(config.state_path)
        self.assertFalse(state.complete)
        self.assertNotIn(
            ("tart", "rename", config.preparation_image, config.golden_image),
            [call[0].argv for call in runner.calls],
        )

    def test_failed_prepare_records_incomplete_state_and_does_not_promote(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir)
            config = self.config(root)
            runner = FakeRunner([
                completed("[]"), completed(),
                completed(
                    "ghcr.io/cirruslabs/"
                    "macos-tahoe-base@sha256:source\n"
                ),
                completed(), completed('{"OS":"darwin","State":"stopped"}'),
                completed(),
                completed('{"Display":"1920x1080","State":"stopped"}'),
                completed(), completed("192.0.2.20\n"),
                completed(returncode=1, stderr="bootstrap failed"),
            ])
            with self.assertRaises(macos_vm_tart.CommandError):
                macos_vm_tart.prepare(config, runner, install_missing=False)
            state = macos_vm_tart.load_state(config.state_path)
        self.assertFalse(state.complete)
        self.assertNotIn(("tart", "rename", config.preparation_image, config.golden_image),
                         [call[0].argv for call in runner.calls])

    def test_state_file_is_private_and_atomic_temp_is_removed(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            path = Path(temporary_dir) / "state.json"
            config = self.config(Path(temporary_dir))
            macos_vm_tart.write_state_atomic(
                path, macos_vm_tart.PreparationState.complete_for(config, "source")
            )
            mode = os.stat(path).st_mode & 0o777
            leftovers = list(path.parent.glob(f".{path.name}.*.tmp"))
        self.assertEqual(mode, 0o600)
        self.assertEqual(leftovers, [])


if __name__ == "__main__":
    unittest.main()
