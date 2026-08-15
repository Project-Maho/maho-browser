#!/usr/bin/env python3
"""Typed, injectable Tart doctor and golden-image preparation adapter."""

from __future__ import annotations

import hashlib
import json
import os
import platform
import plistlib
import shutil
import subprocess
import tempfile
from dataclasses import asdict, dataclass
from enum import Enum
from pathlib import Path
from typing import Mapping, Protocol, Sequence


_STATE_SCHEMA = 1
_OWNER = "maho-macos-vm-e2e"
_REQUIRED_TAP = "cirruslabs/cli"
_MINIMUM_MACOS = (26, 0)
_DEFAULT_TIMEOUT_SECONDS = 120
_FIXED_DISPLAY = "1920x1080"


class TartAdapterError(RuntimeError):
    """Base error for the Tart adapter."""


class CommandError(TartAdapterError):
    def __init__(self, command: "Command", result: "CommandResult") -> None:
        redacted_command, redacted_result = _redact_command_failure(command, result)
        super().__init__(
            f"command failed ({redacted_result.returncode}): "
            f"{redacted_command.argv!r}: {redacted_result.stderr.strip()}"
        )
        self.command = redacted_command
        self.result = redacted_result


class OwnershipError(TartAdapterError):
    """Raised before changing a VM not demonstrably owned by this adapter."""


@dataclass(frozen=True)
class Command:
    argv: tuple[str, ...]
    background: bool = False

    def __post_init__(self) -> None:
        if not self.argv or any(not isinstance(part, str) or not part for part in self.argv):
            raise ValueError("command argv must contain non-empty strings")


@dataclass(frozen=True)
class CommandResult:
    returncode: int
    stdout: str = ""
    stderr: str = ""


def _redact_command_failure(
    command: Command,
    result: CommandResult,
) -> tuple[Command, CommandResult]:
    argv = list(command.argv)
    secrets: list[str] = []
    if Path(argv[0]).name == "sshpass":
        for index, argument in enumerate(argv[:-1]):
            if argument in {"-p", "--password"}:
                secrets.append(argv[index + 1])
                argv[index + 1] = "<redacted>"
    stdout = result.stdout
    stderr = result.stderr
    for secret in secrets:
        if secret:
            stdout = stdout.replace(secret, "<redacted>")
            stderr = stderr.replace(secret, "<redacted>")
    return (
        Command(tuple(argv), background=command.background),
        CommandResult(result.returncode, stdout, stderr),
    )


class Runner(Protocol):
    def run(
        self,
        command: Command,
        *,
        input_text: str | None = None,
        timeout: float | None = None,
        env: Mapping[str, str] | None = None,
    ) -> CommandResult: ...


class SubprocessRunner:
    """Real side effects; callers must inject this explicitly."""

    def __init__(self) -> None:
        self._background_processes: list[subprocess.Popen[str]] = []

    def run(
        self,
        command: Command,
        *,
        input_text: str | None = None,
        timeout: float | None = None,
        env: Mapping[str, str] | None = None,
    ) -> CommandResult:
        if command.background:
            process = subprocess.Popen(
                command.argv,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                text=True,
                start_new_session=True,
                env=dict(env) if env is not None else None,
            )
            self._background_processes.append(process)
            return CommandResult(0)
        completed = subprocess.run(
            command.argv,
            input=input_text,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
            env=dict(env) if env is not None else None,
        )
        return CommandResult(completed.returncode, completed.stdout, completed.stderr)


@dataclass(frozen=True)
class TartConfig:
    base_image: str
    golden_image: str
    preparation_image: str
    state_path: Path
    private_key: Path
    public_key: Path
    minimum_free_bytes: int = 50 * 1024**3
    guest_user: str = "admin"
    bootstrap_password: str = "admin"

    def __post_init__(self) -> None:
        names = (self.base_image, self.golden_image, self.preparation_image)
        if any(not name or any(character.isspace() for character in name) for name in names):
            raise ValueError("image names must be non-empty and contain no whitespace")
        if self.golden_image == self.preparation_image:
            raise ValueError("golden and preparation images must differ")
        if self.minimum_free_bytes <= 0:
            raise ValueError("minimum_free_bytes must be positive")

    @property
    def fingerprint(self) -> str:
        data = "\0".join(
            (
                _OWNER,
                self.base_image,
                self.golden_image,
                self.preparation_image,
                str(self.private_key.resolve()),
                str(self.public_key.resolve()),
            )
        )
        return hashlib.sha256(data.encode("utf-8")).hexdigest()


@dataclass(frozen=True)
class DoctorCheck:
    name: str
    ok: bool
    detail: str


@dataclass(frozen=True)
class DoctorReport:
    checks: tuple[DoctorCheck, ...]

    @property
    def ok(self) -> bool:
        return all(check.ok for check in self.checks)

    def to_json(self) -> str:
        return json.dumps(
            {"ok": self.ok, "checks": [asdict(check) for check in self.checks]},
            sort_keys=True,
        )


@dataclass(frozen=True)
class PreparationState:
    schema: int
    owner: str
    config_fingerprint: str
    base_image: str
    golden_image: str
    preparation_image: str
    source_identity: str
    complete: bool

    @classmethod
    def incomplete_for(cls, config: TartConfig, source_identity: str) -> "PreparationState":
        return cls(
            _STATE_SCHEMA,
            _OWNER,
            config.fingerprint,
            config.base_image,
            config.golden_image,
            config.preparation_image,
            source_identity,
            False,
        )

    @classmethod
    def complete_for(cls, config: TartConfig, source_identity: str) -> "PreparationState":
        state = cls.incomplete_for(config, source_identity)
        return cls(**{**asdict(state), "complete": True})

    def owns(self, config: TartConfig) -> bool:
        return (
            self.schema == _STATE_SCHEMA
            and self.owner == _OWNER
            and self.config_fingerprint == config.fingerprint
            and self.base_image == config.base_image
            and self.golden_image == config.golden_image
            and self.preparation_image == config.preparation_image
        )


class PrepareDisposition(str, Enum):
    PREPARED = "prepared"
    ALREADY_PREPARED = "already_prepared"


@dataclass(frozen=True)
class PrepareResult:
    disposition: PrepareDisposition
    state: PreparationState


def _run_checked(
    runner: Runner,
    argv: Sequence[str],
    *,
    input_text: str | None = None,
    timeout: float = _DEFAULT_TIMEOUT_SECONDS,
    background: bool = False,
) -> CommandResult:
    command = Command(tuple(argv), background=background)
    result = runner.run(command, input_text=input_text, timeout=timeout, env=None)
    if result.returncode != 0:
        raise CommandError(command, result)
    return result


def _key_permissions(config: TartConfig) -> tuple[bool, str]:
    if not config.private_key.is_file() or not config.public_key.is_file():
        return False, "SSH keypair is missing"
    private_mode = config.private_key.stat().st_mode & 0o777
    public_mode = config.public_key.stat().st_mode & 0o777
    ok = private_mode == 0o600 and public_mode & 0o022 == 0
    return ok, f"private={private_mode:04o}, public={public_mode:04o}"


def _parse_images(output: str) -> dict[str, str]:
    try:
        payload = json.loads(output or "[]")
    except json.JSONDecodeError as error:
        raise TartAdapterError("tart list returned invalid JSON") from error
    if not isinstance(payload, list):
        raise TartAdapterError("tart list JSON must be an array")
    images: dict[str, str] = {}
    for entry in payload:
        if not isinstance(entry, dict):
            raise TartAdapterError("tart list contains an invalid image entry")
        name = entry.get("name", entry.get("Name"))
        state = entry.get("state", entry.get("State", "unknown"))
        if not isinstance(name, str):
            raise TartAdapterError("tart list contains an invalid image entry")
        images[name] = str(state)
    return images


def doctor(
    config: TartConfig,
    runner: Runner,
    *,
    macos_version: tuple[int, int] | None = None,
    architecture: str | None = None,
    free_bytes: int | None = None,
) -> DoctorReport:
    architecture = architecture or platform.machine()
    if macos_version is None:
        version = platform.mac_ver()[0].split(".")
        if version[0]:
            major = int(version[0])
            minor = int(version[1]) if len(version) > 1 else 0
            resolved_macos_version = (major, minor)
        else:
            resolved_macos_version = (0, 0)
    else:
        resolved_macos_version = macos_version
    free_bytes = free_bytes if free_bytes is not None else shutil.disk_usage(config.state_path.parent).free
    checks = [
        DoctorCheck("architecture", architecture == "arm64", architecture),
        DoctorCheck(
            "macos_version",
            resolved_macos_version >= _MINIMUM_MACOS,
            ".".join(map(str, resolved_macos_version)),
        ),
        DoctorCheck(
            "disk_headroom",
            free_bytes >= config.minimum_free_bytes,
            f"{free_bytes} bytes free; {config.minimum_free_bytes} required",
        ),
    ]
    if not all(check.ok for check in checks):
        return DoctorReport(tuple(checks))

    tart = runner.run(Command(("tart", "--version")), timeout=30, env=None)
    checks.append(DoctorCheck("tart", tart.returncode == 0, tart.stdout.strip() or tart.stderr.strip()))
    taps = runner.run(Command(("brew", "tap")), timeout=30, env=None)
    tap_names = set(taps.stdout.splitlines()) if taps.returncode == 0 else set()
    checks.append(DoctorCheck("homebrew_tap", _REQUIRED_TAP in tap_names, _REQUIRED_TAP))
    sshpass = runner.run(Command(("command", "-v", "sshpass")), timeout=30, env=None)
    checks.append(DoctorCheck("sshpass", sshpass.returncode == 0, sshpass.stdout.strip()))
    key_ok, key_detail = _key_permissions(config)
    checks.append(DoctorCheck("ssh_keypair", key_ok, key_detail))
    listed = runner.run(Command(("tart", "list", "--format", "json")), timeout=30, env=None)
    conflict_ok = False
    detail = listed.stderr.strip()
    if listed.returncode == 0:
        images = _parse_images(listed.stdout)
        state = images.get(config.golden_image)
        conflict_ok = state not in {"running", "suspended"}
        detail = state or "absent"
    checks.append(DoctorCheck("golden_not_running", conflict_ok, detail))
    return DoctorReport(tuple(checks))


def key_only_ssh_command(
    config: TartConfig, ip_address: str, remote_argv: Sequence[str]
) -> Command:
    if not remote_argv:
        raise ValueError("remote argv must not be empty")
    return Command(
        (
            "ssh",
            "-i",
            str(config.private_key),
            "-o",
            "BatchMode=yes",
            "-o",
            "PasswordAuthentication=no",
            "-o",
            "KbdInteractiveAuthentication=no",
            "-o",
            "IdentitiesOnly=yes",
            "-o",
            "StrictHostKeyChecking=accept-new",
            f"{config.guest_user}@{ip_address}",
            *remote_argv,
        )
    )


def load_state(path: Path) -> PreparationState:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
        return PreparationState(**payload)
    except (OSError, json.JSONDecodeError, TypeError) as error:
        raise TartAdapterError(f"invalid preparation state: {path}") from error


def _optional_state(path: Path) -> PreparationState | None:
    return load_state(path) if path.exists() else None


def write_state_atomic(path: Path, state: PreparationState) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(asdict(state), output, sort_keys=True, indent=2)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_name, path)
    finally:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass


def _install_tools(runner: Runner) -> None:
    _run_checked(runner, ("brew", "tap", _REQUIRED_TAP))
    _run_checked(runner, ("brew", "install", "cirruslabs/cli/tart"), timeout=600)
    _run_checked(runner, ("brew", "install", "hudochenkov/sshpass/sshpass"), timeout=600)


def _password_only_ssh_command(
    config: TartConfig,
    ip_address: str,
    remote_argv: Sequence[str],
) -> Command:
    if not remote_argv:
        raise ValueError("remote argv must not be empty")
    return Command(
        (
            "sshpass",
            "-p",
            config.bootstrap_password,
            "ssh",
            "-o",
            "StrictHostKeyChecking=accept-new",
            "-o",
            "PreferredAuthentications=password",
            "-o",
            "PasswordAuthentication=yes",
            "-o",
            "KbdInteractiveAuthentication=no",
            "-o",
            "PubkeyAuthentication=no",
            "-o",
            "NumberOfPasswordPrompts=1",
            f"{config.guest_user}@{ip_address}",
            *remote_argv,
        )
    )


def _bootstrap_command(config: TartConfig, ip_address: str) -> tuple[str, ...]:
    return _password_only_ssh_command(
        config,
        ip_address,
        (
            "umask 077; mkdir -p ~/.ssh; cat >> ~/.ssh/authorized_keys; "
            "chmod 600 ~/.ssh/authorized_keys",
        ),
    ).argv


def _provision_script() -> str:
    return """set -euo pipefail
echo MAHO_TART_PROVISION_V1
export PATH="/opt/homebrew/bin:$PATH"
sudo sh -c 'printf "PasswordAuthentication no\\nKbdInteractiveAuthentication no\\n" > /etc/ssh/sshd_config.d/99-maho-key-only.conf'
sudo launchctl kickstart -k system/com.openssh.sshd
sudo defaults write /Library/Preferences/com.apple.loginwindow autoLoginUser admin
sudo pmset -a sleep 0 displaysleep 0 disksleep 0
sudo defaults write /Library/Preferences/com.apple.screensaver loginWindowIdleTime 0
if ! command -v node >/dev/null || [ "$(node -p 'process.versions.node.split(`.`)[0]')" != 24 ]; then
  curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh | NONINTERACTIVE=1 /bin/bash
  /opt/homebrew/bin/brew install node@24
  /opt/homebrew/bin/brew link --overwrite node@24
fi
test "$(stat -f %Su /dev/console)" = admin
pgrep -x WindowServer >/dev/null
test "$(/opt/homebrew/bin/node -p 'process.versions.node.split(`.`)[0]')" = 24
echo provisioned
"""


def _verify_preparation_display(metadata: str) -> None:
    try:
        value = json.loads(metadata)
    except json.JSONDecodeError as error:
        raise TartAdapterError("preparation display metadata is invalid") from error
    if not isinstance(value, dict) or value.get("Display") != _FIXED_DISPLAY:
        raise TartAdapterError(
            f"preparation image display is not {_FIXED_DISPLAY}"
        )


def _source_identity(raw_metadata: str) -> str:
    raw_metadata = raw_metadata.strip()
    if not raw_metadata:
        raise TartAdapterError("base image metadata is empty")
    try:
        return json.dumps(json.loads(raw_metadata), sort_keys=True, separators=(",", ":"))
    except json.JSONDecodeError:
        return raw_metadata


def _matching_digest_identities(
    images: Mapping[str, str], base_image: str
) -> set[str]:
    repository = base_image.rsplit(":", 1)[0]
    prefix = f"{repository}@sha256:"
    return {name for name in images if name.startswith(prefix)}


def _pulled_source_identity(
    before: Mapping[str, str],
    after: Mapping[str, str],
    base_image: str,
) -> str | None:
    identities = _matching_digest_identities(after, base_image) - _matching_digest_identities(
        before, base_image
    )
    if len(identities) > 1:
        raise TartAdapterError("tart pull produced multiple new base image digests")
    return next(iter(identities), None)


def _cached_source_identity(
    runner: Runner,
    images: Mapping[str, str],
    base_image: str,
) -> str:
    resolved = _run_checked(runner, ("tart", "fqn", base_image), timeout=30).stdout.strip()
    if resolved not in _matching_digest_identities(images, base_image):
        raise TartAdapterError(
            "tart fqn did not resolve to a listed base image digest"
        )
    return resolved


def _resolved_source_identity(
    runner: Runner,
    base_image: str,
) -> str:
    resolved = _run_checked(
        runner, ("tart", "fqn", base_image), timeout=30
    ).stdout.strip()
    if resolved not in _matching_digest_identities(
        {resolved: "oci"},
        base_image,
    ):
        raise TartAdapterError(
            "tart fqn did not resolve the base image to a digest"
        )
    return resolved


def _local_clone_source_identity(
    base_image: str,
    resolved: str,
    local_metadata: str,
) -> str:
    try:
        metadata = json.loads(local_metadata)
    except json.JSONDecodeError as error:
        raise TartAdapterError("local preparation image metadata is invalid") from error
    if not isinstance(metadata, dict) or not metadata:
        raise TartAdapterError("local preparation image metadata is empty")
    return json.dumps(
        {
            "base_image": base_image,
            "resolved_reference": resolved,
            "local_metadata": metadata,
        },
        sort_keys=True,
        separators=(",", ":"),
    )


def prepare(
    config: TartConfig,
    runner: Runner,
    *,
    install_missing: bool,
) -> PrepareResult:
    if install_missing:
        _install_tools(runner)

    listed = _run_checked(runner, ("tart", "list", "--format", "json"), timeout=30)
    images = _parse_images(listed.stdout)
    state = _optional_state(config.state_path)
    relevant_present = config.golden_image in images or config.preparation_image in images
    if relevant_present and (state is None or not state.owns(config)):
        raise OwnershipError("refusing to change a golden/preparation image without matching ownership state")
    if config.golden_image in images:
        if images[config.golden_image] in {"running", "suspended"}:
            raise OwnershipError("golden image is running")
        if state is not None and state.owns(config) and state.complete:
            return PrepareResult(PrepareDisposition.ALREADY_PREPARED, state)
        raise OwnershipError("golden image exists without complete preparation state")
    if config.preparation_image in images:
        raise OwnershipError("incomplete preparation image requires explicit operator recovery")

    _run_checked(runner, ("tart", "pull", config.base_image), timeout=1800)
    resolved_source = _resolved_source_identity(runner, config.base_image)
    source_identity = resolved_source
    incomplete = PreparationState.incomplete_for(config, source_identity)
    write_state_atomic(config.state_path, incomplete)
    try:
        _run_checked(runner, ("tart", "clone", config.base_image, config.preparation_image), timeout=1800)
        local_metadata = _run_checked(
            runner,
            ("tart", "get", config.preparation_image, "--format", "json"),
            timeout=30,
        )
        source_identity = _local_clone_source_identity(
            config.base_image,
            resolved_source,
            local_metadata.stdout,
        )
        incomplete = PreparationState.incomplete_for(config, source_identity)
        write_state_atomic(config.state_path, incomplete)
        _run_checked(
            runner,
            ("tart", "set", config.preparation_image, "--display", _FIXED_DISPLAY),
            timeout=60,
        )
        display_metadata = _run_checked(
            runner,
            ("tart", "get", config.preparation_image, "--format", "json"),
            timeout=30,
        )
        _verify_preparation_display(display_metadata.stdout)
        _run_checked(
            runner,
            ("tart", "run", "--no-graphics", config.preparation_image),
            background=True,
        )
        ip_address = _run_checked(
            runner,
            ("tart", "ip", config.preparation_image, "--wait", "600"),
            timeout=600,
        ).stdout.strip()
        if not ip_address:
            raise TartAdapterError("tart ip --wait returned an empty address")
        public_key = config.public_key.read_text(encoding="utf-8")
        _run_checked(runner, _bootstrap_command(config, ip_address), input_text=public_key)
        key_check = key_only_ssh_command(config, ip_address, ("printf", "ready"))
        result = runner.run(key_check, timeout=60, env=None)
        if result.returncode != 0:
            raise CommandError(key_check, result)
        provision = key_only_ssh_command(config, ip_address, ("bash", "-s"))
        result = runner.run(provision, input_text=_provision_script(), timeout=1800, env=None)
        if result.returncode != 0:
            raise CommandError(provision, result)
        hardened_key_check = key_only_ssh_command(
            config, ip_address, ("printf", "hardened")
        )
        result = runner.run(hardened_key_check, timeout=60, env=None)
        if result.returncode != 0:
            raise CommandError(hardened_key_check, result)
        password_check = _password_only_ssh_command(
            config, ip_address, ("true",)
        )
        result = runner.run(password_check, timeout=60, env=None)
        if result.returncode == 0:
            raise TartAdapterError(
                "password authentication remains enabled after SSH hardening"
            )
        _run_checked(runner, ("tart", "stop", config.preparation_image), timeout=300)
        _run_checked(
            runner, ("tart", "rename", config.preparation_image, config.golden_image), timeout=300
        )
    except Exception:
        write_state_atomic(config.state_path, incomplete)
        raise

    complete = PreparationState.complete_for(config, source_identity)
    write_state_atomic(config.state_path, complete)
    return PrepareResult(PrepareDisposition.PREPARED, complete)
