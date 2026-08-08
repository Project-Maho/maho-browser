# Build Queue Daemon

Serializes Chromium builds through a single output directory (`out/Default`) so
concurrent agent/developer builds don't oversubscribe RAM (swap thrash) or
collide on one out dir. Adds queue visibility, no-polling waits, and automatic
slot release (crash-safe) on top of the plain `.build.lock`.

> Earlier this supported parallel "narrow" builds in separate out dirs
> (out/Narrow1/2), but the one-time warming cost per fresh out dir wasn't worth
> it, so **all builds now serialize on out/Default**. Parallelism can be
> re-enabled via `MAHO_BQ_NARROW_DIRS` (see below), but that path is unmaintained.

## How builds enqueue (automatic)

Just build normally — enqueuing is automatic:

```bash
python3 maho-chromium/build/scripts/build_maho.py [--ninja-target <t>]
```

`build_maho.py` → `build_lock.py` connects the daemon, **blocks (no polling)**
until the single `out/Default` slot is free, builds, then releases on exit
(closing the connection). If the daemon is down, it **falls back** to the plain
`.build.lock` — builds still work, just without queue visibility.

## Correctness model

The daemon is an **advisory scheduler, not the lock**. Every build also holds an
`flock` on `.build.lock`. So a daemon bug, restart, or bypass can only affect
*ordering* — never allow two concurrent builds on `out/Default`. (Verified: after
a daemon restart it may re-grant the slot, but the `flock` backstop still blocks
the second build.)

## Checking the queue

```bash
printf 'STATUS\n' | nc -U .build-queue.sock
# running=0 used_mb=0/40000 default_free=true narrow_free=[] pending=0
```

## Protocol (direct integration)

Unix socket at `<workspace>/.build-queue.sock`, newline-delimited:

```
client -> "ACQUIRE full\n"
server -> "GRANTED out/Default\n"   (holds the connection = the lease)
release  = close the connection (kernel-detected on crash) or send "RELEASE\n"
client -> "STATUS\n"  -> snapshot, then close
```

The held connection *is* the lease: if the owning process dies, the kernel tears
down the socket, the daemon sees EOF, and frees the slot — no orphaned locks
(this fixed the recurring dead-session orphan-build problem).

## Config (env)

| Env var | Default | Meaning |
|---|---|---|
| `MAHO_BQ_BUDGET_MB` | 40000 | RAM budget (only relevant if narrow re-enabled) |
| `MAHO_BQ_FULL_MB` | 20000 | estimated peak per full build |
| `MAHO_BQ_NARROW_DIRS` | (empty) | narrow out-dir pool — empty = Default-only serialization |

## Daemon lifecycle

- Binary: `maho/build-queue/` (standalone Rust crate, std-only).
  Build: `cd maho/build-queue && cargo build --release`.
- launchd: `~/Library/LaunchAgents/com.maho.build-queue.plist`
  (`RunAtLoad` + `KeepAlive`), socket at `<workspace>/.build-queue.sock`.

```bash
launchctl list | grep build-queue
launchctl kickstart -k gui/$(id -u)/com.maho.build-queue
```

If the daemon is down, builds still work via the `.build.lock` fallback.
