// Maho build-queue daemon.
//
// A tiny advisory scheduler over a Unix-domain socket. It bounds how many
// Chromium builds run at once and assigns each an output directory, so that
// concurrent agent/developer builds do not oversubscribe RAM (swap thrash).
//
// CORRECTNESS NOTE: this daemon is NOT the mutual-exclusion primitive. Each
// build additionally holds an flock on its assigned out-dir lock file (done by
// the client). If this daemon crashes, restarts, or is bypassed, the flock
// still guarantees one build per out dir -- the daemon only affects ordering.
//
// Protocol (newline-delimited, over $WORKSPACE/.build-queue.sock):
//   client -> "ACQUIRE full\n" | "ACQUIRE narrow\n"
//   server -> "GRANTED <out_dir>\n"   (then holds the connection)
//   release = client closes the connection (kernel-detected) or sends "RELEASE\n"
//   client -> "STATUS\n"  ->  server writes a snapshot and closes.
//
// The held connection is the lease: when the owning process dies for any reason
// the kernel tears the socket down, the server sees EOF, and frees the slot.

use std::collections::{HashMap, VecDeque};
use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::PathBuf;
use std::sync::{Arc, Condvar, Mutex};
use std::thread;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Class {
    Full,
    Narrow,
}

struct Config {
    socket_path: PathBuf,
    full_dir: String,
    narrow_dirs: Vec<String>,
    budget_mb: u64,
    full_mb: u64,
    narrow_mb: u64,
}

impl Config {
    fn from_env() -> Config {
        let root = std::env::var("MAHO_WORKSPACE_ROOT").unwrap_or_else(|_| discover_root());
        let socket_path = std::env::var("MAHO_BQ_SOCKET")
            .map(PathBuf::from)
            .unwrap_or_else(|_| PathBuf::from(&root).join(".build-queue.sock"));
        let env_u64 = |k: &str, d: u64| {
            std::env::var(k).ok().and_then(|v| v.parse().ok()).unwrap_or(d)
        };
        Config {
            socket_path,
            full_dir: std::env::var("MAHO_BQ_FULL_DIR").unwrap_or_else(|_| "out/Default".into()),
            // Narrow parallel out dirs removed by default (all builds serialize on
            // out/Default). Set MAHO_BQ_NARROW_DIRS to re-enable narrow parallelism.
            narrow_dirs: std::env::var("MAHO_BQ_NARROW_DIRS")
                .map(|v| v.split(',').map(|s| s.trim().to_string()).filter(|s| !s.is_empty()).collect())
                .unwrap_or_default(),
            budget_mb: env_u64("MAHO_BQ_BUDGET_MB", 40_000),
            full_mb: env_u64("MAHO_BQ_FULL_MB", 20_000),
            narrow_mb: env_u64("MAHO_BQ_NARROW_MB", 8_000),
        }
    }
}

fn discover_root() -> String {
    // Walk up from CWD until a dir containing both `maho` and `maho-chromium`.
    let mut cur = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
    loop {
        if cur.join("maho").is_dir() && cur.join("maho-chromium").is_dir() {
            return cur.to_string_lossy().into_owned();
        }
        match cur.parent() {
            Some(p) => cur = p.to_path_buf(),
            None => return ".".into(),
        }
    }
}

struct Sched {
    cfg: Config,
    pending: VecDeque<(u64, Class)>,
    granted: HashMap<u64, String>,        // req id -> assigned out_dir (signals waiting handler)
    running: HashMap<u64, (String, u64)>, // req id -> (out_dir, weight_mb)
    default_free: bool,
    narrow_free: Vec<bool>,
    used_mb: u64,
    next_id: u64,
}

impl Sched {
    fn new(cfg: Config) -> Sched {
        let n = cfg.narrow_dirs.len();
        Sched {
            cfg,
            pending: VecDeque::new(),
            granted: HashMap::new(),
            running: HashMap::new(),
            default_free: true,
            narrow_free: vec![true; n],
            used_mb: 0,
            next_id: 1,
        }
    }

    // FIFO admission with head-of-line RAM reservation for a waiting full build:
    // if a full build's out dir is free but the RAM budget can't fit it yet,
    // reserve its weight so narrow admissions can't starve it out.
    fn schedule(&mut self) {
        // Reserve full weight iff a queued full is dir-available but RAM-blocked.
        let mut reserve_mb = 0u64;
        for &(_id, class) in &self.pending {
            if class == Class::Full
                && self.default_free
                && self.used_mb + self.cfg.full_mb > self.cfg.budget_mb
            {
                reserve_mb = self.cfg.full_mb;
                break;
            }
        }

        let mut i = 0;
        while i < self.pending.len() {
            let (id, class) = self.pending[i];
            let admitted = match class {
                Class::Full => {
                    if self.default_free && self.used_mb + self.cfg.full_mb <= self.cfg.budget_mb {
                        self.default_free = false;
                        self.used_mb += self.cfg.full_mb;
                        self.assign(id, self.cfg.full_dir.clone(), self.cfg.full_mb);
                        true
                    } else {
                        false
                    }
                }
                Class::Narrow => {
                    let free_idx = self.narrow_free.iter().position(|&f| f);
                    if let Some(idx) = free_idx {
                        if self.used_mb + self.cfg.narrow_mb + reserve_mb <= self.cfg.budget_mb {
                            self.narrow_free[idx] = false;
                            self.used_mb += self.cfg.narrow_mb;
                            let dir = self.cfg.narrow_dirs[idx].clone();
                            self.assign(id, dir, self.cfg.narrow_mb);
                            true
                        } else {
                            false
                        }
                    } else {
                        false
                    }
                }
            };
            if admitted {
                self.pending.remove(i);
            } else {
                i += 1;
            }
        }
    }

    fn assign(&mut self, id: u64, dir: String, weight: u64) {
        self.running.insert(id, (dir.clone(), weight));
        self.granted.insert(id, dir);
    }

    fn release(&mut self, id: u64) {
        if let Some((dir, weight)) = self.running.remove(&id) {
            self.used_mb = self.used_mb.saturating_sub(weight);
            if dir == self.cfg.full_dir {
                self.default_free = true;
            } else if let Some(idx) = self.cfg.narrow_dirs.iter().position(|d| *d == dir) {
                self.narrow_free[idx] = true;
            }
        }
        // Drop pending entry too, in case release arrives before grant (rare).
        self.pending.retain(|&(pid, _)| pid != id);
        self.granted.remove(&id);
    }

    fn status(&self) -> String {
        let running: Vec<String> = self
            .running
            .iter()
            .map(|(id, (dir, w))| format!("#{id} {dir} {w}MB"))
            .collect();
        format!(
            "running={} used_mb={}/{} default_free={} narrow_free={:?} pending={}\n  [{}]\n",
            self.running.len(),
            self.used_mb,
            self.cfg.budget_mb,
            self.default_free,
            self.narrow_free,
            self.pending.len(),
            running.join(", ")
        )
    }
}

type Shared = Arc<(Mutex<Sched>, Condvar)>;

fn handle(stream: UnixStream, shared: Shared) {
    let peer = stream.try_clone().ok();
    let mut reader = BufReader::new(stream);
    let mut line = String::new();
    if reader.read_line(&mut line).is_err() {
        return;
    }
    let cmd = line.trim();
    let mut wstream = match peer {
        Some(s) => s,
        None => return,
    };

    if cmd == "STATUS" {
        let (lock, _) = &*shared;
        let snap = lock.lock().unwrap().status();
        let _ = wstream.write_all(snap.as_bytes());
        return;
    }

    let class = if cmd == "ACQUIRE full" {
        Class::Full
    } else if cmd == "ACQUIRE narrow" {
        Class::Narrow
    } else {
        let _ = wstream.write_all(b"ERROR unknown command\n");
        return;
    };

    let (lock, cvar) = &*shared;

    // Enqueue + schedule.
    let id = {
        let mut s = lock.lock().unwrap();
        let id = s.next_id;
        s.next_id += 1;
        s.pending.push_back((id, class));
        s.schedule();
        cvar.notify_all();
        id
    };

    // Wait for this request to be granted an out dir.
    let out_dir = {
        let mut s = lock.lock().unwrap();
        loop {
            if let Some(dir) = s.granted.get(&id) {
                break dir.clone();
            }
            s = cvar.wait(s).unwrap();
        }
    };

    if wstream.write_all(format!("GRANTED {out_dir}\n").as_bytes()).is_err() {
        // Client vanished between grant and notify; release immediately.
        let mut s = lock.lock().unwrap();
        s.release(id);
        s.schedule();
        cvar.notify_all();
        return;
    }

    // Hold the lease: block until the client closes the connection (EOF -> the
    // owning process died or finished) or sends RELEASE. Either frees the slot.
    let mut buf = String::new();
    let _ = reader.read_line(&mut buf); // returns Ok(0)/EOF on close, or a line

    let mut s = lock.lock().unwrap();
    s.release(id);
    s.schedule();
    cvar.notify_all();
}

fn main() {
    let cfg = Config::from_env();
    let socket_path = cfg.socket_path.clone();
    // Remove any stale socket from a previous run.
    let _ = std::fs::remove_file(&socket_path);
    let listener = match UnixListener::bind(&socket_path) {
        Ok(l) => l,
        Err(e) => {
            eprintln!("maho-build-queued: cannot bind {}: {e}", socket_path.display());
            std::process::exit(1);
        }
    };
    eprintln!(
        "maho-build-queued: listening on {} (budget {}MB: full {}MB, narrow {}MB x{})",
        socket_path.display(),
        cfg.budget_mb,
        cfg.full_mb,
        cfg.narrow_mb,
        cfg.narrow_dirs.len()
    );
    let shared: Shared = Arc::new((Mutex::new(Sched::new(cfg)), Condvar::new()));

    for stream in listener.incoming() {
        match stream {
            Ok(s) => {
                let sh = Arc::clone(&shared);
                thread::spawn(move || handle(s, sh));
            }
            Err(e) => eprintln!("maho-build-queued: accept error: {e}"),
        }
    }
}
