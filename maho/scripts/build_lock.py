#!/usr/bin/env python3
"""Shared build lock helper for repo-wide build entrypoints."""

from __future__ import annotations

import argparse
import os
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


@contextmanager
def build_lock(timeout_seconds: float = _DEFAULT_TIMEOUT_SECONDS) -> Iterator[None]:
    lock_file = open(LOCK_PATH, 'a+')
    start = time.monotonic()
    warned = False
    try:
        while True:
            if _lock(lock_file):
                break
            if timeout_seconds is not None and time.monotonic() - start >= timeout_seconds:
                raise TimeoutError(f'timed out waiting for build lock after {timeout_seconds:.0f}s: {LOCK_PATH}')
            if not warned:
                print(f'Waiting for build lock: {LOCK_PATH}', file=sys.stderr)
                warned = True
            time.sleep(_WAIT_INTERVAL_SECONDS)
        yield
    finally:
        try:
            _unlock(lock_file)
        finally:
            lock_file.close()


def run_command(command: list[str], timeout_seconds: float = _DEFAULT_TIMEOUT_SECONDS) -> int:
    with build_lock(timeout_seconds):
        return subprocess.run(command).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description='Run a command under the shared build lock.')
    parser.add_argument('--timeout', type=float, default=_DEFAULT_TIMEOUT_SECONDS, help='Lock wait timeout in seconds.')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()

    command = args.command
    if command and command[0] == '--':
        command = command[1:]
    if not command:
        parser.error('missing command after --')

    return run_command(command, args.timeout)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except TimeoutError as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        sys.exit(1)
