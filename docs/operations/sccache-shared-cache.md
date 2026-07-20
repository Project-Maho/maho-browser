# sccache Shared Compile Cache (LAN)

Local shared compile cache for the Maho build (Rust + Chromium C++), backed by
a MinIO (S3-compatible) store running on a second Mac on the LAN/Tailnet.

The goal is **not** distributed compilation (offloading CPU is not possible Mac
-to-Mac; see the research summary below). The goal is a **shared object cache**:
avoid recompiling unchanged code across clean builds, fresh `out/` directories,
and branch switches. Since day-to-day work is on `main`, cache locality is high.

## What speeds up (and what does not)

| Scenario | Benefit |
|----------|---------|
| Clean build / fresh `out/` dir | High — near-incremental instead of full compile |
| Branch switch / rebase invalidating many files | Real |
| Same-`out/` incremental edit of a few files | Minimal — ninja/cargo already handle this |
| Final `chrome` binary link | None — linking is never cached |

## Topology

```
Main Mac (this machine)                     Windows desktop (desktop-1lapjmp, always on)
  ninja / cargo                               MinIO  (actual cache bytes)
    | cc_wrapper / rustc-wrapper                 scheduled task: MahoMinio (SYSTEM, ONSTART)
  sccache broker (~10MB, CPU ~0)  --S3 over--    :9000, bucket "sccache"
    launchd: com.maho.sccache        Tailscale   survives reboot
    (S3 credentials scoped here)     (~50ms)
```

- **MinIO** holds the cache data; it runs on the **Windows desktop** (chosen
  because it stays powered on 24/7, unlike the idle Mac). No cache bytes are
  stored on the main Mac.
- The **sccache broker** on the main Mac is a lightweight client-server daemon.
  It is required on every machine that compiles; it forwards reads/writes to
  MinIO. It does not store the cache locally.
- **Latency**: the Windows box is on a different network (~50ms over Tailscale)
  vs the LAN-local idle Mac (~7ms). Cache reads/writes are slower per object,
  but availability (always-on) matters more here.

## Endpoints & credentials

- MinIO endpoint (Tailscale): `100.126.171.58:9000` (Windows `desktop-1lapjmp`)
- Bucket: `sccache`, key prefix: `maho`
- Access key: `mahocache`. The secret lives **only** in:
  - Main Mac broker: `~/Library/LaunchAgents/com.maho.sccache.plist` (chmod 600)
  - Windows: `C:\Users\sook\minio\run-minio.bat` (sets `MINIO_ROOT_*` env)
  Never in the repo.

Credentials are deliberately kept out of the global environment so the Mac's
existing `~/.aws/credentials` `[default]` profile is untouched. Only the sccache
broker process (started by launchd) knows the S3 credentials; compile clients
need no credentials at all.

### Windows MinIO server (persistence)

- Binary: `C:\Users\sook\minio\minio.exe`, data: `C:\Users\sook\minio\data`
- Launcher: `C:\Users\sook\minio\run-minio.bat`
- Auto-start: Scheduled Task `MahoMinio` (trigger: At startup, run as `SYSTEM`,
  highest privileges, restart-on-failure). Survives reboot and logoff.
- Firewall: inbound TCP `9000,9001` allowed (rule `MahoMinio`, all profiles).

Access from a Mac (via SSH over Tailscale, key auth):
`ssh sook@100.126.171.58`

## Client wiring (already applied on the main Mac)

- **Rust** — `maho/.cargo/config.toml` (this file is gitignored):
  ```toml
  [build]
  rustc-wrapper = "sccache"
  incremental = false
  ```
- **Chromium** — `chromium/src/out/Default/args.gn` (untracked, under `out/`):
  ```gn
  cc_wrapper = "sccache"
  use_clang_modules = false        # -fmodules is non-cacheable by sccache
  clang_use_chrome_plugins = false # -Xclang plugin flags are non-cacheable
  ```
  Without the latter two, Chromium C++ is ~99% non-cacheable: `-fmodules`
  (Clang modules) and Chromium's `-Xclang` plugins (blink GC, find-bad-constructs)
  are flags sccache cannot cache. Cost of disabling them: local builds skip those
  static-analysis plugins (CI still runs them) and lose Clang-modules build speed,
  in exchange for a cacheable C++ build. `build_maho.py` does not overwrite an
  existing `out/<dir>/args.gn` (only with `--force-args-template`), so this
  persists. ninja re-runs `gn gen` automatically when `args.gn` changes.

The tracked GN template `maho-chromium/build/config/args_mac.gn` is intentionally
**not** modified, so nothing here is committable by accident.

## Daily use

Build as usual — the cache is transparent:
```bash
python3 maho-chromium/build/scripts/build_maho.py
```
Check hit rate anytime:
```bash
sccache --show-stats
```

## Requirements / operational notes

- The idle Mac must stay awake and networked (cache is unreachable otherwise;
  builds then fall back to local compilation — slower, never broken). Sleep is
  disabled on it and MinIO auto-restarts via launchd.
- Both launchd agents have `RunAtLoad` + `KeepAlive`, so the cache survives
  reboot and process crashes.

## Troubleshooting

```bash
# Is MinIO up and reachable from the main Mac?
curl -sf http://100.126.171.58:9000/minio/health/ready && echo ready

# Is the sccache broker on S3 (not disk) and error-free?
sccache --show-stats | grep -iE "cache location|errors"

# Restart the broker (picks up S3 config from the plist)
launchctl kickstart -k gui/$(id -u)/com.maho.sccache

# On Windows (via SSH): check / restart the MinIO task
ssh sook@100.126.171.58 "powershell -NoProfile -Command \"(Get-ScheduledTask MahoMinio).State\""
ssh sook@100.126.171.58 "powershell -NoProfile -Command \"Start-ScheduledTask MahoMinio\""
```

If `Cache location` shows `disk` instead of `s3`, a stray client-spawned server
grabbed the port. Stop all servers and restart the launchd one:
```bash
sccache --stop-server
launchctl bootout gui/$(id -u)/com.maho.sccache
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.maho.sccache.plist
launchctl kickstart -k gui/$(id -u)/com.maho.sccache
```

## Cross-checkout cache hits (future, if ever needed)

Cache hits require identical toolchain + flags. C++ hits across *different*
checkout paths depend on path normalization; Chromium's use of relative paths
covers much of this, but if a second checkout ever shows a low C/C++ hit rate,
set `SCCACHE_BASEDIR` to that checkout root in the build environment and
re-measure. Not relevant while all work happens in the single main checkout.

## Why not distributed compilation

Verified (2024–2026): true Mac-to-Mac CPU offloading is a dead end for this
codebase. `sccache-dist` build servers require Linux (bubblewrap/overlayfs);
distcc/icecc break on Chromium's hermetic clang + PGO/ThinLTO; reclient/Siso
need a full REAPI buildfarm backend. A shared cache is the practical win.
