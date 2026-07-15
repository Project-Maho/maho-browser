# CI Infrastructure — Cross-platform Chromium Build

**Last updated:** 2026-07-10
**Companion documents:**
- `maho-chromium/docs/cross-platform-build.md` — build system reference
- `.omo/plans/cross-platform-completion-plan-2026-07-10.md` — active completion plan
- `docs/operations/linux-packaging-pipeline.md` — Linux packaging runbook

---

## Executive Summary

Chromium fork builds require heavy CI hardware (32+ cores, 64GB+ RAM, ~120GB Chromium source + build outputs). Hosted CI services (GitHub Actions, BuildJet, Depot) cost $200–$1,800/month at the volumes we need. **Self-hosted on existing hardware costs $0–$150/month.**

This document describes the target hardware layout, runner registration, cache infrastructure, and workflow integration.

---

## Target Hardware Layout

The design assumes the following physical hardware is already available:

| Machine | Role | Hosts |
|---|---|---|
| Apple Silicon Mac (M-series, 24GB+ RAM recommended) | Primary macOS builder | macOS arm64 native runner + Linux arm64 VM (Lima) |
| Windows PC (x86_64, 32GB+ RAM recommended) | Primary Windows builder | Windows x64 native runner + Linux x64 VM (Hyper-V) OR shared with a separate spare Linux box |

This layout covers **four build targets** with self-hosted infrastructure:

1. **macOS arm64** — native on Mac
2. **Windows x64** — native on PC
3. **Linux x64** — Hyper-V VM on Windows PC (or dedicated spare box)
4. **Linux arm64** — Lima VM on Mac

Additional platforms (macOS x64, Windows arm64, Linux arm64 packages) are deferred; see `cross-platform-build.md` "Supported target triples".

---

## Runner Labels

All runners register with GitHub Actions using labels that identify platform and architecture. Workflow matrix jobs select by label.

| Label set | Selects |
|---|---|
| `self-hosted, chromium-builder, macos, arm64` | Mac native |
| `self-hosted, chromium-builder, linux, arm64` | Lima VM on Mac |
| `self-hosted, chromium-builder, windows, x64` | Windows native |
| `self-hosted, chromium-builder, linux, x64` | Linux VM or spare box |

Register a new runner with:

```bash
./config.sh \
  --url https://github.com/YOUR-ORG/maho-workspace \
  --token YOUR_TOKEN \
  --labels self-hosted,chromium-builder,linux,x64 \
  --name maho-linux-x64-builder
```

Install as a background service:

```bash
sudo ./svc.sh install
sudo ./svc.sh start
```

---

## macOS Native Runner Setup

**Host:** Apple Silicon Mac (M-series, 24GB+ RAM strongly recommended).

**Chromium build performance:**
- Mac mini M4 (10-core, 24GB): ~90 min clean, ~10 min incremental
- Mac Studio M2 Max (12-core, 32GB): ~60 min clean, ~5 min incremental

**Setup steps:**

1. Install Xcode Command Line Tools: `xcode-select --install`
2. Install `depot_tools` and add to PATH.
3. Install Rust toolchain: `rustup default stable && rustup target add aarch64-apple-darwin`
4. Clone workspace and run initial gclient sync (keeps `chromium/` warm — ~121GB, 250GB+ free space required).
5. Register self-hosted runner with labels `self-hosted,chromium-builder,macos,arm64`.
6. Verify: run `python3 maho-chromium/build/scripts/build_maho.py` once manually to confirm the build passes.

**Storage:** Keep `chromium/` on the same volume as the runner. Re-fetching Chromium source takes 30+ minutes; keeping it warm reduces this to a fast `gclient sync`.

---

## Linux arm64 via Lima on Mac

**Rationale:** Apple Silicon Macs execute arm64 Linux at native speed via `Hypervisor.framework`. Perfect for Linux arm64 CI without buying additional hardware.

**Chromium build performance:** ~100 min clean, ~12 min incremental (5–10% overhead vs. bare metal).

**Setup steps:**

1. Install Lima:
   ```bash
   brew install lima
   ```

2. Create VM config `~/chromium-linux-arm64.yaml`:
   ```yaml
   images:
   - location: "https://cloud-images.ubuntu.com/releases/24.04/release/ubuntu-24.04-server-cloudimg-arm64.img"
     arch: "aarch64"
   cpus: 6           # of 10 on Mac mini M4; adjust for host
   memory: "24GiB"   # of 32GiB; leave 8GiB for macOS host
   disk: "250GiB"    # Chromium source + build outputs
   mounts: []        # Do NOT mount macOS filesystem into VM (see caveat below)
   ```

3. Start VM:
   ```bash
   limactl start --name=chromium-linux ~/chromium-linux-arm64.yaml
   ```

4. Inside the VM:
   ```bash
   limactl shell chromium-linux
   sudo apt install build-essential python3 git curl clang lld ninja-build pkg-config
   # Install Chromium build deps (see chromium/src/build/install-build-deps.sh)
   # Install rustup, depot_tools
   # Clone workspace into ~/maho-workspace (NOT on shared mount)
   ```

5. Register GitHub Actions runner inside the VM with labels `self-hosted,chromium-builder,linux,arm64`.

6. Register a LaunchAgent so the VM auto-starts on Mac boot:
   ```bash
   # ~/Library/LaunchAgents/io.lima.chromium-linux.plist
   # Runs: limactl start chromium-linux --tty=false
   launchctl load ~/Library/LaunchAgents/io.lima.chromium-linux.plist
   ```

**Critical caveat — filesystem performance:**

Chromium source must live **inside the VM's disk**, NOT on a Lima mount from macOS. Mount performance (via `virtiofs`) is 2–3x slower than native ext4/btrfs. Clone `maho-workspace` inside `/home/ubuntu/` and treat the VM as a fully independent build environment. Cross-copy artifacts only after build completion.

**Alternative to Lima:** OrbStack (paid for teams) offers similar functionality with better UX — 3-second boot, dynamic resource allocation. Use whichever fits the team's preferences.

**Resource allocation:** With `cpus: 6` on a Mac mini M4 (10 cores total), macOS and Linux arm64 builds can run concurrently, each at partial resources. If serial execution is preferred, use GitHub Actions `concurrency` groups to force mutual exclusion.

---

## Windows Native Runner Setup

**Host:** Windows x86_64 PC (Windows 10 22H2+ or Windows 11).

**Chromium build performance:**
- Ryzen 7950X (16-core, 32GB): ~40 min clean, ~4 min incremental
- Older i7/i9 (8-core, 32GB): ~90 min clean, ~10 min incremental

**Setup steps:**

1. Install Visual Studio Build Tools 2022 with:
   - "Desktop development with C++" workload
   - Windows 10/11 SDK (10.0.22000.0 or later)
   - MSVC v143 toolset
2. Install `depot_tools` (download the ZIP, extract, add to PATH — do NOT use `git clone` per Chromium docs).
3. Install Rust: `rustup-init.exe` → default → `rustup target add x86_64-pc-windows-msvc`
4. Set `DEPOT_TOOLS_WIN_TOOLCHAIN=0` in system environment variables.
5. Enable "Long path support" via `gpedit.msc` (Local Computer Policy → Administrative Templates → System → Filesystem → "Enable Win32 long paths" = Enabled).
6. Clone workspace to a **short path** (e.g. `C:\maho\`), NOT `C:\Users\...`, to avoid MAX_PATH issues in Chromium build.
7. Register runner with labels `self-hosted,chromium-builder,windows,x64`.

**Storage:** Minimum 500GB free on the build volume (Chromium source + build + intermediates).

**Known limitation:** The `.build.lock` mechanism (`maho/scripts/build_lock.py`) uses POSIX `fcntl.flock`. Windows compatibility has not been verified. If lock acquisition fails at build start, the lock helper needs a Windows abstraction (`msvcrt._locking` or Win32 `LockFileEx`). See `.omo/plans/cross-platform-completion-plan-2026-07-10.md` Phase A.6.

---

## Linux x64 Runner — Three Options

Choose based on available hardware and topology:

### Option 1: Dedicated spare hardware (best if available)

Any x86_64 machine (spare desktop, old workstation, mini PC). Install Ubuntu 24.04 LTS natively. Best performance, zero shared-resource conflicts.

Setup mirrors the macOS runner steps: install `depot_tools`, Rust, register the runner.

### Option 2: Hyper-V VM on the Windows PC

If the Windows PC has 32GB+ RAM and 16+ cores, run an Ubuntu VM alongside the Windows native runner.

1. Enable Hyper-V in Windows Features.
2. Create Ubuntu 24.04 LTS VM with 8 vCPU, 16GB RAM, 250GB disk.
3. Configure VM to auto-start on host boot.
4. Register runner inside the VM.

**Trade-off:** Windows and Linux x64 builds share the same physical machine. Use GitHub Actions `concurrency` groups if simultaneous execution would exhaust resources.

### Option 3: WSL2 + Docker on Windows

Least recommended. Chromium builds inside WSL2 experience 10–20% filesystem overhead. Requires storing Chromium source inside the WSL2 filesystem (e.g. `~/`), NOT on `/mnt/c/`. Docker Desktop or podman-machine both work; OrbStack is not available on Windows.

**Selection heuristic:**
- Have spare hardware? → **Option 1**
- Windows PC has 32GB+ / 16+ cores? → **Option 2**
- Neither? → Buy a used mini PC ($400–800) or rent Hetzner AX42 (€55/mo)

---

## Build Cache Infrastructure — sccache + Cloudflare R2

Bare-metal is only cheap if builds are cached. `sccache` intercepts C/C++/Rust compilation and caches by content hash to a remote store.

### Why Cloudflare R2

- **Zero egress cost** — the killer feature. Every cache hit pulls from R2 to the runner; on any other provider this would rack up bandwidth charges.
- S3-compatible API — sccache supports it directly.
- ~$15/month for 100GB storage.

### Setup

1. Create a Cloudflare R2 bucket:
   ```bash
   # Via Cloudflare dashboard: R2 → Create bucket → "maho-sccache"
   ```

2. Generate an R2 API token with read/write to the bucket.

3. On each runner, install sccache:
   ```bash
   # macOS / Linux
   curl -L https://github.com/mozilla/sccache/releases/latest/download/sccache-*.tar.gz | tar xz
   sudo mv sccache /usr/local/bin/

   # Windows
   choco install sccache
   ```

4. Configure sccache to use R2. Create `~/.config/sccache/config` (Linux/Mac) or `%APPDATA%\Mozilla\sccache\config\config` (Windows):
   ```toml
   [cache.s3]
   bucket = "maho-sccache"
   endpoint = "https://<ACCOUNT_ID>.r2.cloudflarestorage.com"
   region = "auto"
   key_prefix = "sccache/"
   no_credentials = false
   ```

5. Set environment variables in the runner service:
   ```
   AWS_ACCESS_KEY_ID=<R2 access key>
   AWS_SECRET_ACCESS_KEY=<R2 secret>
   SCCACHE_S3_USE_SSL=true
   RUSTC_WRAPPER=sccache
   ```

6. Wire sccache into Chromium GN args (add to each `args_*.gn` or a local override):
   ```gn
   cc_wrapper = "sccache"
   ```

### Expected impact

- Cold cache: no speedup on first build.
- Warm cache (typical PR): 70–90% cache hit rate → **incremental Chromium build drops from 30 min to 5–15 min**.
- Rust cache: high reuse across Rust workspace edits.
- Per-platform cache namespaces prevent cross-platform pollution (sccache handles this automatically via target triple).

### Cost expectation

- Storage: ~50–100GB after 3 months (varies by churn) = $8–15/month
- Egress: $0 (R2 free egress)
- Total: ~$15/month for all 4 runners combined

---

## GitHub Actions Workflow Integration

### Current state

The existing `.github/workflows/traffic_annotation.yml` uses `[self-hosted, chromium-builder]`. This is the pattern to extend.

### Recommended matrix workflow (proposed)

Create `.github/workflows/build-matrix.yml`:

```yaml
name: Cross-platform build
on: [pull_request]

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: macos-arm64
            labels: [self-hosted, chromium-builder, macos, arm64]
          - os: linux-x64
            labels: [self-hosted, chromium-builder, linux, x64]
          - os: linux-arm64
            labels: [self-hosted, chromium-builder, linux, arm64]
          - os: windows-x64
            labels: [self-hosted, chromium-builder, windows, x64]
    runs-on: ${{ matrix.labels }}
    steps:
      - uses: actions/checkout@v4
      - name: Sync Chromium
        run: gclient sync
      - name: Build
        run: python3 maho-chromium/build/scripts/build_maho.py
```

**Concurrency policy:** Chromium builds are expensive. Add:

```yaml
concurrency:
  group: chromium-build-${{ matrix.os }}-${{ github.ref }}
  cancel-in-progress: true
```

This ensures a new PR push cancels the older run on the same platform.

### Selective builds

Not every PR needs to trigger a full cross-platform build. Use path filters:

```yaml
on:
  pull_request:
    paths:
      - 'maho-chromium/**'
      - 'maho/**'
      - '!**.md'
```

For PRs touching only Rust code, run `cargo check --target <triple>` on each platform via `ubuntu-latest` (free tier) rather than firing self-hosted Chromium builds.

---

## Total Monthly Cost

Assuming hardware is already owned:

| Item | Monthly cost |
|---|---|
| Mac (owned) | $0 |
| Windows PC (owned) | $0 |
| Cloudflare R2 storage (~50GB) | ~$8 |
| Cloudflare R2 requests (~10M/mo) | ~$4 |
| Electricity (approximation) | ~$10 |
| **Total** | **~$22/month** |

Compare to hosted CI at equivalent throughput:

| Provider | 60hrs/mo of chromium-scale builds |
|---|---|
| GitHub Actions (Linux 64-core) | $920 |
| GitHub Actions (macOS M2 XL) | $1,150 |
| GitHub Actions (Windows 64-core) | $1,800 |
| BuildJet / Depot / Namespace | $190–230 |

**Savings: ~$3,800/mo vs. all-GitHub-hosted; ~$570/mo vs. specialized providers.**

---

## Operational Notes

### Runner health monitoring

- Configure GitHub Actions runner logs to rotate weekly.
- Use `systemctl status actions.runner.*` (Linux/Mac) or Service Manager (Windows) to verify runners are online.
- Alert on runner offline > 15 minutes via a Cloudflare Worker cron polling the GitHub API.

### Disk space management

Chromium builds consume disk aggressively. Add a nightly cleanup job:

```bash
# Prune old build outputs
cd chromium/src && gn clean out/Default
# sccache local cache trim (kept size at 20GB per runner)
sccache --stop-server && rm -rf ~/.cache/sccache/*
```

### Runner isolation

- Each runner user must NOT have shell access to other users' data.
- Runner tokens rotate every 90 days per GitHub policy — automate renewal.
- Do not run runners as root/Administrator.

### Failure recovery

If a Chromium clean-build breaks (rare after `gclient sync`):

1. Check `.build.lock` — remove the file only if no `build_maho.py` process is running (see `AGENTS.md`).
2. Clear `out/Default/` and re-run `gn gen out/Default`.
3. If the failure is at GN regen stage, do NOT edit BUILD.gn files to bypass; report to the team (per `maho-chromium/AGENTS.md` anti-pattern).

---

## Future Improvements

Deferred until initial CI is stable:

1. **Distributed build** via `reclient` (Chromium's official system) — appropriate at Tier 3 team size.
2. **Persistent chromium/ warm mirrors** — set up a local `git-cache-http-server` to reduce initial `gclient sync` from 30 min to 5 min.
3. **Cross-arch build orchestration** — for Windows arm64 and Linux arm64 packages, once GN presets exist.
4. **Automated release builds** on tags — currently manual.

See `.omo/plans/cross-platform-completion-plan-2026-07-10.md` Phase D for the CI-specific rollout sequence.
