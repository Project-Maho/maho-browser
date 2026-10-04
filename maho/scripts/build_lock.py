#!/usr/bin/env python3
"""Shared build lock helper for repo-wide build entrypoints."""

from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import time
from contextlib import contextmanager
from typing import Iterator

if os.name == 'nt':
    import msvcrt

    def _lock(file) -> bool:
        try:
            file.seek(0)
            msvcrt.locking(file.fileno(), msvcrt.LK_NBLCK, 1)
            return True
        except OSError:
            return False

    def _unlock(file) -> None:
        try:
            file.seek(0)
            msvcrt.locking(file.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
else:
    import fcntl

    def _lock(file) -> bool:
        try:
            fcntl.flock(file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except BlockingIOError:
            return False

    def _unlock(file) -> None:
        try:
            fcntl.flock(file.fileno(), fcntl.LOCK_UN)
        except (BlockingIOError, OSError):
            pass

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WAIT_INTERVAL_SECONDS = 0.25
_DEFAULT_TIMEOUT_SECONDS = 3000.0


def discover_workspace_root() -> str:
    current = _SCRIPT_DIR
    while True:
        if os.path.isdir(os.path.join(current, 'maho')) and os.path.isdir(os.path.join(current, 'maho-chromium')):
            return current
        parent = os.path.dirname(current)
        if parent == current:
            raise RuntimeError('workspace root not found')
        current = parent


WORKSPACE_ROOT = discover_workspace_root()
LOCK_PATH = os.path.join(WORKSPACE_ROOT, '.build.lock')
SOCKET_PATH = os.path.join(WORKSPACE_ROOT, '.build-queue.sock')

_FULL_TARGETS = frozenset({'chrome', 'unit_tests', 'interactive_ui_tests', 'browser_tests'})


def classify_targets(targets) -> str:
    # Narrow parallel out dirs were removed (warming cost not worth it); all
    # builds serialize on out/Default via the single full slot.
    return 'full'


def _flock_path_for(out_dir) -> str:
    # full/legacy share .build.lock (also excludes Rust/test entrypoints); each
    # narrow out dir gets its own backstop lock so narrows run concurrently.
    if out_dir in (None, 'out/Default'):
        return LOCK_PATH
    return os.path.join(WORKSPACE_ROOT, '.build-lock-' + out_dir.replace('/', '_'))


@contextmanager
def _flock(path: str, timeout_seconds: float) -> Iterator[None]:
    lock_file = open(path, 'a+')
    start = time.monotonic()
    warned = False
    try:
        while True:
            if _lock(lock_file):
                break
            if timeout_seconds is not None and time.monotonic() - start >= timeout_seconds:
                raise TimeoutError(f'timed out waiting for build lock after {timeout_seconds:.0f}s: {path}')
            if not warned:
                print(f'Waiting for build lock: {path}', file=sys.stderr)
                warned = True
            time.sleep(_WAIT_INTERVAL_SECONDS)
        yield
    finally:
        try:
            _unlock(lock_file)
        finally:
            lock_file.close()


def _queue_acquire(build_class: str, timeout_seconds: float):
    # Block (server-push, no polling) until the daemon grants a slot. Returns
    # (conn, out_dir); the connection is the lease and must stay open for the
    # whole build. Returns None if the daemon is unreachable (caller falls back).
    try:
        conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        conn.settimeout(timeout_seconds if timeout_seconds else None)
        conn.connect(SOCKET_PATH)
        conn.sendall(f'ACQUIRE {build_class}\n'.encode())
        buf = b''
        while b'\n' not in buf:
            chunk = conn.recv(256)
            if not chunk:
                conn.close()
                return None
            buf += chunk
        line = buf.decode().strip()
        if line.startswith('GRANTED '):
            conn.settimeout(None)
            return conn, line[len('GRANTED '):].strip()
        conn.close()
        return None
    except OSError:
        return None


@contextmanager
def build_lock(build_class=None, timeout_seconds: float = _DEFAULT_TIMEOUT_SECONDS):
    # Chromium builds (build_class full|narrow) go through the queue daemon, which
    # assigns an out dir and bounds concurrency. A per-out-dir flock is the real
    # mutual-exclusion backstop, so daemon bugs/downtime can only affect ordering,
    # never corrupt an out dir. If the daemon is down, fall back to .build.lock.
    if os.name == 'nt':
        # Windows has neither fcntl.flock nor socket.AF_UNIX, so both the queue
        # daemon and the flock backstop are unavailable. The Windows build box
        # runs a single build at a time, so proceed without the lock.
        yield 'out/Default' if build_class in ('full', 'narrow') else None
        return
    if build_class in ('full', 'narrow'):
        granted = _queue_acquire(build_class, timeout_seconds)
        if granted is not None:
            conn, out_dir = granted
            try:
                with _flock(_flock_path_for(out_dir), timeout_seconds):
                    yield out_dir
            finally:
                try:
                    conn.close()  # closing the lease connection frees the daemon slot
                except OSError:
                    pass
            return
        # daemon unreachable: conservative fallback (serialize on out/Default)
        with _flock(LOCK_PATH, timeout_seconds):
            yield 'out/Default'
        return
    # legacy callers (Rust prebuild, tests, wrapped commands): global lock, no out dir
    with _flock(LOCK_PATH, timeout_seconds):
        yield None


def run_command(command: list[str], timeout_seconds: float = _DEFAULT_TIMEOUT_SECONDS,
                build_class=None) -> int:
    with build_lock(build_class, timeout_seconds):
        return subprocess.run(command).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description='Run a command under the shared build lock/queue.')
    parser.add_argument('--timeout', type=float, default=_DEFAULT_TIMEOUT_SECONDS, help='Lock wait timeout in seconds.')
    parser.add_argument('--class', dest='build_class', choices=('full', 'narrow'), default=None,
                        help='Build class for the queue daemon; omit for the legacy global lock.')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()

    command = args.command
    if command and command[0] == '--':
        command = command[1:]
    if not command:
        parser.error('missing command after --')

    return run_command(command, args.timeout, args.build_class)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except TimeoutError as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        sys.exit(1)
