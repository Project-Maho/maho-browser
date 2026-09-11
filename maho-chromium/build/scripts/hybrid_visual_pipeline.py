#!/usr/bin/env python3
"""Baseline-only CLI measurement. Run browser automation only inside Tart."""
from __future__ import annotations

import argparse
import asyncio
import base64
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import tempfile
import time
import shutil
from contextlib import AsyncExitStack, asynccontextmanager
from contextvars import ContextVar

FIXTURE = Path(__file__).with_name('security_fixtures') / 'baseline.html'
OPERATIONS = ('observe', 'act_click', 'act_type')
MAX_RECEIPT_BYTES = 8 * 1024 * 1024
SCENARIO_RUNTIME = ContextVar('scenario_runtime', default=None)


def resolve_node_binary():
    configured = os.environ.get('MAHO_NODE')
    homebrew = '/opt/homebrew/bin/node'

    def executable(candidate):
        return bool(candidate and Path(candidate).is_file() and
                    os.access(candidate, os.X_OK))

    if executable(configured):
        return configured
    if executable(homebrew):
        return homebrew
    on_path = shutil.which('node')
    if on_path:
        return on_path

    configured_reason = (f'MAHO_NODE={configured} missing or not executable'
                         if configured else 'MAHO_NODE unset')
    raise ScenarioBlocked(
        f'node runtime unavailable: {configured_reason}, '
        '/opt/homebrew/bin/node missing, node not on PATH')


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def validate_baseline(report):
    if report.get('scenario') != 'baseline' or report.get('status') != 'MEASURED':
        raise ValueError('baseline measurement missing')
    for field in ('cli_sha256', 'app_sha256', 'fixture_sha256'):
        value = report.get(field)
        if not isinstance(value, str) or len(value) != 64:
            raise ValueError(f'{field} missing')
    samples = report.get('samples', {})
    if set(samples) != set(OPERATIONS):
        raise ValueError('operation set mismatch')
    for operation, rows in samples.items():
        if len(rows) != 50:
            raise ValueError('exactly 50 samples required')
        for index, row in enumerate(rows):
            elapsed = row.get('elapsed_ms')
            if isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or not math.isfinite(elapsed) or elapsed < 0:
                raise ValueError('invalid elapsed sample')
            if operation != 'observe':
                kind = operation.removeprefix('act_')
                expected = index + 1 if kind == 'click' else f'baseline-{index}'
                receipt = row.get('receipt', {})
                if receipt.get('kind') != kind or receipt.get('value') != expected or receipt.get('isTrusted') is not True:
                    raise ValueError('missing trusted fixture postcondition')
    if report.get('internal_invocations') != {'screenshot': None, 'model': None}:
        raise ValueError('internal invocation instrumentation is unavailable')
    display = report.get('display')
    if not isinstance(display, dict):
        raise ValueError('measured display metadata missing')
    for field in ('screen_width', 'screen_height', 'viewport_width',
                  'viewport_height', 'device_pixel_ratio'):
        value = display.get(field)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            raise ValueError(f'invalid measured display field: {field}')


async def measure(pipe, fixture, iterations=50):
    samples = {op: [] for op in OPERATIONS}
    for index in range(iterations):
        start = time.perf_counter_ns()
        response = await pipe.request(op='observe')
        samples['observe'].append({'elapsed_ms': (time.perf_counter_ns() - start) / 1e6, 'response': response})
        for kind in ('click', 'type'):
            value = index + 1 if kind == 'click' else f'baseline-{index}'
            receipt = fixture.subscribe(kind, value)
            request = {'op': 'act', 'action': {'kind': kind}, 'lease': 'scoped', 'locator': {'css': '#baseline-click' if kind == 'click' else '#baseline-input'}}
            if kind == 'type':
                request['text'] = value
            start = time.perf_counter_ns()
            response = await pipe.request(**request)
            elapsed = (time.perf_counter_ns() - start) / 1e6
            received = await asyncio.wait_for(receipt, 15)
            if received.get('isTrusted') is not True:
                raise ValueError('untrusted fixture action')
            samples[f'act_{kind}'].append({'elapsed_ms': elapsed, 'receipt': received, 'response': response})
    return samples


async def verify_keyboard_followup(pipe, fixture, key_press):
    receipts = []
    for text in ('', 'keyboard-check'):
        pending = fixture.subscribe('type', text)
        await pipe.request(op='act', action={'kind': 'type'}, lease='scoped',
                           locator={'css': '#baseline-input'}, text=text)
        received = await asyncio.wait_for(pending, 15)
        if received.get('isTrusted') is not True or received.get('value') != text:
            raise ValueError('replacement did not produce trusted input')
        receipts.append(received)
    for number in (1, 2):
        if number == 2:
            pending = fixture.subscribe('caret', 1)
            await key_press('ArrowRight', [])
            received = await asyncio.wait_for(pending, 15)
            if received.get('isTrusted') is not True or received.get('selection') != [14, 14]:
                raise ValueError('caret did not collapse before cancelled SelectAll')
            receipts.append(received)
        pending = fixture.subscribe('shortcut', number)
        await key_press('a', ['meta'])
        received = await asyncio.wait_for(pending, 15)
        expected = [0, 14] if number == 1 else [14, 14]
        if (received.get('isTrusted') is not True or
                received.get('text') != 'keyboard-check' or
                received.get('before_selection') != [14, 14] or
                received.get('selection') != expected or
                received.get('input_events') != received.get('before_input_events')):
            raise ValueError(f'SelectAll postcondition failed: {received}')
        receipts.append(received)
    return receipts


class FixtureServer:
    """Plain loopback HTTP: no certificate bypass or duplicated fixture HTML."""
    def __init__(self, route='/baseline'):
        self.route = route
        self.pending = {}
        self.pending_posts = {}
        self.receipts = []
        self.active_episodes = set()

    def subscribe_post(self, path):
        if path in self.pending_posts:
            raise ValueError('duplicate endpoint subscription')
        future = asyncio.get_running_loop().create_future()
        self.pending_posts[path] = future
        return future

    def subscribe(self, kind, value):
        key = (kind, value)
        if key in self.pending:
            raise ValueError('duplicate receipt subscription')
        future = asyncio.get_running_loop().create_future()
        self.pending[key] = future
        return future

    async def start(self):
        if self.route == '/baseline':
            self.body = FIXTURE.read_bytes()
        else:
            node = resolve_node_binary()
            pages = Path(__file__).with_name('security_fixtures') / 'pages.js'
            script = (
                "const p=require(process.argv[1]).PAGE_BUILDERS[process.argv[2]];"
                "if(!p)process.exit(3);"
                "process.stdout.write(p(JSON.parse(process.argv[3])))"
            )
            # Bind first so the exact loopback origin embedded in the page is real.
            self.server = await asyncio.start_server(self.handle, '127.0.0.1', 0)
            port = self.server.sockets[0].getsockname()[1]
            origin = f'http://127.0.0.1:{port}'
            process = await asyncio.create_subprocess_exec(
                node, '-e', script, str(pages), self.route,
                json.dumps({'selfOrigin': origin, 'peerOrigin': origin,
                            'sentinel': 'S3NTINEL-maho-vault-9F4C'}),
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
            stdout, stderr = await process.communicate()
            if process.returncode != 0 or not stdout:
                await self.close()
                raise ScenarioBlocked(
                    f'node fixture render failed for {self.route}: {stderr.decode().strip()}')
            self.body = stdout
            import re
            m = re.search(r'data-episode-id="([^"]+)"', self.body.decode('utf-8', errors='ignore'))
            if m:
                self.active_episodes.add(m.group(1))
            self.url = origin + self.route
            return self
        self.server = await asyncio.start_server(self.handle, '127.0.0.1', 0)
        self.url = f'http://127.0.0.1:{self.server.sockets[0].getsockname()[1]}{self.route}'
        return self

    async def close(self):
        self.server.close()
        await self.server.wait_closed()
        for future in (*self.pending.values(), *self.pending_posts.values()):
            future.cancel()

    async def handle(self, reader, writer):
        try:
            try:
                headers = (await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), 10)).decode('ascii')
            except asyncio.IncompleteReadError as error:
                if error.partial:
                    raise
                return
            method, path, _ = headers.split('\r\n')[0].split()
            if method == 'GET' and path == self.route:
                body, status, mime = self.body, '200 OK', 'text/html'
            elif method == 'POST' and path in {
                    '/receipt', '/challenge/verify', '/challenge/misclick',
                    '/challenge/turnstile-verify', '/creepjs/results',
                    '/telemetry/trace'}:
                try:
                    length_headers = [
                        line.split(':', 1)[1].strip()
                        for line in headers.split('\r\n')
                        if line.lower().startswith('content-length:')]
                    if len(length_headers) != 1:
                        raise ValueError('exactly one Content-Length is required')
                    length = int(length_headers[0])
                    if not 0 < length <= MAX_RECEIPT_BYTES:
                        raise ValueError(
                            f'receipt length must be 1..{MAX_RECEIPT_BYTES} bytes')
                    event = json.loads(
                        await asyncio.wait_for(reader.readexactly(length), 10))
                    if not isinstance(event, dict):
                        raise ValueError('receipt JSON must be an object')
                    if path == '/receipt' and (
                            'kind' not in event or 'value' not in event):
                        raise ValueError(
                            'receipt JSON requires kind and value')
                except (ValueError, json.JSONDecodeError,
                        asyncio.IncompleteReadError, TimeoutError) as error:
                    blocker = {
                        'endpoint': path,
                        'kind': 'blocker',
                        'value': 'malformed-or-oversize-post',
                        'reason': str(error),
                    }
                    self.receipts.append(blocker)
                    runtime = SCENARIO_RUNTIME.get()
                    if runtime is not None:
                        runtime['receipts'].append(blocker)
                    pending = self.pending_posts.pop(path, None)
                    if pending is not None and not pending.done():
                        pending.set_exception(ScenarioBlocked(
                            f'fixture rejected {path}: {error}'))
                    body, status, mime = (json.dumps({'error': blocker}).encode(),
                                          '413 Payload Too Large'
                                          if 'length' in str(error) else
                                          '400 Bad Request',
                                          'application/json')
                else:
                    event = {'endpoint': path, **event}
                    self.receipts.append(event)
                    runtime = SCENARIO_RUNTIME.get()
                    if runtime is not None:
                        runtime['receipts'].append(event)
                    if path == '/receipt':
                        future = self.pending.pop((event['kind'], event['value']), None)
                    else:
                        future = self.pending_posts.pop(path, None)
                    if future is not None and not future.done():
                        future.set_result(event)
                    if path == '/challenge/verify':
                        is_trusted = event.get('isTrusted') is True
                        target_ok = event.get('target') == 'challenge-target'
                        ep_id = event.get('episodeId')
                        ep_ok = ep_id is not None and ep_id in self.active_episodes
                        if is_trusted and target_ok and ep_ok:
                            self.active_episodes.remove(ep_id)
                            response = {'verified': True, 'token': f'local-token-{ep_id}'}
                        else:
                            response = {'verified': False, 'error': 'untrusted_or_invalid_episode'}
                    elif path == '/challenge/misclick':
                        response = {'verified': False, 'misclick_logged': True,
                                    'count': event.get('count')}
                    elif path == '/challenge/turnstile-verify':
                        response = {'success': bool(event.get('token')),
                                    'sitekey': event.get('sitekey')}
                    else:
                        response = {'ok': True, 'received': event}
                    body, status, mime = json.dumps(response).encode(), '200 OK', 'application/json'
            else:
                body, status, mime = b'not found', '404 Not Found', 'text/plain'
            writer.write(f'HTTP/1.1 {status}\r\nContent-Type: {mime}\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n'.encode() + body)
            await writer.drain()
        finally:
            writer.close()
            await writer.wait_closed()


class PipeRequestError(RuntimeError):
    def __init__(self, response):
        super().__init__(f'CLI request failed: {response}')
        self.response = response


class BrowserPipe:
    def __init__(self, process):
        self.process = process
        self.transcript = []

    async def request(self, **request):
        request['id'] = len(self.transcript) + 1
        self.process.stdin.write(json.dumps(request).encode() + b'\n')
        await self.process.stdin.drain()
        line = await asyncio.wait_for(self.process.stdout.readline(), 30)
        if not line:
            raise RuntimeError('CLI pipe closed')
        response = json.loads(line)
        self.transcript.append({'request': request, 'response': response})
        if response.get('id') != request['id'] or response.get('ok') is not True:
            raise PipeRequestError(response)
        return response.get('result')


async def stop(process):
    if process is not None and process.returncode is None:
        process.terminate()
        try:
            await asyncio.wait_for(process.wait(), 10)
        except TimeoutError:
            process.kill()
            await process.wait()


async def baseline(args):
    async with AsyncExitStack() as cleanup:
        await _baseline(args, cleanup)


async def _baseline(args, cleanup):
    from macos_vm_build_transport import verify_baseline_cli
    if os.environ.get('MAHO_BASELINE_TART') != '1' or not str(args.cli).startswith('/Users/Shared/maho-workspace/'):
        raise ValueError('baseline browser launch requires the Tart guest command')
    verify_baseline_cli(args.cli)
    evidence = Path(args.evidence_dir)
    evidence.mkdir(parents=True, exist_ok=True)
    fixture = await FixtureServer().start()
    cleanup.push_async_callback(fixture.close)
    app = Path(args.app) / 'Contents/MacOS/Maho'
    browser = cli = None
    drain = None
    transcript = []
    # Short unique profile parent also keeps the Unix socket below sun_path limits.
    with tempfile.TemporaryDirectory(prefix='mb-') as temporary:
        profile = Path(temporary).resolve() / 'profile'
        socket = profile / 'maho.sock'
        ready = fixture.subscribe('ready', 'Maho ordinary form baseline')
        log = (evidence / 'browser.stderr.log').open('wb')
        try:
            browser = await asyncio.create_subprocess_exec(str(app), f'--user-data-dir={profile}', '--no-first-run', '--no-default-browser-check', '--maho-disable-login-gate', '--enable-logging=stderr', fixture.url, stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.PIPE)
            cleanup.push_async_callback(stop, browser)
            listening = asyncio.get_running_loop().create_future()
            stderr = browser.stderr
            assert stderr is not None
            async def read_log():
                async for line in stderr:
                    log.write(line)
                    if b'MCP: Listening on ' + str(socket).encode() in line and not listening.done():
                        listening.set_result(True)
                if not listening.done():
                    listening.set_exception(RuntimeError('browser exited before socket readiness'))
            drain = asyncio.create_task(read_log())
            await asyncio.wait_for(listening, 30)
            ready_receipt = await asyncio.wait_for(ready, 30)
            with (evidence / 'cli.stderr.log').open('wb') as cli_log:
                cli = await asyncio.create_subprocess_exec(str(args.cli), '--socket-path', str(socket), 'browser', 'pipe', stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=cli_log)
                cleanup.push_async_callback(stop, cli)
                pipe = BrowserPipe(cli)
                transcript = pipe.transcript
                samples = await measure(pipe, fixture, args.iterations)
                async def tool_call(capability, arguments):
                    process = await asyncio.create_subprocess_exec(
                        str(args.cli), '--socket-path', str(socket), '--json',
                        'tool', 'run', capability, '--args', json.dumps(arguments),
                        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
                    try:
                        stdout, stderr_output = await asyncio.wait_for(process.communicate(), 30)
                    finally:
                        await stop(process)
                    if process.returncode != 0:
                        raise RuntimeError(f'CLI {capability} failed: {stderr_output.decode()}')
                    result = json.loads(stdout)
                    transcript.append({'request': {'op': 'tool', 'capability': capability,
                                                   'args': arguments}, 'response': result})
                    return result

                async def key_press(key, modifiers):
                    await tool_call('input.key_press', {'key': key, 'modifiers': modifiers})

                keyboard_receipts = await verify_keyboard_followup(pipe, fixture, key_press)
                screenshot = await tool_call('capture.screenshot_full', {})
                image = screenshot.get('result', screenshot)
                png = base64.b64decode(image['data'], validate=True)
                if (png[:8] != b'\x89PNG\r\n\x1a\n' or len(png) < 24 or
                        int.from_bytes(png[16:20], 'big') <= 1 or
                        int.from_bytes(png[20:24], 'big') <= 1):
                    raise ValueError('real screenshot missing; placeholder is not evidence')
                report = {'scenario': 'baseline', 'status': 'MEASURED', 'samples': samples,
                          'keyboard_receipts': keyboard_receipts, 'screenshot': image,
                          'cli_sha256': sha256(args.cli), 'app_sha256': sha256(app), 'fixture_sha256': sha256(FIXTURE),
                          'machine': platform.uname()._asdict(), 'display': ready_receipt.get('display'),
                          'source': verify_baseline_cli(args.cli),
                          'internal_invocations': {'screenshot': None, 'model': None},
                          'driver_invocations': {'screenshot': sum(x['request']['op'] == 'screenshot' for x in transcript), 'model': sum(x['request']['op'] == 'model' for x in transcript)},
                          'limitations': ['Internal screenshot/model invocations unobserved; performance acceptance not established'],
                          'transcript': transcript, 'fixture_receipts': fixture.receipts}
                report['latency'] = {op: {'p50_ms': (sorted(x['elapsed_ms'] for x in rows)[24] + sorted(x['elapsed_ms'] for x in rows)[25]) / 2, 'p95_ms': sorted(x['elapsed_ms'] for x in rows)[47]} for op, rows in samples.items()}
                validate_baseline(report)
                Path(args.output).write_text(json.dumps(report, indent=2) + '\n')
        finally:
            try:
                await stop(cli)
            finally:
                try:
                    await stop(browser)
                finally:
                    try:
                        if drain is not None:
                            await drain
                    finally:
                        log.close()
                        await fixture.close()
            (evidence / 'cleanup.json').write_text(json.dumps({'cli_returncode': cli.returncode if cli else None, 'browser_returncode': browser.returncode if browser else None, 'fixture_closed': not fixture.server.is_serving()}) + '\n')
            (evidence / 'transcript.json').write_text(json.dumps(transcript, indent=2) + '\n')
            (evidence / 'fixture-receipts.json').write_text(json.dumps(fixture.receipts, indent=2) + '\n')


ALL_STEALTH_SCENARIOS = (
    'sentinel',
    'coordinates',
    'permission-denied',
    'native-input',
    'security',
    'model-loop',
    'recovery',
    'fingerprint',
    'challenge',
    'misclick',
    'telemetry',
    'turnstile-online',
    'model-provider-online',
)


class ScenarioBlocked(RuntimeError):
    pass


async def _wait_for_settled_fixture(pipe, route):
    """Confirm the document settled, preferring the browser's own wait.

    The fixture's trusted kind='ready' receipt is already awaited before this
    runs and is the authoritative readiness signal, because the page posts it
    only after its DOM and route scripts are installed. The pipe's 'wait' op
    calls the browser capability page.wait_for_selector, which this build does
    not expose (-32601 'Method not found: unknown tool'), so an absent
    capability is reported and skipped rather than failing every scenario.
    """
    selector = ('#baseline-input' if route == '/baseline' else
                '[data-maho-fixture="maho-security-fixtures"]')
    try:
        result = await pipe.request(op='wait', selector=selector, timeout_ms=10000)
    except PipeRequestError as error:
        detail = error.response.get('error', {})
        if (detail.get('rpc_code') == -32601 or
                detail.get('code') in ('method_not_found', 'unknown_op')):
            return {'settled_by': 'fixture-ready-receipt',
                    'wait_capability': 'unavailable',
                    'detail': detail.get('message')}
        raise
    if not isinstance(result, dict) or result.get('found') is not True:
        raise ScenarioBlocked(
            f'fixture {route} did not settle at selector {selector}')
    return {'settled_by': 'page.wait_for_selector', **result}


@asynccontextmanager
async def stealth_session(args, evidence, route='/baseline', ready=None, post_ready=None):
    """Launch the signed app and persistent CLI pipe used by real scenarios."""
    fixture = await FixtureServer(route).start()
    if ready is None:
        ready = ('ready', route.strip('/') or 'baseline')
    browser = cli = None
    drain = None
    log = None
    with tempfile.TemporaryDirectory(prefix='ms-') as temporary:
        profile = Path(temporary).resolve() / 'profile'
        socket = profile / 'maho.sock'
        pending_ready = fixture.subscribe(*ready) if ready else None
        pending_post = fixture.subscribe_post(post_ready) if post_ready else None
        try:
            log = (evidence / f'{route.strip("/").replace("/", "-") or "fixture"}.browser.stderr.log').open('wb')
            app = Path(args.app) / 'Contents/MacOS/Maho'
            browser = await asyncio.create_subprocess_exec(
                str(app), f'--user-data-dir={profile}', '--no-first-run',
                '--no-default-browser-check', '--maho-disable-login-gate',
                '--enable-logging=stderr', fixture.url,
                stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.PIPE)
            runtime = SCENARIO_RUNTIME.get()
            if runtime is not None:
                runtime['browser_executed'] = True
            listening = asyncio.get_running_loop().create_future()

            async def read_log():
                async for line in browser.stderr:
                    log.write(line)
                    if b'MCP: Listening on ' + str(socket).encode() in line and not listening.done():
                        listening.set_result(True)
                if not listening.done():
                    listening.set_exception(RuntimeError('browser exited before socket readiness'))

            drain = asyncio.create_task(read_log())
            try:
                await asyncio.wait_for(listening, 30)
            except TimeoutError as error:
                raise ScenarioBlocked('browser never opened its MCP socket') from error
            if pending_ready is not None:
                try:
                    await asyncio.wait_for(pending_ready, 30)
                except TimeoutError as error:
                    raise ScenarioBlocked(f'fixture {route} did not become ready') from error
            cli_log = (evidence / f'{route.strip("/").replace("/", "-") or "fixture"}.cli.stderr.log').open('wb')
            pipe = None
            try:
                cli = await asyncio.create_subprocess_exec(
                    str(args.cli), '--socket-path', str(socket), 'browser', 'pipe',
                    stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
                    stderr=cli_log)
                pipe = BrowserPipe(cli)
                await _wait_for_settled_fixture(pipe, route)
                yield fixture, pipe, pending_post
            finally:
                runtime = SCENARIO_RUNTIME.get()
                if runtime is not None and pipe is not None:
                    runtime.setdefault('actions', []).extend(pipe.transcript)
                if cli is not None:
                    await stop(cli)
                cli_log.close()
        finally:
            await stop(browser)
            if drain is not None:
                await drain
            if log is not None:
                log.close()
            await fixture.close()


async def _native_frame_token(pipe):
    """Refuse native dispatch until production can issue a real visual frame."""
    # page.accessibility_snapshot_v2 issues only snapshot_token, while screenshot
    # capture returns content_type + data. Production never sets
    # MahoMcpSession::last_visual_frame_; an unrecognized non-empty token takes
    # the browser's synthetic 1280x800 fallback path instead of proving that the
    # coordinates correspond to a captured frame.
    raise ScenarioBlocked(
        'browser exposes no visual frame capture: last_visual_frame_ is only set '
        'by SetVisualFrameForTesting, so any frame_token resolves to a synthetic '
        '1280x800 frame')


def _record_result(scenario, evidence, transcript, receipts, status, details=None, reason=None):
    runtime = SCENARIO_RUNTIME.get() or {'browser_executed': False, 'receipts': []}
    actions = runtime.get('actions', [])
    blockers = [reason] if status == 'BLOCKED' and reason else []
    result = {
        'status': status,
        'browser_executed': runtime['browser_executed'],
        'receipts': list(runtime['receipts']),
        'blockers': blockers,
        'actions': list(actions),
    }
    if reason:
        result['reason'] = reason
    if details is not None:
        result['details'] = details
    payload = {'scenario': scenario, 'status': status, 'timestamp': time.time(),
               **({'reason': reason} if reason else {}),
               **({'observation': details} if details is not None else {})}
    filename = 'provider-online-receipt.json' if scenario == 'model-provider-online' else f'{scenario}-receipt.json'
    (evidence / filename).write_text(json.dumps(payload, indent=2) + '\n')
    if scenario == 'turnstile-online':
        (evidence / 'turnstile-receipt.json').write_text(json.dumps(payload, indent=2) + '\n')
    if scenario == 'fingerprint':
        (evidence / 'fingerprint-audit.json').write_text(json.dumps(payload, indent=2) + '\n')

    # Export dedicated per-scenario transcript including CLI actions
    (evidence / f'{scenario}-transcript.json').write_text(json.dumps({
        'scenario': scenario,
        'status': status,
        'actions': actions,
        **({'reason': reason} if reason else {})
    }, indent=2) + '\n')

    transcript.append({'scenario': scenario, 'status': status,
                       'actions': actions,
                       **({'reason': reason} if reason else {})})
    return result


async def run_sentinel_scenario(args, evidence, cleanup, transcript, receipts):
    sentinel = 'S3NTINEL-maho-vault-9F4C'
    async with stealth_session(args, evidence, '/login') as (_, pipe, _):
        authorized = await pipe.request(op='observe', mode='full')
    async with stealth_session(args, evidence, '/prompt-injection') as (_, pipe, _):
        unauthorized = await pipe.request(op='observe', mode='full')
    passed = sentinel in json.dumps(authorized) and sentinel not in json.dumps(unauthorized)
    status = 'PASS' if passed else 'FAILED'
    return _record_result('sentinel', evidence, transcript, receipts, status,
                          {'authorized_page_observation': authorized,
                           'unauthorized_page_observation': unauthorized},
                          None if status == 'PASS' else
                          'sentinel was absent from login or leaked into the unauthorized observation')


async def _native_probe(args, evidence, expect_denied=False):
    async with stealth_session(args, evidence, ready=('ready', 'Maho ordinary form baseline')) as (fixture, pipe, _):
        observation = await pipe.request(op='observe', mode='full')
        frame_token = await _native_frame_token(pipe)
        receipt = fixture.subscribe('click', 1)
        try:
            response = await pipe.request(
                op='native', action={'kind': 'click', 'click_point_css': [60, 60],
                                     'target_rect_css': [0, 0, 200, 120]},
                frame_token=frame_token, lease='scoped')
        except PipeRequestError as error:
            if expect_denied:
                return {'error': error.response, 'observation': observation}
            detail = error.response.get('error', {})
            if detail.get('rpc_code') == -32011:
                raise ScenarioBlocked('macOS accessibility permission denied') from error
            raise
        if expect_denied:
            return {'unexpected_success': response}
        delivered = await asyncio.wait_for(receipt, 15)
        return {'rpc': response, 'receipt': delivered, 'observation': observation}


async def run_coordinates_scenario(args, evidence, cleanup, transcript, receipts):
    details = await _native_probe(args, evidence)
    receipt = details['receipt']
    status = 'PASS' if receipt.get('isTrusted') is True else 'FAILED'
    return _record_result('coordinates', evidence, transcript, receipts, status, details,
                          None if status == 'PASS' else 'native coordinate action produced no trusted target receipt')


async def run_permission_denied_scenario(args, evidence, cleanup, transcript, receipts):
    details = await _native_probe(args, evidence, expect_denied=True)
    error = details.get('error', {}).get('error', {})
    status = 'PASS' if error.get('rpc_code') == -32011 else 'FAILED'
    return _record_result('permission-denied', evidence, transcript, receipts, status, details,
                          None if status == 'PASS' else 'runtime did not return native permission RPC -32011')


async def run_native_input_scenario(args, evidence, cleanup, transcript, receipts):
    if platform.system() not in ('Darwin', 'Windows'):
        return _record_result('native-input', evidence, transcript, receipts, 'BLOCKED',
                              reason='native input unsupported on this platform')
    details = await _native_probe(args, evidence)
    status = 'PASS' if details['receipt'].get('isTrusted') is True else 'FAILED'
    return _record_result('native-input', evidence, transcript, receipts, status, details,
                          None if status == 'PASS' else 'native input RPC did not deliver a trusted event')


async def run_security_scenario(args, evidence, cleanup, transcript, receipts):
    async with stealth_session(args, evidence, '/cross-origin-iframe') as (_, pipe, _):
        observation = await pipe.request(op='observe', mode='full')
    return _record_result('security', evidence, transcript, receipts, 'BLOCKED',
                          {'rpc_observation': observation},
                          'one browser RPC cannot prove OTR separation and cross-origin restriction')


async def run_model_loop_scenario(args, evidence, cleanup, transcript, receipts):
    if not os.environ.get('MAHO_VISION_PROVIDER'):
        return _record_result('model-loop', evidence, transcript, receipts, 'BLOCKED',
                              reason='no configured vision provider')
    async with stealth_session(args, evidence, ready=('ready', 'Maho ordinary form baseline')) as (_, pipe, _):
        observation = await pipe.request(op='observe')
    return _record_result('model-loop', evidence, transcript, receipts, 'BLOCKED',
                          {'rpc_observation': observation},
                          'browser pipe exposes no model-loop operation or bounded-loop receipt')


async def run_recovery_scenario(args, evidence, cleanup, transcript, receipts):
    async with stealth_session(args, evidence, '/challenge') as (fixture, pipe, _):
        receipt = fixture.subscribe_post('/challenge/verify')
        settled = fixture.subscribe('state', 'challenge-cleared')
        action = await pipe.request(op='act', action={'kind': 'click'}, lease='scoped',
                                    locator={'css': '#challenge-target'})
        delivered = await asyncio.wait_for(receipt, 15)
        await asyncio.wait_for(settled, 15)
        observed = await pipe.request(op='observe', mode='full',
                                      since_snapshot_token='')
    cleared = delivered.get('isTrusted') is True and 'cleared' in json.dumps(observed)
    status = 'PASS' if cleared else 'FAILED'
    return _record_result('recovery', evidence, transcript, receipts, status,
                          {'action_rpc': action, 'receipt': delivered,
                           'postcondition_rpc': observed},
                          None if status == 'PASS' else 'real RPC did not observe cleared same-document postcondition')


async def run_fingerprint_scenario(args, evidence, cleanup, transcript, receipts):
    creepjs_dir = Path(__file__).parent / 'security_fixtures/creepjs'
    manifest = json.loads((creepjs_dir / 'manifest.json').read_text())
    for item in manifest.get('files', []):
        file_path = creepjs_dir / item['path']
        if not file_path.is_file() or sha256(file_path) != item['sha256']:
            raise ScenarioBlocked(f'CreepJS asset missing or hash mismatch: {item["path"]}')
    async with stealth_session(args, evidence, '/creepjs',
                               post_ready='/creepjs/results') as (fixture, pipe, pending):
        audit = await asyncio.wait_for(pending, 15)
        observation = await pipe.request(op='observe', mode='full')
    passed = audit.get('passed') is True and audit.get('webdriver') is False and audit.get('cdc_markers_found') is False
    status = 'PASS' if passed else 'FAILED'
    return _record_result('fingerprint', evidence, transcript, receipts, status,
                          {'page_receipt': audit, 'rpc_observation': observation,
                           'manifest_verified': True},
                          None if status == 'PASS' else 'real CreepJS fixture audit reported an automation marker')


async def run_challenge_scenario(args, evidence, cleanup, transcript, receipts):
    async with stealth_session(args, evidence, '/challenge') as (fixture, pipe, _):
        pending = fixture.subscribe_post('/challenge/verify')
        settled = fixture.subscribe('state', 'challenge-cleared')
        action = await pipe.request(op='act', action={'kind': 'click'}, lease='scoped',
                                    locator={'css': '#challenge-target'})
        delivered = await asyncio.wait_for(pending, 15)
        await asyncio.wait_for(settled, 15)
        observed = await pipe.request(op='observe', mode='full',
                                      since_snapshot_token='')
    passed = delivered.get('isTrusted') is True and 'cleared' in json.dumps(observed)
    status = 'PASS' if passed else 'FAILED'
    return _record_result('challenge', evidence, transcript, receipts, status,
                          {'action_rpc': action, 'receipt': delivered,
                           'postcondition_rpc': observed},
                          None if status == 'PASS' else 'trusted target receipt and cleared postcondition were not both observed')


async def run_misclick_scenario(args, evidence, cleanup, transcript, receipts):
    async with stealth_session(args, evidence, '/challenge') as (fixture, pipe, _):
        pending = fixture.subscribe_post('/challenge/misclick')
        action = await pipe.request(op='act', action={'kind': 'click'}, lease='scoped',
                                    locator={'css': '#challenge-misclick-zone'})
        delivered = await asyncio.wait_for(pending, 15)
        observed = await pipe.request(op='observe', mode='full',
                                      since_snapshot_token='')
    passed = delivered.get('count') == 1 and 'cleared' not in json.dumps(observed)
    status = 'PASS' if passed else 'FAILED'
    return _record_result('misclick', evidence, transcript, receipts, status,
                          {'action_rpc': action, 'receipt': delivered,
                           'postcondition_rpc': observed},
                          None if status == 'PASS' else 'misclick was not logged or challenge was incorrectly cleared')


def _telemetry_measurements(trace):
    moves = trace.get('moves') if isinstance(trace, dict) else None
    center = trace.get('target_center') if isinstance(trace, dict) else None
    downs = trace.get('pointerdowns') if isinstance(trace, dict) else None
    ups = trace.get('pointerups') if isinstance(trace, dict) else None
    if (not isinstance(moves, list) or len(moves) < 2 or
            not isinstance(center, dict) or
            not isinstance(downs, list) or not downs or
            not isinstance(ups, list) or not ups):
        raise ValueError('telemetry receipt omitted raw pointer evidence')
    try:
        points = [(float(item['x']), float(item['y']), float(item['t']))
                  for item in moves]
        center_x, center_y = float(center['x']), float(center['y'])
        down_timestamp = float(downs[0]['t'])
        up_timestamp = float(ups[-1]['t'])
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError('telemetry receipt contains invalid pointer evidence') from error
    if not all(math.isfinite(value) for point in points for value in point):
        raise ValueError('telemetry receipt contains non-finite pointer evidence')
    if not all(math.isfinite(value) for value in
               (center_x, center_y, down_timestamp, up_timestamp)):
        raise ValueError('telemetry receipt contains non-finite pointer evidence')

    first_x, first_y, _ = points[0]
    last_x, last_y, _ = points[-1]
    line_x, line_y = last_x - first_x, last_y - first_y
    line_length = math.hypot(line_x, line_y)
    jitter = max(
        math.hypot(x - first_x, y - first_y) if line_length == 0 else
        abs(line_x * (first_y - y) - (first_x - x) * line_y) / line_length
        for x, y, _ in points)
    approach_x, approach_y = center_x - first_x, center_y - first_y
    approach_length = math.hypot(approach_x, approach_y)
    overshoot = max((
        ((x - center_x) * approach_x + (y - center_y) * approach_y) /
        approach_length if approach_length else 0.0
        for x, y, _ in points), default=0.0)
    return {
        'jitter_css_px': jitter,
        'overshoot_px': max(0.0, overshoot),
        'dwell_ms': up_timestamp - down_timestamp,
        'moves_monotonic': all(points[index][2] >= points[index - 1][2]
                               for index in range(1, len(points))),
    }


def _keyboard_telemetry_valid(sequence):
    if not isinstance(sequence, list) or not sequence:
        return False, False
    active = {}
    ordered = True
    modifiers_preserved = True
    previous = -math.inf
    modifier_names = ('altKey', 'ctrlKey', 'metaKey', 'shiftKey')
    for event in sequence:
        if not isinstance(event, dict):
            return False, False
        timestamp = event.get('t')
        if (isinstance(timestamp, bool) or not isinstance(timestamp, (int, float)) or
                not math.isfinite(timestamp)):
            return False, False
        ordered = ordered and timestamp >= previous
        previous = timestamp
        identity = event.get('code') or event.get('key')
        if (not isinstance(identity, str) or not identity or
                not isinstance(event.get('key'), str) or
                not isinstance(event.get('code'), str) or
                any(not isinstance(event.get(name), bool) for name in modifier_names)):
            modifiers_preserved = False
            continue
        if event.get('type') == 'keydown':
            if identity in active:
                modifiers_preserved = False
            active[identity] = event
        elif event.get('type') == 'keyup':
            down = active.pop(identity, None)
            if down is None:
                modifiers_preserved = False
            elif identity not in ('ShiftLeft', 'ShiftRight', 'ControlLeft',
                                  'ControlRight', 'AltLeft', 'AltRight',
                                  'MetaLeft', 'MetaRight'):
                modifiers_preserved = modifiers_preserved and all(
                    down[name] == event[name] for name in modifier_names)
        else:
            modifiers_preserved = False
    return ordered, modifiers_preserved and not active


async def run_telemetry_scenario(args, evidence, cleanup, transcript, receipts):
    if platform.system() not in ('Darwin', 'Windows'):
        raise ScenarioBlocked('native input unsupported on this platform')
    async with stealth_session(args, evidence, '/telemetry',
                               post_ready='/telemetry/trace') as (_, pipe, pending):
        observation = await pipe.request(op='observe', mode='full')
        frame_token = await _native_frame_token(pipe)

        native_receipts = []
        for index, point in enumerate(([120, 160], [203, 160], [200, 160])):
            try:
                native_receipts.append(await pipe.request(
                    op='native', action={
                        'kind': 'click', 'click_point_css': point,
                        'target_rect_css': [100, 100, 200, 120],
                    }, frame_token=frame_token, lease='scoped'))
            except PipeRequestError as error:
                if error.response.get('error', {}).get('rpc_code') == -32011:
                    raise ScenarioBlocked('macOS accessibility permission denied') from error
                raise
            if index == 0:
                # Dwell is the observed first-down to final-up interval. Time is
                # deliberately part of this scenario's bounded behavior.
                await asyncio.sleep(0.1)

        try:
            keyboard_rpc = await pipe.request(
                op='act', action={'kind': 'type'}, lease='scoped',
                locator={'css': '#telemetry-input'}, text='a')
        except PipeRequestError as error:
            detail = error.response.get('error', {})
            if (detail.get('rpc_code') == -32011 or
                    detail.get('code') in ('unsupported', 'not_implemented')):
                raise ScenarioBlocked(
                    detail.get('message', 'native keyboard input unavailable')) from error
            raise
        try:
            delivered = await asyncio.wait_for(pending, 15)
        except TimeoutError as error:
            raise ValueError('telemetry fixture did not deliver a measured trace') from error

    measured = _telemetry_measurements(delivered)
    keyboard_ordered, modifiers_preserved = _keyboard_telemetry_valid(
        delivered.get('keyboard'))
    reported_matches = all(
        isinstance(delivered.get(name), (int, float)) and
        not isinstance(delivered.get(name), bool) and
        math.isclose(float(delivered[name]), measured[name], rel_tol=1e-9,
                     abs_tol=1e-6)
        for name in ('jitter_css_px', 'overshoot_px', 'dwell_ms'))
    passed = (
        reported_matches and 0 <= measured['jitter_css_px'] <= 0.8 and
        2.0 <= measured['overshoot_px'] <= 4.0 and
        80 <= measured['dwell_ms'] <= 220 and
        measured['moves_monotonic'] and keyboard_ordered and modifiers_preserved)
    details = {
        'observation': observation,
        'native_rpcs': native_receipts,
        'keyboard_rpc': keyboard_rpc,
        'trace': delivered,
        'measured': measured,
        'keyboard_ordered': keyboard_ordered,
        'modifiers_preserved': modifiers_preserved,
        'fixture_metrics_match_raw_trace': reported_matches,
    }
    status = 'PASS' if passed else 'FAILED'
    return _record_result('telemetry', evidence, transcript, receipts, status,
                          details, None if passed else
                          'measured telemetry violated bounded contract')


async def run_turnstile_online_scenario(args, evidence, cleanup, transcript, receipts):
    if os.environ.get('MAHO_ALLOW_ONLINE') != '1':
        return _record_result('turnstile-online', evidence, transcript, receipts, 'BLOCKED',
                              reason='network unavailable: MAHO_ALLOW_ONLINE is not 1')
    async with stealth_session(args, evidence, '/challenge/turnstile') as (_, pipe, _):
        observation = await pipe.request(op='observe', mode='full')
    return _record_result('turnstile-online', evidence, transcript, receipts, 'BLOCKED',
                          {'rpc_observation': observation},
                          'no Turnstile callback or Siteverify response is observable through the browser pipe')


async def run_model_provider_online_scenario(args, evidence, cleanup, transcript, receipts):
    if os.environ.get('MAHO_ALLOW_ONLINE') != '1':
        return _record_result('model-provider-online', evidence, transcript, receipts, 'BLOCKED',
                              reason='network unavailable: MAHO_ALLOW_ONLINE is not 1')
    if not os.environ.get('MAHO_VISION_PROVIDER'):
        return _record_result('model-provider-online', evidence, transcript, receipts, 'BLOCKED',
                              reason='no configured vision provider')
    async with stealth_session(args, evidence, ready=('ready', 'Maho ordinary form baseline')) as (_, pipe, _):
        observation = await pipe.request(op='observe')
    return _record_result('model-provider-online', evidence, transcript, receipts, 'BLOCKED',
                          {'rpc_observation': observation},
                          'browser pipe exposes no online model-provider response')


SCENARIO_REGISTRY = {
    'sentinel': run_sentinel_scenario,
    'coordinates': run_coordinates_scenario,
    'permission-denied': run_permission_denied_scenario,
    'native-input': run_native_input_scenario,
    'security': run_security_scenario,
    'model-loop': run_model_loop_scenario,
    'recovery': run_recovery_scenario,
    'fingerprint': run_fingerprint_scenario,
    'challenge': run_challenge_scenario,
    'misclick': run_misclick_scenario,
    'telemetry': run_telemetry_scenario,
    'turnstile-online': run_turnstile_online_scenario,
    'model-provider-online': run_model_provider_online_scenario,
}


async def run(args):
    async with AsyncExitStack() as cleanup:
        return await _run(args, cleanup)


async def _run(args, cleanup):
    from macos_vm_build_transport import verify_baseline_cli
    if not str(args.cli).startswith('/Users/Shared/maho-workspace/'):
        raise ValueError('stealth browser launch requires the Tart guest command')
    evidence = Path(args.evidence_dir)
    evidence.mkdir(parents=True, exist_ok=True)
    output_path = Path(args.output) if args.output else evidence / 'stealth-summary.json'
    output_path.parent.mkdir(parents=True, exist_ok=True)

    cli = Path(args.cli)
    app = Path(args.app) / 'Contents/MacOS/Maho'
    missing = None
    if not cli.is_file():
        missing = f'CLI executable missing: {cli}'
    elif not app.is_file():
        missing = f'app executable missing: {app}'
    else:
        verify_baseline_cli(cli)

    scenarios = args.scenario if isinstance(args.scenario, list) else [args.scenario]
    results = {}
    transcript = []
    fixture_receipts = []

    for scenario_id in scenarios:
        handler = SCENARIO_REGISTRY.get(scenario_id)
        if handler is None:
            raise ValueError(f'unknown stealth scenario: {scenario_id}')
        runtime_token = SCENARIO_RUNTIME.set(
            {'browser_executed': False, 'receipts': []})
        try:
            if missing:
                scenario_result = _record_result(
                    scenario_id, evidence, transcript, fixture_receipts,
                    'BLOCKED', reason=missing)
            else:
                try:
                    scenario_result = await handler(
                        args, evidence, cleanup, transcript, fixture_receipts)
                except ScenarioBlocked as error:
                    scenario_result = _record_result(
                        scenario_id, evidence, transcript, fixture_receipts,
                        'BLOCKED', reason=str(error))
                except (OSError, TimeoutError, RuntimeError, ValueError) as error:
                    scenario_result = _record_result(
                        scenario_id, evidence, transcript, fixture_receipts,
                        'FAILED', reason=str(error))
            results[scenario_id] = scenario_result
            fixture_receipts.extend(
                {'scenario': scenario_id, 'receipt': receipt}
                for receipt in scenario_result['receipts'])
        finally:
            SCENARIO_RUNTIME.reset(runtime_token)

    all_pass = all(r.get('status') == 'PASS' for r in results.values())
    has_fail = any(r.get('status') not in ('PASS', 'BLOCKED') for r in results.values())
    has_blocked = any(r.get('status') == 'BLOCKED' for r in results.values())

    if has_fail:
        overall_status = 'FAIL'
    elif has_blocked and not all_pass:
        overall_status = 'BLOCKED'
    else:
        overall_status = 'PASS'

    observed_receipts = [receipt for result in results.values()
                         for receipt in result['receipts']]
    blockers = [blocker for result in results.values()
                for blocker in result['blockers']]
    summary = {
        'suite': 'stealth',
        'scenario': scenarios[0],
        'scenarios': scenarios,
        'status': overall_status,
        'browser_executed': any(result['browser_executed']
                                for result in results.values()),
        'receipts': observed_receipts,
        'blockers': blockers,
        'results': results,
        'cli_sha256': sha256(cli) if cli.is_file() else None,
        'app_sha256': sha256(app) if app.is_file() else None,
        'machine': platform.uname()._asdict(),
        'timestamp': time.time(),
    }
    output_path.write_text(json.dumps(summary, indent=2) + '\n')
    (evidence / 'cleanup.json').write_text(json.dumps({'cleaned': True, 'scenarios': scenarios}) + '\n')
    (evidence / 'transcript.json').write_text(json.dumps(transcript, indent=2) + '\n')
    (evidence / 'fixture-receipts.json').write_text(json.dumps(fixture_receipts, indent=2) + '\n')
    return summary


def compute_percentiles(rows):
    if len(rows) != 50:
        raise ValueError(f'exactly 50 samples required, got {len(rows)}')
    times = []
    for r in rows:
        if not isinstance(r, dict):
            raise ValueError(f'sample row must be an object, got {type(r)}')
        receipt = r.get('receipt')
        if not isinstance(receipt, dict):
            raise ValueError('missing action receipt in sample')
        if receipt.get('isTrusted') is not True:
            raise ValueError('untrusted sample receipt detected')
        t = r.get('elapsed_ms')
        if isinstance(t, bool) or not isinstance(t, (int, float)) or not math.isfinite(t) or t <= 0:
            raise ValueError(f'invalid sample timing: {t}')
        times.append(t)
    times.sort()
    p50 = (times[24] + times[25]) / 2.0
    p95 = times[47]
    return p50, p95


def compare_performance(before_report, after_report, max_regression_percent=3.0):
    if not isinstance(before_report, dict) or not isinstance(after_report, dict):
        raise ValueError('before and after reports must be objects')
    if before_report.get('status') != 'PASS':
        raise ValueError(f"before report status is not PASS: {before_report.get('status')}")
    if after_report.get('status') != 'PASS':
        raise ValueError(f"after report status is not PASS: {after_report.get('status')}")

    # Validate matching fixture/display metadata
    before_fixture = before_report.get('fixture')
    after_fixture = after_report.get('fixture')
    if before_fixture is not None or after_fixture is not None:
        if before_fixture != after_fixture:
            raise ValueError(f'fixture metadata mismatch: {before_fixture} != {after_fixture}')
    before_display = before_report.get('display')
    after_display = after_report.get('display')
    if before_display is not None or after_display is not None:
        if before_display != after_display:
            raise ValueError(f'display metadata mismatch: {before_display} != {after_display}')

    before_samples = before_report.get('samples', {})
    after_samples = after_report.get('samples', {})
    if set(before_samples) != set(OPERATIONS) or set(after_samples) != set(OPERATIONS):
        raise ValueError('operation set mismatch between before and after reports')

    # Validate baseline internal counters
    before_internal = before_report.get('internal_invocations')
    if not isinstance(before_internal, dict):
        raise ValueError('missing internal_invocations in baseline report')
    b_screenshot = before_internal.get('screenshot')
    b_model = before_internal.get('model')
    if b_screenshot is None or isinstance(b_screenshot, bool) or type(b_screenshot) is not int or b_screenshot != 0:
        raise ValueError(f'unverified, missing, boolean, or nonzero baseline screenshot invocations: {b_screenshot}')
    if b_model is None or isinstance(b_model, bool) or type(b_model) is not int or b_model != 0:
        raise ValueError(f'unverified, missing, boolean, or nonzero baseline model invocations: {b_model}')

    # Validate candidate internal counters
    after_internal = after_report.get('internal_invocations')
    if not isinstance(after_internal, dict):
        raise ValueError('missing internal_invocations in candidate report')
    screenshot_count = after_internal.get('screenshot')
    model_count = after_internal.get('model')
    if screenshot_count is None or isinstance(screenshot_count, bool) or type(screenshot_count) is not int or screenshot_count != 0:
        raise ValueError(f'unverified, missing, boolean, or nonzero internal screenshot invocations: {screenshot_count}')
    if model_count is None or isinstance(model_count, bool) or type(model_count) is not int or model_count != 0:
        raise ValueError(f'unverified, missing, boolean, or nonzero internal model invocations: {model_count}')

    comparison = {
        'status': 'PASS',
        'max_p50_regression_percent': max_regression_percent,
        'operations': {},
        'timestamp': time.time(),
    }
    for op in OPERATIONS:
        before_p50, before_p95 = compute_percentiles(before_samples[op])
        after_p50, after_p95 = compute_percentiles(after_samples[op])
        if before_p50 <= 0:
            raise ValueError(f'invalid zero before p50 for {op}')
        regression_pct = 100.0 * (after_p50 / before_p50 - 1.0)
        if round(regression_pct, 6) > max_regression_percent:
            comparison['status'] = 'FAIL'
            raise ValueError(f'p50 regression for {op} exceeded {max_regression_percent}%: {regression_pct:.2f}% (before={before_p50:.2f}ms, after={after_p50:.2f}ms)')
        comparison['operations'][op] = {
            'before_p50_ms': before_p50,
            'before_p95_ms': before_p95,
            'after_p50_ms': after_p50,
            'after_p95_ms': after_p95,
            'regression_percent': round(regression_pct, 2),
        }
    return comparison


def compare_cli(args):
    before_path = Path(args.before)
    after_path = Path(args.after)
    if not before_path.is_file():
        raise FileNotFoundError(f'before report not found: {before_path}')
    if not after_path.is_file():
        raise FileNotFoundError(f'after report not found: {after_path}')
    before_report = json.loads(before_path.read_text(encoding='utf-8'))
    after_report = json.loads(after_path.read_text(encoding='utf-8'))
    result = compare_performance(before_report, after_report, args.max_p50_regression_percent)
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return result


def build_argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    stage = sub.add_parser('stage-cli')
    stage.add_argument('--workspace', type=Path, required=True)
    capture = sub.add_parser('baseline')
    capture.add_argument('--cli', type=Path, required=True)
    capture.add_argument('--app', type=Path, required=True)
    capture.add_argument('--evidence-dir', required=True)
    capture.add_argument('--iterations', type=int, choices=[50], default=50)
    capture.add_argument('--output', required=True)
    run_cmd = sub.add_parser('run')
    run_cmd.add_argument('--scenario', action='append', choices=ALL_STEALTH_SCENARIOS, required=True)
    run_cmd.add_argument('--cli', type=Path, required=True)
    run_cmd.add_argument('--app', type=Path, required=True)
    run_cmd.add_argument('--evidence-dir', required=True)
    run_cmd.add_argument('--output', default=None)
    comp = sub.add_parser('compare')
    comp.add_argument('--before', type=Path, required=True)
    comp.add_argument('--after', type=Path, required=True)
    comp.add_argument('--max-p50-regression-percent', type=float, default=3.0)
    comp.add_argument('--output', default=None)
    return parser


def main():
    parser = build_argument_parser()
    args = parser.parse_args()
    if args.command == 'stage-cli':
        from macos_vm_build_transport import stage_baseline_cli
        print(stage_baseline_cli(args.workspace))
    elif args.command == 'baseline':
        asyncio.run(baseline(args))
    elif args.command == 'run':
        summary = asyncio.run(run(args))
        if not summary.get('browser_executed'):
            return 2
        status = summary.get('status')
        if status == 'PASS':
            return 0
        if status == 'BLOCKED':
            return 3
        return 1
    elif args.command == 'compare':
        compare_cli(args)


if __name__ == '__main__':
    raise SystemExit(main())
