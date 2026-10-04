#!/usr/bin/env python3
from __future__ import annotations

import argparse
import glob
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from typing import Sequence
from xml.sax.saxutils import escape


_SCRIPT_DIR = Path(__file__).resolve().parent
_OVERLAY_ROOT = _SCRIPT_DIR.parents[1]
_WORKSPACE_ROOT = _OVERLAY_ROOT.parent
_DEFAULT_RELEASE_FILE = (
    _WORKSPACE_ROOT / "chromium/src/chrome/installer/mini_installer/chrome.release"
)
_DEFAULT_VERSION_FILE = _WORKSPACE_ROOT / "maho/version.txt"
_MANIFEST_TEMPLATE = _OVERLAY_ROOT / "build/windows_msix/AppxManifest.xml.in"
_BRANDING_DIR = _OVERLAY_ROOT / "branding"
_DEFAULT_PUBLISHER = "CN=68073D7F-44F8-47BF-8B3E-B17FBDC44F36"
_DEFAULT_PACKAGE_NAME = "ProjectMaho.MahoBrowser"


def is_windows_host() -> bool:
    return os.name == "nt"


def normalized_msix_version(version: str) -> str:
    parts = version.split(".")
    if not 1 <= len(parts) <= 4 or any(not part.isdecimal() for part in parts):
        raise ValueError(f"Invalid MSIX version: {version!r}")
    values = [int(part) for part in parts]
    if any(value > 65535 for value in values):
        raise ValueError(f"MSIX version component exceeds 65535: {version!r}")
    return ".".join(str(value) for value in (*values, *([0] * (4 - len(values)))))


def parse_release_entries(release_file: Path) -> list[tuple[str, str, bool]]:
    entries: list[tuple[str, str, bool]] = []
    active_section: str | None = None
    for raw_line in release_file.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if line.startswith("[") and line.endswith("]"):
            active_section = line[1:-1]
            continue
        if not active_section or not line or line.startswith("#") or ":" not in line:
            continue
        source, destination = (item.strip() for item in line.split(":", 1))
        if source and destination.startswith(("%(ChromeDir)s", "%(VersionDir)s")):
            entries.append((source, destination, active_section == "GENERAL"))
    if not entries:
        raise RuntimeError(f"No MSIX runtime package entries in {release_file}")
    return entries


def _destination_directory(destination: str) -> Path:
    if destination.startswith("%(ChromeDir)s"):
        suffix = destination.removeprefix("%(ChromeDir)s")
    elif destination.startswith("%(VersionDir)s"):
        suffix = destination.removeprefix("%(VersionDir)s")
    else:
        raise ValueError(f"Unsupported MSIX release destination: {destination!r}")
    suffix = suffix.lstrip("\\/")
    return Path(*[part for part in suffix.replace("\\", "/").split("/") if part])


def stage_runtime_files(
    input_dir: Path,
    release_file: Path,
    staging_dir: Path,
) -> list[Path]:
    copied: list[Path] = []
    for source_pattern, destination, required in parse_release_entries(release_file):
        pattern = source_pattern.replace("\\", os.sep).replace("/", os.sep)
        matches = sorted(glob.glob(str(input_dir / pattern), recursive=True))
        if not matches:
            # chrome_child.dll and optimization_guide_internal.dll are optional based on build config
            if required and source_pattern not in ("chrome_child.dll", "optimization_guide_internal.dll"):
                raise FileNotFoundError(
                    f"Required MSIX payload entry {source_pattern!r} is absent from "
                    f"{input_dir}"
                )
            continue
        target_dir = staging_dir / _destination_directory(destination)
        for match in matches:
            source = Path(match)
            if source.is_dir():
                continue
            target = target_dir / source.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            copied.append(target)

    maho_executable = staging_dir / "maho.exe"
    if not maho_executable.is_file():
        raise FileNotFoundError(
            "maho.exe was not staged at the MSIX package root. "
            "Check chrome.release and the Windows build output."
        )
    mail_helper = staging_dir / "maho_mail_helper.exe"
    if not mail_helper.is_file():
        raise FileNotFoundError(
            "maho_mail_helper.exe was not staged at the MSIX package root. "
            "Check chrome.release and the Windows build output."
        )
    chrome_executable = staging_dir / "chrome.exe"
    if not chrome_executable.is_file():
        shutil.copy2(maho_executable, chrome_executable)
        copied.append(chrome_executable)
    if not (staging_dir / "chrome.dll").is_file():
        raise FileNotFoundError(
            "The required Chromium runtime payload was not staged. "
            "Check chrome.release and the Windows build output."
        )
    return copied


def stage_visual_assets(staging_dir: Path) -> None:
    assets = staging_dir / "Assets"
    assets.mkdir(parents=True, exist_ok=True)
    logos = {
        "Square44x44Logo.png": (_BRANDING_DIR / "product_logo_48.png", (44, 44)),
        "Square150x150Logo.png": (
            _BRANDING_DIR / "product_logo_256.png",
            (150, 150),
        ),
        "Wide310x150Logo.png": (_BRANDING_DIR / "product_logo_256.png", (310, 150)),
        "StoreLogo.png": (_BRANDING_DIR / "product_logo_256.png", (50, 50)),
    }
    try:
        from PIL import Image
    except ImportError as error:
        if not is_windows_host():
            raise RuntimeError(
                "Pillow is required to resize MSIX visual assets. "
                "Install it with: python -m pip install Pillow"
            ) from error
        powershell = shutil.which("powershell") or shutil.which("pwsh")
        if not powershell:
            raise FileNotFoundError(
                "PowerShell is required to generate MSIX visual assets on Windows"
            )
        for target_name, (source, size) in logos.items():
            if not source.is_file():
                raise FileNotFoundError(
                    f"Required Maho branding asset is absent: {source}"
                )
            script = "\n".join(
                (
                    "Add-Type -AssemblyName System.Drawing",
                    f"$source = [System.Drawing.Image]::FromFile('{source}')",
                    f"$bitmap = New-Object System.Drawing.Bitmap({size[0]}, {size[1]})",
                    "$graphics = [System.Drawing.Graphics]::FromImage($bitmap)",
                    "$graphics.InterpolationMode = "
                    "[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic",
                    "$scale = [Math]::Min($bitmap.Width / $source.Width, "
                    "$bitmap.Height / $source.Height)",
                    "$width = [int][Math]::Round($source.Width * $scale)",
                    "$height = [int][Math]::Round($source.Height * $scale)",
                    "$left = [int](($bitmap.Width - $width) / 2)",
                    "$top = [int](($bitmap.Height - $height) / 2)",
                    "$graphics.DrawImage($source, $left, $top, $width, $height)",
                    "$bitmap.Save("
                    f"'{assets / target_name}', "
                    "[System.Drawing.Imaging.ImageFormat]::Png)",
                    "$graphics.Dispose()",
                    "$bitmap.Dispose()",
                    "$source.Dispose()",
                )
            )
            run(
                (powershell, "-NoProfile", "-NonInteractive", "-Command", script),
                f"PowerShell: generate {target_name}",
            )
        return

    for target_name, (source, size) in logos.items():
        if not source.is_file():
            raise FileNotFoundError(f"Required Maho branding asset is absent: {source}")
        with Image.open(source).convert("RGBA") as image:
            image.thumbnail(size, Image.Resampling.LANCZOS)
            canvas = Image.new("RGBA", size)
            offset = ((size[0] - image.width) // 2, (size[1] - image.height) // 2)
            canvas.alpha_composite(image, offset)
            canvas.save(assets / target_name, "PNG")


def write_manifest(
    staging_dir: Path,
    *,
    package_name: str,
    publisher: str,
    version: str,
    architecture: str,
) -> Path:
    template = _MANIFEST_TEMPLATE.read_text(encoding="utf-8")
    replacements = {
        "@PACKAGE_NAME@": escape(package_name),
        "@PUBLISHER@": escape(publisher),
        "@VERSION@": normalized_msix_version(version),
        "@ARCHITECTURE@": escape(architecture),
    }
    for token, value in replacements.items():
        template = template.replace(token, value)
    manifest = staging_dir / "AppxManifest.xml"
    manifest.write_text(template, encoding="utf-8")
    return manifest


def find_windows_sdk_tool(name: str) -> str:
    tool = shutil.which(name)
    if tool:
        return tool
    if os.name == "nt":
        kits_bin = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        kits_bin /= "Windows Kits/10/bin"
        candidates = sorted(
            kits_bin.glob(f"*/x64/{name}"),
            reverse=True,
        )
        if candidates:
            return str(candidates[0])
    raise FileNotFoundError(
        f"{name} was not found on PATH. Install the Windows SDK and add its "
        "x64 bin directory to PATH."
    )


def run(command: Sequence[str], display_command: str | None = None) -> None:
    print("+", display_command or subprocess.list2cmdline(list(command)))
    subprocess.run(command, check=True)


def create_development_certificate(
    *,
    publisher: str,
    pfx_path: Path,
    password: str,
) -> Path:
    powershell = shutil.which("powershell") or shutil.which("pwsh")
    if not powershell:
        raise FileNotFoundError("PowerShell is required to create a development certificate")
    certificate_path = pfx_path.with_suffix(".cer")
    for path in (pfx_path, certificate_path):
        path.unlink(missing_ok=True)

    def quote(value: str) -> str:
        return value.replace("'", "''")

    command = "\n".join(
        (
            "$ErrorActionPreference = 'Stop'",
            "$cert = New-SelfSignedCertificate "
            f"-Type Custom -Subject '{quote(publisher)}' "
            "-CertStoreLocation 'Cert:\\CurrentUser\\My' "
            "-KeyUsage DigitalSignature -KeyExportPolicy Exportable "
            "-HashAlgorithm SHA256 -FriendlyName 'Maho MSIX Development'",
            "$password = ConvertTo-SecureString "
            f"'{quote(password)}' -AsPlainText -Force",
            "Export-PfxCertificate -Cert $cert "
            f"-FilePath '{quote(str(pfx_path))}' -Password $password | Out-Null",
            "Export-Certificate -Cert $cert "
            f"-FilePath '{quote(str(certificate_path))}' | Out-Null",
            "Import-Certificate -FilePath "
            f"'{quote(str(certificate_path))}' "
            "-CertStoreLocation 'Cert:\\CurrentUser\\TrustedPeople' | Out-Null",
            )
    )
    run(
        (powershell, "-NoProfile", "-NonInteractive", "-Command", command),
        "PowerShell: create and trust Maho MSIX development certificate",
    )
    return certificate_path


def pack_sign_and_install(
    *,
    staging_dir: Path,
    output: Path,
    certificate: Path | None,
    certificate_password: str | None,
    install: bool,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    run((find_windows_sdk_tool("makeappx.exe"), "pack", "/d", str(staging_dir), "/p", str(output), "/o"))
    if certificate:
        if not certificate_password:
            raise ValueError("--certificate-password is required when signing an MSIX")
        signtool = find_windows_sdk_tool("signtool.exe")
        run(
            (
                signtool,
                "sign",
                "/fd",
                "SHA256",
                "/f",
                str(certificate),
                "/p",
                certificate_password,
                str(output),
            ),
            f"{signtool} sign /fd SHA256 /f {certificate} /p [redacted] {output}",
        )
    elif install:
        raise ValueError("A signed MSIX is required for --install")

    if install:
        powershell = shutil.which("powershell") or shutil.which("pwsh")
        if not powershell:
            raise FileNotFoundError("PowerShell is required for --install")
        escaped_output = str(output).replace("'", "''")
        run(
            (
                powershell,
                "-NoProfile",
                "-NonInteractive",
                "-Command",
                f"Add-AppxPackage -ForceApplicationShutdown -Path '{escaped_output}'",
            )
        )


def verify_installed_package(
    *,
    package_name: str,
    expected_executable: str,
    launch: bool,
) -> None:
    powershell = shutil.which("powershell") or shutil.which("pwsh")
    if not powershell:
        raise FileNotFoundError("PowerShell is required for --verify-installed")
    verification_lines = [
            "$ErrorActionPreference = 'Stop'",
            f"$package = Get-AppxPackage -Name '{package_name}'",
            "if (-not $package) { throw 'Maho MSIX is not registered' }",
            "$manifest = [xml](Get-Content "
            "-LiteralPath (Join-Path $package.InstallLocation 'AppxManifest.xml') "
            "-Raw)",
            "$application = $manifest.Package.Applications.Application",
            f"if ($application.Executable -ne '{expected_executable}') "
            "{ throw \"Unexpected package executable: $($application.Executable)\" }",
            "if (-not (Test-Path (Join-Path $package.InstallLocation "
            f"'{expected_executable}'))) "
            "{ throw 'Package executable is missing' }",
            "if (-not (Test-Path (Join-Path $package.InstallLocation 'chrome.dll'))) "
            "{ throw 'Package chrome.dll is missing' }",
            "if (-not (Test-Path (Join-Path $package.InstallLocation "
            "'maho_mail_helper.exe'))) "
            "{ throw 'Package Mail helper is missing' }",
            "$app_id = \"$($package.PackageFamilyName)!$($application.Id)\"",
    ]
    if launch:
        verification_lines.extend(
            (
                "$subscription = Register-WmiEvent "
                "-Class Win32_ProcessStartTrace "
                "-SourceIdentifier MahoChromeStarted",
                "try {",
                "  Start-Process explorer.exe \"shell:AppsFolder\\$app_id\"",
                "  $deadline = (Get-Date).AddSeconds(20)",
                "  do {",
                "    $remaining = [Math]::Max(0, "
                "[int][Math]::Ceiling(($deadline - (Get-Date)).TotalSeconds))",
                "    $event = Wait-Event -SourceIdentifier MahoChromeStarted "
                "-Timeout $remaining",
                "    if (-not $event -or "
                "$event.SourceEventArgs.NewEvent.ProcessName -ine 'chrome.exe') { "
                "continue }",
                "    $process = Get-CimInstance Win32_Process "
                "-Filter \"ProcessId = $($event.SourceEventArgs.NewEvent.ProcessID)\"",
                "    if ($process -and $process.ExecutablePath -eq "
                "(Join-Path $package.InstallLocation 'chrome.exe')) { "
                "break }",
                "  } while ((Get-Date) -lt $deadline)",
                "  if (-not $process -or $process.ExecutablePath -ne "
                "(Join-Path $package.InstallLocation 'chrome.exe')) { "
                "throw 'Packaged Maho did not start chrome.exe' }",
                "} finally {",
                "  Remove-Event -SourceIdentifier MahoChromeStarted "
                "-ErrorAction SilentlyContinue",
                "  Unregister-Event -SourceIdentifier MahoChromeStarted "
                "-ErrorAction SilentlyContinue",
                "}",
            )
        )
    verification_lines.append(
        "[PSCustomObject]@{ "
            "PackageFullName = $package.PackageFullName; "
            "PackageFamilyName = $package.PackageFamilyName; "
            "InstallLocation = $package.InstallLocation; "
            "AppId = $app_id; "
            "Executable = $application.Executable "
            "} | ConvertTo-Json -Compress"
    )
    command = "\n".join(verification_lines)
    run(
        (powershell, "-NoProfile", "-NonInteractive", "-Command", command),
        "PowerShell: verify installed Maho MSIX package",
    )


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create a locally installable Maho MSIX from a Windows build output."
    )
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--version",
        default=_DEFAULT_VERSION_FILE.read_text(encoding="utf-8").strip(),
    )
    parser.add_argument("--architecture", choices=("x64", "arm64"), default="x64")
    parser.add_argument("--package-name", default=_DEFAULT_PACKAGE_NAME)
    parser.add_argument("--publisher", default=_DEFAULT_PUBLISHER)
    parser.add_argument("--release-file", type=Path, default=_DEFAULT_RELEASE_FILE)
    parser.add_argument("--certificate", type=Path)
    parser.add_argument("--certificate-password")
    parser.add_argument("--generate-dev-certificate", action="store_true")
    parser.add_argument(
        "--dev-certificate-path",
        type=Path,
        help="PFX path to create when --generate-dev-certificate is selected.",
    )
    parser.add_argument("--install", action="store_true")
    parser.add_argument("--verify-installed", action="store_true")
    parser.add_argument("--launch", action="store_true")
    parser.add_argument("--keep-stage-dir", type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.certificate and args.generate_dev_certificate:
        raise ValueError("--certificate and --generate-dev-certificate are mutually exclusive")
    if args.generate_dev_certificate and not args.certificate_password:
        raise ValueError(
            "--certificate-password is required with --generate-dev-certificate"
        )
    if args.launch and not args.verify_installed:
        raise ValueError("--launch requires --verify-installed")
    if args.install and not (args.certificate or args.generate_dev_certificate):
        raise ValueError("--install requires --certificate or --generate-dev-certificate")

    with tempfile.TemporaryDirectory(prefix="maho-msix-") as temporary_dir:
        staging_dir = Path(temporary_dir) / "package"
        staging_dir.mkdir()
        stage_runtime_files(args.input_dir, args.release_file, staging_dir)
        stage_visual_assets(staging_dir)
        write_manifest(
            staging_dir,
            package_name=args.package_name,
            publisher=args.publisher,
            version=args.version,
            architecture=args.architecture,
        )

        certificate = args.certificate
        if args.generate_dev_certificate:
            certificate = args.dev_certificate_path or args.output.with_suffix(".pfx")
            create_development_certificate(
                publisher=args.publisher,
                pfx_path=certificate,
                password=args.certificate_password,
            )
        pack_sign_and_install(
            staging_dir=staging_dir,
            output=args.output,
            certificate=certificate,
            certificate_password=args.certificate_password,
            install=args.install,
        )
        if args.verify_installed:
            verify_installed_package(
                package_name=args.package_name,
                expected_executable="chrome.exe",
                launch=args.launch,
            )
        if args.keep_stage_dir:
            args.keep_stage_dir.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(staging_dir, args.keep_stage_dir, dirs_exist_ok=True)

    print(f"Created {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (FileNotFoundError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
