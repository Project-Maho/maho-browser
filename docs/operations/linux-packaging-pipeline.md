# Linux Packaging Pipeline

## Overview

The Linux packaging pipeline produces installable Maho Browser packages for major Linux distributions. It wraps the canonical `build_maho.py` Chromium build workflow with format-specific post-build steps to generate `.tar.gz`, `.deb`, and `.rpm` artifacts.

**When to run:**
- Before distro verification on Ubuntu/Fedora/Arch test machines
- When preparing release artifacts for Linux distribution channels
- After changes to `maho-chromium/build/config/args_linux.gn` or `maho-chromium/build/args.gni`

**Script location:** `maho-chromium/build/scripts/build_maho_linux_packages.py`

---

## Prerequisites

### Host Requirements

- **Linux host required** — the script will refuse to run on macOS/Windows (Chromium Linux builds need a Linux sysroot and toolchain)
- For non-Linux dev machines, use Docker:
  ```bash
  docker run --rm -v $(pwd):/workspace -w /workspace ubuntu:22.04 \
    python3 maho-chromium/build/scripts/build_maho_linux_packages.py --format tarball
  ```

### Per-Format Tools

| Format | Required Tools | Install (Ubuntu) | Install (Fedora) |
|--------|---------------|------------------|------------------|
| tarball | `tar` | pre-installed | pre-installed |
| deb | `dpkg-deb`, `fakeroot` | `apt install dpkg-dev fakeroot` | N/A (use Ubuntu) |
| rpm | `rpmbuild` | `apt install rpm` | `dnf install rpm-build` |
| flatpak | `flatpak-builder` | — STUB, not yet usable — | — |
| appimage | `appimagetool` | — STUB, not yet usable — | — |

### Other Prerequisites

- Python 3.8+
- Chromium source tree at `chromium/` (gclient-managed)
- Symlink `chromium/src/maho` → `../../maho-chromium` (auto-created by build_maho.py)
- NDK is **irrelevant** for desktop Linux builds

---

## Step-by-Step

### 1. Verify args_linux.gn is unchanged

```bash
git diff --exit-code maho-chromium/build/config/args_linux.gn
```

The packaging script reads this file but never modifies it. If there are local changes, investigate before proceeding.

### 2. Check prerequisites

```bash
python3 maho-chromium/build/scripts/build_maho_linux_packages.py --check --format tarball
python3 maho-chromium/build/scripts/build_maho_linux_packages.py --check --format deb
python3 maho-chromium/build/scripts/build_maho_linux_packages.py --check --format rpm
```

### 3. Build packages

```bash
# Single format
python3 maho-chromium/build/scripts/build_maho_linux_packages.py --format tarball

# All buildable formats (tarball + deb + rpm)
python3 maho-chromium/build/scripts/build_maho_linux_packages.py --format all

# Skip Rust rebuild (if maho-core prebuilt is already current)
python3 maho-chromium/build/scripts/build_maho_linux_packages.py --format deb --skip-rust

# Only packaging step (skip Chromium build, use existing output)
python3 maho-chromium/build/scripts/build_maho_linux_packages.py --format tarball --skip-chromium

# Dry run (print commands without executing)
python3 maho-chromium/build/scripts/build_maho_linux_packages.py --format all --dry-run
```

### 4. Find artifacts

Artifacts are placed in per-format output directories under `chromium/src/`:

| Format | Output Directory | Artifact |
|--------|-----------------|----------|
| tarball | `out/Linux-tarball/` | `maho-<version>-x86_64.tar.gz` |
| deb | `out/Linux-deb/` | `maho_<version>_amd64.deb` |
| rpm | `out/Linux-rpm/rpmbuild/RPMS/x86_64/` | `maho-<version>-1.x86_64.rpm` |

---

## Per-Format Details

### Tarball (.tar.gz)

- Simplest format — just `tar -czf` the built binary
- No system integration (no desktop file, no icons)
- Update mechanism: `maho_update_delegate_linux_noop.cc` — returns `kUpToDate` (user manages updates manually)
- File naming: `maho-<version>-x86_64.tar.gz`

### Deb (.deb)

- Generated `DEBIAN/control` file with minimal metadata
- **Review the control file before publishing** — placeholders for Maintainer and Homepage
- Depends: `libgtk-3-0, libdbus-1-3, libnss3, libatk1.0-0` (verify on target distro)
- Update mechanism: `maho_update_delegate_linux_pkg.cc` — displays signed-envelope banner prompting user to `apt upgrade`
- File naming: `maho_<version>_amd64.deb`

### RPM (.rpm)

- Generated `.spec` file with minimal metadata
- **Review the spec file before publishing** — placeholders for Packager and URL
- Requires: `gtk3, dbus-libs, nss` (verify on target distro)
- Update mechanism: `maho_update_delegate_linux_pkg.cc` — same signed-envelope banner as deb
- File naming: `maho-<version>-1.x86_64.rpm`

### Flatpak — STUB

- **Status:** NOT IMPLEMENTED
- C++ delegate: `maho-chromium/browser/updates/maho_update_delegate_linux_flatpak.cc` — returns `kUpToDate` unconditionally
- Blocked on: libflatpak integration in the update delegate
- Track in: P0-4 follow-up

### AppImage — STUB

- **Status:** NOT IMPLEMENTED
- C++ delegate: `maho-chromium/browser/updates/maho_update_delegate_linux_appimage.cc` — has TODO comment for libappimageupdate
- Blocked on: libappimageupdate integration in the update delegate
- Track in: P0-4 follow-up

---

## Distro Test Matrix

### Ubuntu 22.04 LTS

```bash
# Install build prerequisites
sudo apt update
sudo apt install -y dpkg-dev fakeroot rpm

# Install the .deb package
sudo apt install ./maho_1.0.0_amd64.deb

# Verify
maho --version
# Launch and test: tabs, AI Chat, sync, ad blocking

# Remove
sudo apt remove maho
```

### Fedora 39

```bash
# Install build prerequisites
sudo dnf install -y rpm-build

# Install the .rpm package
sudo dnf install ./maho-1.0.0-1.x86_64.rpm

# Verify
maho --version
# Launch and test: tabs, AI Chat, sync, ad blocking

# Remove
sudo dnf remove maho
```

### Arch Linux

```bash
# Use the tarball (AUR package is a future item)
tar -xzf maho-1.0.0-x86_64.tar.gz
./maho --version
# Launch and test: tabs, AI Chat, sync, ad blocking
```

---

## Phoronix Submission Guidance

To submit Maho for Phoronix review:

1. **Contact:** https://www.phoronix.com/contact — editorial inquiries
2. **Include:**
   - Brief product description (AI-native Chromium-based browser)
   - Download links for .deb, .rpm, and tarball
   - Key differentiators from Chrome/Firefox (AI features, privacy, no promotional pricing)
   - System requirements (x86_64, GTK3, 4GB+ RAM recommended)
3. **Timing:** Submit after AC1 (Ubuntu + Fedora + Arch verification passing)
4. **Follow-up:** Monitor https://www.phoronix.com for coverage, respond to community questions on r/linux and HN

---

## Known Limitations

1. **Flatpak and AppImage delegates are stubs** — packages will build the Chromium binary correctly (the GN flag routes to the appropriate delegate), but the in-app update banner will always report "up to date" for these formats until the corresponding `.cc` delegate files are fully implemented:
   - `maho-chromium/browser/updates/maho_update_delegate_linux_flatpak.cc`
   - `maho-chromium/browser/updates/maho_update_delegate_linux_appimage.cc`

2. **Packaging script is build-only** — it does not handle signing, repository setup, or distribution. Those are separate concerns for the release pipeline.

3. **Generated control/spec files are bootstraps** — they contain placeholder metadata. A human must review and fill in correct values before publishing packages to any repository.

4. **x86_64 only** — `args_linux.gn` sets `target_cpu = "x64"`. ARM64 Linux is not currently configured.

5. **Linux test machine required** — the script cannot run on macOS/Windows. CI/CD or a dedicated Linux machine is needed for distro verification (P0-4 items A4.1-A4.3).
