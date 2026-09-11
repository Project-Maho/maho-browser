"""Behavioral baseline contracts; no browser or timing-luck dependencies."""
import asyncio
import copy
import json
from pathlib import Path, PurePosixPath
from types import SimpleNamespace
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch, AsyncMock, MagicMock
from argparse import Namespace

import hybrid_visual_pipeline as driver
import macos_vm_e2e as runner
import macos_vm_guest_suites as guest
import macos_vm_build_transport as transport
from macos_vm_tart import TartConfig


class BaselineTests(unittest.IsolatedAsyncioTestCase):
    async def test_browser_profile_resolves_temporary_directory_alias(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            real = root / 'real'
            real.mkdir()
            alias = root / 'alias'
            alias.symlink_to(real, target_is_directory=True)
            args = Namespace(cli=Path('/Users/Shared/maho-workspace/cli'),
                             app=Path('/app'), evidence_dir=root / 'evidence')

            async def capture_launch(*argv, **kwargs):
                self.assertIn(f'--user-data-dir={real.resolve() / "profile"}', argv)
                raise OSError('launch intercepted before browser execution')

            directory = MagicMock()
            directory.__enter__.return_value = str(alias)
            with patch.dict(driver.os.environ, {'MAHO_BASELINE_TART': '1'}), \
                 patch.object(transport, 'verify_baseline_cli'), \
                 patch.object(driver.tempfile, 'TemporaryDirectory', return_value=directory), \
                 patch.object(driver.asyncio, 'create_subprocess_exec', side_effect=capture_launch):
                with self.assertRaisesRegex(OSError, 'launch intercepted'):
                    await driver.baseline(args)

    async def test_log_creation_failure_closes_started_fixture(self):
        fixture = driver.FixtureServer()
        original = driver.FixtureServer.start
        async def start(_self):
            return await original(fixture)
        with tempfile.TemporaryDirectory() as tmp:
            args = Namespace(cli=Path('/Users/Shared/maho-workspace/cli'), app=Path('/app'), evidence_dir=tmp)
            with patch.dict(driver.os.environ, {'MAHO_BASELINE_TART': '1'}), patch.object(transport, 'verify_baseline_cli'), patch.object(driver.FixtureServer, 'start', start), patch.object(Path, 'open', side_effect=OSError('disk full')):
                # Read fixture bytes before injecting the artifact-open fault.
                with patch.object(Path, 'read_bytes', return_value=b'fixture'):
                    with self.assertRaises(OSError):
                        await driver.baseline(args)
            self.assertFalse(fixture.server.is_serving())

    async def test_loopback_receipt_and_cleanup(self):
        fixture = await driver.FixtureServer().start()
        port = fixture.server.sockets[0].getsockname()[1]
        subscribed = fixture.subscribe('click', 1)
        try:
            reader, writer = await asyncio.open_connection('127.0.0.1', port)
            body = json.dumps({'kind': 'click', 'value': 1, 'isTrusted': True}).encode()
            writer.write(f'POST /receipt HTTP/1.1\r\nContent-Length: {len(body)}\r\n\r\n'.encode() + body)
            await writer.drain()
            receipt = await asyncio.wait_for(subscribed, 2)
            self.assertTrue(receipt['isTrusted'])
            self.assertIn(b'200 OK', await asyncio.wait_for(reader.read(), 2))
            writer.close()
            await writer.wait_closed()
        finally:
            await fixture.close()
        self.assertFalse(fixture.server.is_serving())
        self.assertEqual(fixture.server.sockets, ())

    async def test_large_receipt_is_accepted_up_to_hard_cap(self):
        fixture = await driver.FixtureServer().start()
        port = fixture.server.sockets[0].getsockname()[1]
        pending = fixture.subscribe_post('/creepjs/results')
        try:
            reader, writer = await asyncio.open_connection('127.0.0.1', port)
            body = json.dumps({'passed': True, 'audit': 'x' * 70000}).encode()
            writer.write(
                f'POST /creepjs/results HTTP/1.1\r\nContent-Length: {len(body)}\r\n\r\n'.encode()
                + body)
            await writer.drain()
            receipt = await asyncio.wait_for(pending, 2)
            self.assertEqual(len(receipt['audit']), 70000)
            self.assertIn(b'200 OK', await asyncio.wait_for(reader.read(), 2))
            writer.close()
            await writer.wait_closed()
        finally:
            await fixture.close()

    async def test_oversize_or_malformed_receipt_returns_error_and_blocker(self):
        for content_length, body, expected_status in (
                (driver.MAX_RECEIPT_BYTES + 1, b'', b'413 Payload Too Large'),
                (1, b'{', b'400 Bad Request')):
            with self.subTest(content_length=content_length):
                fixture = await driver.FixtureServer().start()
                port = fixture.server.sockets[0].getsockname()[1]
                pending = fixture.subscribe_post('/creepjs/results')
                try:
                    reader, writer = await asyncio.open_connection(
                        '127.0.0.1', port)
                    writer.write(
                        f'POST /creepjs/results HTTP/1.1\r\nContent-Length: {content_length}\r\n\r\n'.encode()
                        + body)
                    await writer.drain()
                    response = await asyncio.wait_for(reader.read(), 2)
                    self.assertIn(expected_status, response)
                    self.assertEqual(
                        fixture.receipts[-1]['value'],
                        'malformed-or-oversize-post')
                    with self.assertRaisesRegex(
                            driver.ScenarioBlocked,
                            'fixture rejected /creepjs/results'):
                        await pending
                    writer.close()
                    await writer.wait_closed()
                finally:
                    await fixture.close()

    async def test_unused_connection_closes_without_request_error(self):
        fixture = driver.FixtureServer()
        reader = asyncio.StreamReader()
        writer = MagicMock(spec=asyncio.StreamWriter)
        writer.wait_closed = AsyncMock()
        reader.feed_eof()

        await fixture.handle(reader, writer)

        writer.close.assert_called_once()
        writer.wait_closed.assert_awaited_once()
        writer.write.assert_not_called()
        self.assertEqual(fixture.receipts, [])

    async def test_partial_request_eof_is_not_an_unused_connection(self):
        fixture = driver.FixtureServer()
        reader = asyncio.StreamReader()
        writer = MagicMock(spec=asyncio.StreamWriter)
        writer.wait_closed = AsyncMock()
        reader.feed_data(b'GET /baseline HTTP/1.1\r\n')
        reader.feed_eof()

        with self.assertRaises(asyncio.IncompleteReadError):
            await fixture.handle(reader, writer)

        writer.close.assert_called_once()
        writer.wait_closed.assert_awaited_once()
        writer.write.assert_not_called()

    async def test_missing_fixture_fails_before_listen(self):
        with patch.object(driver, 'FIXTURE', Path('/nonexistent/maho-baseline.html')):
            with self.assertRaises(FileNotFoundError):
                await driver.FixtureServer().start()

    async def test_measure_subscribes_before_action_and_requires_real_receipts(self):
        class Fixture:
            pending = None
            def subscribe(self, kind, value):
                self.pending = (kind, value, asyncio.get_running_loop().create_future())
                return self.pending[2]
        fixture = Fixture()
        class Pipe:
            async def request(self, **request):
                if request['op'] == 'act':
                    self_outer.assertEqual(request['lease'], 'scoped')
                    assert fixture.pending is not None
                    kind, value, future = fixture.pending
                    self_outer.assertFalse(future.done())
                    future.set_result({'kind': kind, 'value': value, 'isTrusted': True})
                    fixture.pending = None
                return {'verified': 'dispatched'}
        self_outer = self
        report = await driver.measure(Pipe(), fixture, iterations=50)
        self.assertEqual(set(report), {'observe', 'act_click', 'act_type'})
        for samples in report.values():
            self.assertEqual(len(samples), 50)
        self.assertEqual(report['act_type'][49]['receipt']['value'], 'baseline-49')
        self.assertTrue(report['act_click'][0]['receipt']['isTrusted'])

    async def test_dispatched_without_receipt_fails(self):
        class Fixture:
            def subscribe(self, kind, value):
                future = asyncio.get_running_loop().create_future()
                future.set_exception(TimeoutError('no input receipt'))
                return future
        class Pipe:
            async def request(self, **request):
                return {'verified': 'dispatched'}
        with self.assertRaises(TimeoutError):
            await driver.measure(Pipe(), Fixture())

    async def test_keyboard_followup_requires_selection_and_cancel_receipts(self):
        for broken in (None, 'selection', 'cancellation', 'character', 'untrusted'):
            with self.subTest(broken=broken):
                fixture = driver.FixtureServer()
                issued = []

                async def request(**request):
                    issued.append(request)
                    value = request['text']
                    subscribed = fixture.pending[('type', value)]
                    self.assertFalse(subscribed.done())
                    subscribed.set_result({'kind': 'type', 'value': value, 'isTrusted': True})

                async def key_press(key, modifiers):
                    if key == 'ArrowRight':
                        fixture.pending[('caret', 1)].set_result({'kind': 'caret',
                            'value': 1, 'isTrusted': True, 'selection': [14, 14]})
                        return
                    self.assertEqual((key, modifiers), ('a', ['meta']))
                    number = len([key for key in fixture.pending if key[0] == 'shortcut'])
                    subscribed = fixture.pending[('shortcut', number)]
                    self.assertFalse(subscribed.done())
                    before = [14, 14]
                    selected = [0, 14] if number == 1 else before
                    if broken == 'selection' and number == 1:
                        selected = before
                    if broken == 'cancellation' and number == 2:
                        selected = [0, 14]
                    subscribed.set_result({'kind': 'shortcut', 'value': number,
                        'isTrusted': broken != 'untrusted', 'text': 'keyboard-check',
                        'before_selection': before, 'selection': selected,
                        'before_input_events': 15, 'input_events': 16 if broken == 'character' else 15})

                pipe = SimpleNamespace(request=request)
                if broken:
                    with self.assertRaises(ValueError):
                        await driver.verify_keyboard_followup(pipe, fixture, key_press)
                else:
                    receipts = await driver.verify_keyboard_followup(pipe, fixture, key_press)
                    self.assertEqual([item['value'] for item in receipts], ['', 'keyboard-check', 1, 1, 2])
                    self.assertEqual([item['text'] for item in issued], ['', 'keyboard-check'])

    def test_missing_fixture_and_fake_success_rejected(self):
        for report in ({}, {'status': 'SUCCESS', 'samples': {}, 'fixture_sha256': None}):
            with self.assertRaises(ValueError):
                driver.validate_baseline(report)


class RunnerBaselineTests(unittest.TestCase):
    def test_guest_evidence_export_before_validation_on_success_and_failure(self):
        names = ('cleanup.json', 'transcript.json', 'fixture-receipts.json',
                 'browser.stderr.log', 'cli.stderr.log')
        valid_summary = {
            'scenario': 'baseline', 'status': 'MEASURED',
            'cli_sha256': 'a' * 64, 'app_sha256': 'b' * 64, 'fixture_sha256': 'c' * 64,
            'internal_invocations': {'screenshot': None, 'model': None},
            'display': {'screen_width': 1920, 'screen_height': 1080,
                        'viewport_width': 1280, 'viewport_height': 720,
                        'device_pixel_ratio': 2},
            'samples': {op: [{'elapsed_ms': 1, 'receipt': {'kind': op.removeprefix('act_'),
                       'value': i + 1 if op == 'act_click' else f'baseline-{i}', 'isTrusted': True}}
                       for i in range(50)] for op in driver.OPERATIONS}
        }
        cases = (
            (0, True, None, False),                      # success
            (7, False, None, False),                     # failed run, absent summary, cleanup receipt survives
            (7, True, 'transcript.json', False),         # failed run with broken copy
            (0, True, None, True),                       # returncode 0, missing cleanup receipt -> must refuse
            (7, True, None, False),                      # returncode 7, all evidence+summary exists -> must refuse with original 7
        )
        for returncode, summary_present, broken_copy, missing_cleanup in cases:
            with self.subTest(returncode=returncode, summary=summary_present, broken=broken_copy, missing_cleanup=missing_cleanup), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                remote = root / 'guest'
                local = root / 'host'
                remote.mkdir()
                local.mkdir()
                for name in names:
                    if not (missing_cleanup and name == 'cleanup.json'):
                        (remote / name).write_text(f'evidence:{name}')
                (remote / 'private.env').write_text('must not export')
                if summary_present:
                    (remote / 'baseline-latency.json').write_text(json.dumps(valid_summary))
                copied = []
                ledger = []

                def execute(command, *args):
                    result = SimpleNamespace(returncode=returncode, stdout='guest stdout', stderr='guest failure')
                    return guest.CapturedCommandResult(command, returncode, result.stdout, result.stderr), result

                def run_command(services, argv, **kwargs):
                    tokens = shlex.split(argv[-1])
                    exists = Path(tokens[-1]).is_file() if tokens[:2] == ['test', '-f'] else True
                    return SimpleNamespace(returncode=0 if exists else 1, succeeded=exists)

                def copy_file(config, ip, source, target, *args):
                    copied.append(Path(source).name)
                    if not Path(source).exists() or Path(source).name == broken_copy:
                        raise runner._StageFailure(runner._TerminalStatus.SUITE_FAILED,
                                                   f'failed to copy guest result {source}')
                    target.write_bytes(Path(source).read_bytes())

                validator = MagicMock(side_effect=guest.validate_hybrid_visual_result)

                with patch.object(runner, '_run_command', side_effect=run_command), \
                     patch.object(runner, '_execute_guest_command', side_effect=execute), \
                     patch.object(runner, '_copy_guest_file', side_effect=copy_file), \
                     patch.object(runner, '_validate_hybrid_visual_result', validator):
                    config = TartConfig(base_image='base', golden_image='golden',
                                        preparation_image='preparation', state_path=root / 'state',
                                        private_key=root / 'key', public_key=root / 'key.pub')
                    args = (runner.load_manifest(), 'hybrid-visual', PurePosixPath(remote),
                            config, '192.0.2.1', ('ssh', 'guest'), runner.LifecycleServices(),
                            runner._CancellationToken(), local, ledger.append)
                    if returncode or missing_cleanup:
                        with self.assertRaises(runner._StageFailure) as raised:
                            runner._run_selected_suites(*args)
                        if returncode:
                            self.assertIn(f'baseline guest returncode={returncode}', str(raised.exception))
                        if missing_cleanup:
                            self.assertIn('missing cleanup receipt', str(raised.exception))
                        if broken_copy:
                            self.assertIn('failed to copy guest result', str(raised.exception))
                        self.assertEqual(ledger[-1][0].returncode, returncode)
                        self.assertEqual(ledger[-1][0].status, 'failed')
                        validator.assert_not_called()
                    else:
                        runner._run_selected_suites(*args)
                        self.assertEqual(ledger[-1][0].status, 'success')
                        validator.assert_called_once()
                if not missing_cleanup:
                    self.assertEqual((local / 'cleanup.json').read_text(), 'evidence:cleanup.json')
                    self.assertIn(str(local / 'cleanup.json'), ledger[-1][0].artifact_paths)
                if not (returncode or missing_cleanup or broken_copy):
                    for name in names:
                        self.assertEqual((local / name).read_text(), f'evidence:{name}')
                        self.assertIn(str(local / name), ledger[-1][0].artifact_paths)
                    self.assertEqual(set(copied), {*names, 'baseline-latency.json'})
                    self.assertFalse((local / 'private.env').exists())
                self.assertEqual((local / 'hybrid-visual.stderr.log').read_text(), 'guest failure')

    def test_guest_preflight_checks_python_and_staged_hash(self):
        manifest = runner.load_manifest()
        from dataclasses import replace
        manifest = replace(manifest, suites=(manifest.suite('hybrid-visual'),))
        argv = runner._guest_suite_preflight_argv(manifest, ('ssh', 'guest'))
        self.assertIn('version_info', argv[-1])
        self.assertIn('verify_baseline_cli', argv[-1])
        self.assertNotIn('interactive_ui_tests', argv[-1])

    def test_valid_summary_and_tampering(self):
        report = {'scenario': 'baseline', 'status': 'MEASURED',
                  'cli_sha256': 'a' * 64, 'app_sha256': 'b' * 64, 'fixture_sha256': 'c' * 64,
                  'internal_invocations': {'screenshot': None, 'model': None},
                  'display': {'screen_width': 1920, 'screen_height': 1080,
                              'viewport_width': 1280, 'viewport_height': 720,
                              'device_pixel_ratio': 2},
                  'samples': {op: [{'elapsed_ms': 1, 'receipt': {'kind': op.removeprefix('act_'),
                     'value': i + 1 if op == 'act_click' else f'baseline-{i}', 'isTrusted': True}}
                     for i in range(50)] for op in driver.OPERATIONS}}
        driver.validate_baseline(report)
        for mutation in ('count', 'receipt', 'hash', 'counter', 'display_missing',
                         'display_zero', 'display_nonfinite'):
            invalid = copy.deepcopy(report)
            if mutation == 'count':
                invalid['samples']['observe'].pop()
            elif mutation == 'receipt':
                invalid['samples']['act_click'][0]['receipt']['value'] = 99
            elif mutation == 'hash':
                invalid['cli_sha256'] = 'not-a-hash'
            elif mutation == 'counter':
                invalid['internal_invocations']['model'] = 0
            elif mutation == 'display_missing':
                invalid['display'] = None
            elif mutation == 'display_zero':
                invalid['display']['viewport_width'] = 0
            else:
                invalid['display']['device_pixel_ratio'] = float('nan')
            with self.assertRaises(ValueError):
                driver.validate_baseline(invalid)

    def test_baseline_manifest_fixed_scenario_and_no_script_override(self):
        path = Path(runner.__file__).with_name('macos_vm_e2e_manifest.json')
        data = json.loads(path.read_text())
        data['suites'] = [suite for suite in data['suites'] if suite['name'] != 'hybrid-visual']
        data['suites'].append({'name': 'hybrid-visual', 'kind': 'hybrid_visual', 'scenarios': ['baseline']})
        manifest = runner.parse_manifest(data)
        suite = manifest.suite('hybrid-visual')
        assert isinstance(suite, runner.HybridVisualSuite)
        self.assertEqual(suite.scenarios, ('baseline',))
        for change in ({'script': '/tmp/other.py'}, {'scenarios': ['native']}):
            invalid = copy.deepcopy(data)
            invalid['suites'][-1].update(change)
            with self.assertRaises(runner.InvalidManifestError):
                runner.parse_manifest(invalid)

    def test_staging_hash_detects_changed_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'maho/target/release/maho'
            source.parent.mkdir(parents=True)
            source.write_bytes(b'host-built-cli')
            source.chmod(0o755)
            app = root / 'chromium/src/out/Default/Maho.app'
            helper = app / 'Contents/Helpers/maho'
            helper.parent.mkdir(parents=True)
            helper.write_bytes(b'original-helper')
            executable = app / 'Contents/MacOS/Maho'
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b'browser-code')
            component = app.parent / 'libcomponent.dylib'
            component.write_bytes(b'component-code')
            with patch.object(transport.subprocess, 'run', return_value=SimpleNamespace(returncode=0, stdout='revision')):
                staged = transport.stage_baseline_cli(root)
            self.assertTrue(staged.is_file())
            self.assertEqual(staged, root / 'chromium/src/out/Default/hybrid-baseline/Maho.app/Contents/Helpers/maho')
            self.assertEqual(staged.read_bytes(), source.read_bytes())
            self.assertEqual(helper.read_bytes(), b'original-helper')
            self.assertNotEqual(staged.stat().st_ino, source.stat().st_ino)
            staged_component = staged.parents[3] / component.name
            self.assertTrue(staged_component.is_file())
            self.assertEqual(staged_component.read_bytes(), component.read_bytes())
            self.assertNotEqual(staged_component.stat().st_ino, component.stat().st_ino)
            transport.verify_baseline_cli(staged)
            staged_component.write_bytes(b'tampered-component')
            with self.assertRaises(ValueError):
                transport.verify_baseline_cli(staged)
            staged_component.write_bytes(component.read_bytes())
            staged.write_bytes(b'wrong-cli')
            with self.assertRaises(ValueError):
                transport.verify_baseline_cli(staged)

    def test_guest_uses_fixed_driver_and_rejects_dispatched_summary(self):
        command = guest.build_hybrid_visual_command('/Users/Shared/maho-workspace', '/tmp/baseline')
        self.assertIn('baseline', command.argv)
        self.assertIn('/Users/Shared/maho-workspace/chromium/src/out/Default/hybrid-baseline/Maho.app/Contents/Helpers/maho', command.argv)
        self.assertIn('/Users/Shared/maho-workspace/chromium/src/out/Default/hybrid-baseline/Maho.app', command.argv)
        self.assertIsNone(command.cdp_port)
        result = guest.CapturedCommandResult(command, 0, '', '')
        with self.assertRaises(guest.SuiteValidationError):
            guest.validate_hybrid_visual_result(result, {'status': 'SUCCESS', 'verified': 'dispatched'})


class StealthSuiteTests(unittest.TestCase):
    def test_stealth_suite_manifest_parsing(self):
        self.assertTrue(hasattr(runner, 'StealthSuite'), "runner must define StealthSuite")
        path = Path(runner.__file__).with_name('macos_vm_e2e_manifest.json')
        manifest = runner.load_manifest(path)
        suite = manifest.suite('stealth-local')
        self.assertIsInstance(suite, runner.StealthSuite)
        self.assertEqual(suite.kind, 'stealth')
        self.assertIn('sentinel', suite.scenarios)

    def test_stealth_suite_rejects_unknown_scenario_or_script_override(self):
        path = Path(runner.__file__).with_name('macos_vm_e2e_manifest.json')
        data = json.loads(path.read_text())
        invalid_data = copy.deepcopy(data)
        invalid_data['suites'] = [s for s in invalid_data['suites'] if s['name'] != 'stealth-local']
        invalid_data['suites'].append({
            'name': 'stealth-local',
            'kind': 'stealth',
            'scenarios': ['sentinel', 'unknown-scenario']
        })
        with self.assertRaises(runner.InvalidManifestError):
            runner.parse_manifest(invalid_data)

        override_data = copy.deepcopy(data)
        override_data['suites'] = [s for s in override_data['suites'] if s['name'] != 'stealth-local']
        override_data['suites'].append({
            'name': 'stealth-local',
            'kind': 'stealth',
            'scenarios': ['sentinel'],
            'script': '/tmp/override.py'
        })
        with self.assertRaises(runner.InvalidManifestError):
            runner.parse_manifest(override_data)

    def test_stealth_guest_command_and_validation(self):
        self.assertTrue(hasattr(guest, 'build_stealth_command'), "guest must define build_stealth_command")
        self.assertTrue(hasattr(guest, 'validate_stealth_result'), "guest must define validate_stealth_result")
        cmd = guest.build_stealth_command('/Users/Shared/maho-workspace', '/tmp/run', 'stealth-local', ('sentinel',))
        self.assertEqual(cmd.suite, 'stealth-local')
        self.assertIn('run', cmd.argv)
        self.assertIn('--scenario', cmd.argv)
        self.assertIn('sentinel', cmd.argv)
        self.assertIn('--cli', cmd.argv)
        self.assertIn('--app', cmd.argv)
        self.assertIn('--evidence-dir', cmd.argv)

        result = guest.CapturedCommandResult(cmd, 0, '', '')
        valid_summary = {
            'scenario': 'sentinel',
            'scenarios': ['sentinel'],
            'status': 'PASS',
            'browser_executed': True,
            'receipts': ['sentinel-receipt.json'],
            'results': {'sentinel': {'status': 'PASS', 'details': {}, 'receipts': ['sentinel-receipt.json']}},
            'details': {}
        }
        validation = guest.validate_stealth_result(result, valid_summary)
        self.assertEqual(validation.suite, 'stealth-local')
        self.assertEqual(validation.status, 'SUCCESS')
        self.assertEqual(validation.to_dict()['status'], 'SUCCESS')

        # Negative probe 1: wrong return code
        failed_result = guest.CapturedCommandResult(cmd, 1, '', 'error')
        with self.assertRaises(guest.SuiteValidationError):
            guest.validate_stealth_result(failed_result, valid_summary)

        # Negative probe 2: wrong scenario
        wrong_summary = {
            'scenario': 'wrong_scenario',
            'status': 'PASS',
            'browser_executed': True,
            'receipts': ['sentinel-receipt.json'],
            'results': {'wrong_scenario': {'status': 'PASS'}}
        }
        with self.assertRaises(guest.SuiteValidationError):
            guest.validate_stealth_result(result, wrong_summary)

        # Negative probe 3: explicitly failed scenario
        failed_scenario_summary = {
            'scenario': 'sentinel',
            'status': 'FAIL',
            'browser_executed': True,
            'receipts': ['sentinel-receipt.json'],
            'results': {'sentinel': {'status': 'FAIL'}}
        }
        with self.assertRaises(guest.SuiteValidationError):
            guest.validate_stealth_result(result, failed_scenario_summary)

        # Negative probe 4: numeric scenario identity
        numeric_summary = {
            'scenario': 12345,
            'status': 'PASS',
            'browser_executed': True,
            'receipts': ['sentinel-receipt.json'],
            'results': {'sentinel': {'status': 'PASS'}}
        }
        with self.assertRaises(guest.SuiteValidationError):
            guest.validate_stealth_result(result, numeric_summary)

        # Negative probe 5: browser_executed is False or missing
        no_browser_summary = copy.deepcopy(valid_summary)
        no_browser_summary['browser_executed'] = False
        with self.assertRaises(guest.SuiteValidationError):
            guest.validate_stealth_result(result, no_browser_summary)

        # Negative probe 6: empty receipts list
        empty_receipts_summary = copy.deepcopy(valid_summary)
        empty_receipts_summary['receipts'] = []
        with self.assertRaises(guest.SuiteValidationError):
            guest.validate_stealth_result(result, empty_receipts_summary)

        # Negative probe 7: scalar and list scenario identity conflict
        conflict_summary = copy.deepcopy(valid_summary)
        conflict_summary['scenario'] = 'sentinel'
        conflict_summary['scenarios'] = ['other-scenario']
        with self.assertRaises(guest.SuiteValidationError):
            guest.validate_stealth_result(result, conflict_summary)

        # Probe 8: online BLOCKED scenario serialization preserves BLOCKED
        online_cmd = guest.GuestCommand(
            suite='stealth-local',
            argv=('python3', 'driver.py', 'run', '--scenario', 'turnstile-online'),
            environment=(),
            stdout_path='stdout.log',
            stderr_path='stderr.log',
            expected_results=('turnstile-online',),
        )
        online_result = guest.CapturedCommandResult(online_cmd, 0, '', '')
        online_blocked_summary = {
            'scenario': 'turnstile-online',
            'scenarios': ['turnstile-online'],
            'status': 'BLOCKED',
            'browser_executed': True,
            'receipts': ['turnstile-receipt.json'],
            'results': {
                'turnstile-online': {
                    'status': 'BLOCKED',
                    'reason': 'network_unavailable',
                    'receipts': ['turnstile-receipt.json']
                }
            }
        }
        online_val = guest.validate_stealth_result(online_result, online_blocked_summary)
        self.assertEqual(online_val.status, 'BLOCKED')
        self.assertEqual(online_val.to_dict()['status'], 'BLOCKED')
        self.assertEqual(online_val.to_dict()['blocked'], ['turnstile-online'])

    def test_pipeline_run_subcommand_parsing(self):
        self.assertTrue(hasattr(driver, 'build_argument_parser'), "driver must define build_argument_parser")
        parser = driver.build_argument_parser()
        args = parser.parse_args([
            'run',
            '--scenario', 'sentinel',
            '--cli', '/tmp/cli',
            '--app', '/tmp/app',
            '--evidence-dir', '/tmp/evidence',
            '--output', '/tmp/evidence/summary.json',
        ])
        self.assertEqual(args.command, 'run')
        self.assertEqual(args.scenario, ['sentinel'])
        self.assertEqual(args.cli, Path('/tmp/cli'))
        self.assertEqual(args.app, Path('/tmp/app'))
        self.assertEqual(args.evidence_dir, '/tmp/evidence')
        self.assertEqual(args.output, '/tmp/evidence/summary.json')

    def test_pipeline_run_rejects_unknown_scenario(self):
        self.assertTrue(hasattr(driver, 'build_argument_parser'), "driver must define build_argument_parser")
        parser = driver.build_argument_parser()
        with self.assertRaises(SystemExit):
            parser.parse_args([
                'run',
                '--scenario', 'nonexistent-scenario',
                '--cli', '/tmp/cli',
                '--app', '/tmp/app',
                '--evidence-dir', '/tmp/evidence',
            ])

    def test_missing_runtime_blocks_fingerprint_without_fake_audit(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence_dir = Path(tmp) / 'evidence'
            output_file = evidence_dir / 'summary.json'
            args = SimpleNamespace(
                scenario=['fingerprint'],
                cli=Path('/Users/Shared/maho-workspace/missing-cli'),
                app=Path('/missing-app'),
                evidence_dir=str(evidence_dir),
                output=str(output_file),
            )
            summary = asyncio.run(driver.run(args))
            self.assertEqual(summary['status'], 'BLOCKED')
            self.assertFalse(summary['browser_executed'])
            self.assertEqual(summary['receipts'], [])
            self.assertTrue(summary['blockers'])
            self.assertIn('CLI executable missing', summary['results']['fingerprint']['reason'])
            self.assertIsNone(summary['cli_sha256'])
            self.assertIsNone(summary['app_sha256'])
            audit = json.loads((evidence_dir / 'fingerprint-audit.json').read_text())
            self.assertEqual(audit['status'], 'BLOCKED')
            self.assertNotIn('passed', audit)

    def test_every_missing_runtime_scenario_writes_nonpass_receipt(self):
        for scenario in ('challenge', 'misclick', 'recovery', 'telemetry'):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as tmp:
                evidence_dir = Path(tmp) / 'evidence'
                args = SimpleNamespace(
                    scenario=[scenario],
                    cli=Path('/Users/Shared/maho-workspace/missing-cli'),
                    app=Path('/missing-app'), evidence_dir=str(evidence_dir), output=None)
                summary = asyncio.run(driver.run(args))
                self.assertEqual(summary['status'], 'BLOCKED')
                receipt = json.loads((evidence_dir / f'{scenario}-receipt.json').read_text())
                self.assertEqual(receipt['status'], 'BLOCKED')
                self.assertIn('CLI executable missing', receipt['reason'])

    def test_turnstile_online_blocked_when_offline(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence_dir = Path(tmp) / 'evidence'
            output_file = evidence_dir / 'turnstile-summary.json'
            args = SimpleNamespace(
                scenario=['turnstile-online'],
                cli=Path('/Users/Shared/maho-workspace/cli'),
                app=Path('/app'),
                evidence_dir=str(evidence_dir),
                output=str(output_file),
            )
            summary = asyncio.run(driver.run(args))
            self.assertEqual(summary['status'], 'BLOCKED')
            self.assertIn('CLI executable missing', summary['results']['turnstile-online']['reason'])
            self.assertTrue((evidence_dir / 'turnstile-receipt.json').is_file())

    def _make_report(self, elapsed_ms=100.0, status='PASS', fixture='baseline', display='standard', internal=None):
        if internal is None:
            internal = {'screenshot': 0, 'model': 0}
        return {
            'status': status,
            'fixture': fixture,
            'display': display,
            'internal_invocations': dict(internal),
            'samples': {
                op: [{'elapsed_ms': elapsed_ms, 'receipt': {'isTrusted': True}} for _ in range(50)]
                for op in driver.OPERATIONS
            }
        }

    def test_compare_performance_within_threshold(self):
        before = self._make_report(100.0)
        after = self._make_report(102.0)
        res = driver.compare_performance(before, after, max_regression_percent=3.0)
        self.assertEqual(res['status'], 'PASS')
        for op in driver.OPERATIONS:
            self.assertEqual(res['operations'][op]['regression_percent'], 2.0)

    def test_compare_performance_exact_three_percent_passes(self):
        before = self._make_report(100.0)
        after_exact = self._make_report(103.0)
        res = driver.compare_performance(before, after_exact, max_regression_percent=3.0)
        self.assertEqual(res['status'], 'PASS')
        for op in driver.OPERATIONS:
            self.assertEqual(res['operations'][op]['regression_percent'], 3.0)

    def test_compare_performance_rejects_exceeded_threshold_or_internal_work(self):
        before = self._make_report(100.0)
        after_high = self._make_report(105.0)
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_high, max_regression_percent=3.0)

        # Nonzero candidate screenshot
        after_with_ss = self._make_report(100.0, internal={'screenshot': 1, 'model': 0})
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_with_ss, max_regression_percent=3.0)

        # Boolean candidate counter (False treated as 0 without strict type check)
        after_bool = self._make_report(100.0, internal={'screenshot': False, 'model': 0})
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_bool, max_regression_percent=3.0)

        # Null candidate counter
        after_null = self._make_report(100.0, internal={'screenshot': None, 'model': 0})
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_null, max_regression_percent=3.0)

        # Negative candidate counter
        after_neg = self._make_report(100.0, internal={'screenshot': -1, 'model': 0})
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_neg, max_regression_percent=3.0)

        # Missing candidate internal_invocations
        after_missing = copy.deepcopy(before)
        del after_missing['internal_invocations']
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_missing, max_regression_percent=3.0)

        # Null baseline counter
        before_null = self._make_report(100.0, internal={'screenshot': None, 'model': 0})
        with self.assertRaises(ValueError):
            driver.compare_performance(before_null, before, max_regression_percent=3.0)

        # Nonzero baseline counter
        before_nonzero = self._make_report(100.0, internal={'screenshot': 1, 'model': 0})
        with self.assertRaises(ValueError):
            driver.compare_performance(before_nonzero, before, max_regression_percent=3.0)

        # Missing baseline internal_invocations
        before_missing = copy.deepcopy(before)
        del before_missing['internal_invocations']
        with self.assertRaises(ValueError):
            driver.compare_performance(before_missing, before, max_regression_percent=3.0)

        # Missing action receipts in candidate samples
        after_no_receipts = copy.deepcopy(before)
        for op in driver.OPERATIONS:
            after_no_receipts['samples'][op] = [{'elapsed_ms': 100.0} for _ in range(50)]
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_no_receipts, max_regression_percent=3.0)

        # Untrusted action receipts in candidate samples
        after_untrusted = copy.deepcopy(before)
        after_untrusted['samples']['act_click'][0]['receipt'] = {'isTrusted': False}
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_untrusted, max_regression_percent=3.0)

        # Mismatched fixture metadata
        after_fixture_mismatch = self._make_report(100.0, fixture='other-fixture')
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_fixture_mismatch, max_regression_percent=3.0)

        # Mismatched display metadata
        after_display_mismatch = self._make_report(100.0, display='hidpi-retina')
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_display_mismatch, max_regression_percent=3.0)

        # Candidate report status FAIL
        after_failed = self._make_report(100.0, status='FAIL')
        with self.assertRaises(ValueError):
            driver.compare_performance(before, after_failed, max_regression_percent=3.0)

        # Baseline report status FAIL
        before_failed = self._make_report(100.0, status='FAIL')
        with self.assertRaises(ValueError):
            driver.compare_performance(before_failed, before, max_regression_percent=3.0)


class RealScenarioInfrastructureTests(unittest.IsolatedAsyncioTestCase):
    def test_node_resolver_finds_homebrew_without_path_and_reports_candidates(self):
        homebrew_node = '/opt/homebrew/bin/node'
        with patch.dict(driver.os.environ, {'PATH': ''}, clear=True), \
             patch.object(Path, 'is_file', new=lambda path: str(path) == homebrew_node), \
             patch.object(driver.os, 'access', side_effect=lambda path, mode: path == homebrew_node), \
             patch.object(driver.shutil, 'which', return_value=None):
            self.assertEqual(driver.resolve_node_binary(), homebrew_node)

        with patch.dict(driver.os.environ, {'PATH': ''}, clear=True), \
             patch.object(Path, 'is_file', return_value=False), \
             patch.object(driver.os, 'access', return_value=False), \
             patch.object(driver.shutil, 'which', return_value=None):
            with self.assertRaises(driver.ScenarioBlocked) as raised:
                driver.resolve_node_binary()
        self.assertEqual(
            str(raised.exception),
            'node runtime unavailable: MAHO_NODE unset, '
            '/opt/homebrew/bin/node missing, node not on PATH')

    async def test_node_page_is_rendered_from_owned_module(self):
        if driver.shutil.which('node') is None:
            self.skipTest('node runtime unavailable')
        fixture = await driver.FixtureServer('/challenge').start()
        try:
            self.assertIn(b'data-maho-fixture-route="challenge"', fixture.body)
            self.assertEqual(fixture.url.rsplit('/', 1)[-1], 'challenge')
        finally:
            await fixture.close()

    async def test_endpoint_subscription_is_installed_before_post(self):
        fixture = driver.FixtureServer('/challenge')
        fixture.body = b'fixture'
        fixture.server = await asyncio.start_server(fixture.handle, '127.0.0.1', 0)
        pending = fixture.subscribe_post('/challenge/verify')
        port = fixture.server.sockets[0].getsockname()[1]
        try:
            reader, writer = await asyncio.open_connection('127.0.0.1', port)
            fixture.active_episodes.add('test-episode')
            body = json.dumps({'target': 'challenge-target', 'episodeId': 'test-episode', 'isTrusted': True}).encode()
            writer.write(f'POST /challenge/verify HTTP/1.1\r\nContent-Length: {len(body)}\r\n\r\n'.encode() + body)
            await writer.drain()
            receipt = await asyncio.wait_for(pending, 2)
            self.assertTrue(receipt['isTrusted'])
            self.assertIn(b'"verified": true', await asyncio.wait_for(reader.read(), 2))
            writer.close()
            await writer.wait_closed()
        finally:
            await fixture.close()

    async def test_telemetry_endpoint_delivers_complete_trace(self):
        fixture = driver.FixtureServer('/telemetry')
        fixture.body = b'fixture'
        fixture.server = await asyncio.start_server(fixture.handle, '127.0.0.1', 0)
        pending = fixture.subscribe_post('/telemetry/trace')
        port = fixture.server.sockets[0].getsockname()[1]
        trace = {
            'moves': [{'x': 1, 'y': 2, 't': 3}],
            'keyboard': [{'type': 'keydown', 'key': 'a', 'code': 'KeyA', 't': 4}],
            'jitter_css_px': 0.5, 'overshoot_px': 3, 'dwell_ms': 100,
        }
        try:
            reader, writer = await asyncio.open_connection('127.0.0.1', port)
            body = json.dumps(trace).encode()
            writer.write(f'POST /telemetry/trace HTTP/1.1\r\nContent-Length: {len(body)}\r\n\r\n'.encode() + body)
            await writer.drain()
            receipt = await asyncio.wait_for(pending, 2)
            self.assertEqual(receipt['endpoint'], '/telemetry/trace')
            self.assertEqual(receipt['moves'], trace['moves'])
            self.assertIn(b'200 OK', await asyncio.wait_for(reader.read(), 2))
            writer.close()
            await writer.wait_closed()
        finally:
            await fixture.close()

    def test_raw_telemetry_measurements_and_keyboard_validation(self):
        trace = {
            'moves': [
                {'x': 120, 'y': 160, 't': 0},
                {'x': 160, 'y': 160.5, 't': 20},
                {'x': 203, 'y': 160, 't': 40},
                {'x': 200, 'y': 160, 't': 60},
            ],
            'target_center': {'x': 200, 'y': 160},
            'pointerdowns': [{'t': 70}],
            'pointerups': [{'t': 170}],
        }
        measured = driver._telemetry_measurements(trace)
        self.assertAlmostEqual(measured['jitter_css_px'], 0.5)
        self.assertAlmostEqual(measured['overshoot_px'], 3)
        self.assertAlmostEqual(measured['dwell_ms'], 100)
        self.assertTrue(measured['moves_monotonic'])
        sequence = [
            {'type': 'keydown', 'key': 'A', 'code': 'KeyA', 'altKey': False,
             'ctrlKey': False, 'metaKey': False, 'shiftKey': True, 't': 180},
            {'type': 'keyup', 'key': 'A', 'code': 'KeyA', 'altKey': False,
             'ctrlKey': False, 'metaKey': False, 'shiftKey': True, 't': 190},
        ]
        self.assertEqual(driver._keyboard_telemetry_valid(sequence), (True, True))
        sequence[1]['shiftKey'] = False
        self.assertEqual(driver._keyboard_telemetry_valid(sequence), (True, False))

    async def test_native_probe_never_dispatches_without_capture_issued_token(self):
        class Session:
            async def __aenter__(self):
                return (SimpleNamespace(), pipe, None)

            async def __aexit__(self, *exc):
                return False

        async def request(**request):
            requests.append(request)
            if request['op'] == 'observe':
                return {'snapshot_token': 'not-a-frame-token'}
            self.fail(f'native dispatch used an unissued token: {request}')

        requests = []
        pipe = SimpleNamespace(request=request)
        with patch.object(driver, 'stealth_session', return_value=Session()):
            with self.assertRaisesRegex(
                    driver.ScenarioBlocked,
                    '^browser exposes no visual frame capture: '
                    'last_visual_frame_ is only set by SetVisualFrameForTesting, '
                    'so any frame_token resolves to a synthetic 1280x800 frame$'):
                await driver._native_probe(None, None)
        self.assertEqual(requests, [{'op': 'observe', 'mode': 'full'}])

    async def test_settle_uses_pipe_wait_and_requires_found_selector(self):
        requests = []

        async def request(**request):
            requests.append(request)
            return {'found': True}

        pipe = SimpleNamespace(request=request)
        result = await driver._wait_for_settled_fixture(pipe, '/login')
        self.assertIs(result['found'], True)
        self.assertEqual(result['settled_by'], 'page.wait_for_selector')
        self.assertEqual(requests, [{
            'op': 'wait',
            'selector': '[data-maho-fixture="maho-security-fixtures"]',
            'timeout_ms': 10000,
        }])

        pipe.request = AsyncMock(return_value={'found': False})
        with self.assertRaisesRegex(
                driver.ScenarioBlocked, 'fixture /login did not settle'):
            await driver._wait_for_settled_fixture(pipe, '/login')

    async def test_stealth_session_defaults_to_page_ready_receipt(self):
        source = Path(driver.__file__).read_text(encoding='utf-8')
        self.assertIn("ready = ('ready', route.strip('/') or 'baseline')", source)
        self.assertIn('await _wait_for_settled_fixture(pipe, route)', source)
        pages = Path(driver.__file__).with_name('security_fixtures') / 'pages.js'
        self.assertIn("kind: 'ready'", pages.read_text(encoding='utf-8'))

    async def test_browser_pipe_preserves_real_rpc_error(self):
        process = SimpleNamespace(
            stdin=MagicMock(),
            stdout=SimpleNamespace(readline=AsyncMock(return_value=json.dumps({
                'id': 1, 'ok': False,
                'error': {'code': 'rpc_error', 'rpc_code': -32011},
            }).encode() + b'\n')),
        )
        process.stdin.drain = AsyncMock()
        with self.assertRaises(driver.PipeRequestError) as raised:
            await driver.BrowserPipe(process).request(op='native')
        self.assertEqual(raised.exception.response['error']['rpc_code'], -32011)


class NoFabricatedEvidenceTests(unittest.TestCase):
    """A scenario may never report PASS without real runtime interaction.

    Every handler in SCENARIO_REGISTRY is invoked with a guest-shaped but
    nonexistent CLI and app, so no browser, CLI pipe or fixture can ever be
    reached. Any PASS produced under those conditions is fabricated evidence.
    """

    GUEST_PREFIX = '/Users/Shared/maho-workspace/chromium/src/out/Default'

    def _args(self, scenario, evidence_dir):
        return Namespace(
            scenario=[scenario],
            cli=Path(f'{self.GUEST_PREFIX}/hybrid-baseline/Maho.app/Contents/Helpers/maho'),
            app=Path(f'{self.GUEST_PREFIX}/Maho.app'),
            evidence_dir=str(evidence_dir),
            output=None,
        )

    def test_registry_covers_every_declared_scenario(self):
        self.assertEqual(set(driver.SCENARIO_REGISTRY), set(driver.ALL_STEALTH_SCENARIOS))

    def test_entry_point_records_blocked_without_browser_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence = Path(tmp) / 'evidence'
            summary_path = evidence / 'summary.json'
            completed = subprocess.run([
                sys.executable, str(Path(driver.__file__)), 'run',
                '--scenario', 'challenge',
                '--cli', f'{self.GUEST_PREFIX}/missing-cli',
                '--app', f'{self.GUEST_PREFIX}/missing-app',
                '--evidence-dir', str(evidence),
                '--output', str(summary_path),
            ], text=True, capture_output=True, check=False)
            self.assertNotEqual(completed.returncode, 0, completed.stderr)
            summary = json.loads(summary_path.read_text(encoding='utf-8'))
            self.assertEqual(summary['status'], 'BLOCKED')
            self.assertFalse(summary['browser_executed'])
            self.assertEqual(summary['receipts'], [])
            self.assertTrue(summary['blockers'])
            result = summary['results']['challenge']
            self.assertFalse(result['browser_executed'])
            self.assertEqual(result['receipts'], [])
            self.assertTrue(result['blockers'])

    def test_no_scenario_passes_without_a_reachable_runtime(self):
        for scenario in driver.ALL_STEALTH_SCENARIOS:
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as tmp:
                try:
                    summary = asyncio.run(driver.run(self._args(scenario, Path(tmp))))
                except Exception:
                    continue  # An explicit failure is the honest outcome.
                result = (summary or {}).get('results', {}).get(scenario, {})
                self.assertNotEqual(
                    result.get('status'), 'PASS',
                    f'{scenario} reported PASS with no reachable CLI, app, browser or fixture',
                )
                self.assertIn(result.get('status'), {'FAIL', 'FAILED', 'BLOCKED', 'UNSUPPORTED'})

    def test_summary_never_reports_placeholder_hashes(self):
        for scenario in driver.ALL_STEALTH_SCENARIOS:
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as tmp:
                try:
                    summary = asyncio.run(driver.run(self._args(scenario, Path(tmp))))
                except Exception:
                    continue
                for field in ('cli_sha256', 'app_sha256'):
                    self.assertNotEqual(
                        (summary or {}).get(field), '0' * 64,
                        f'{field} reported a placeholder digest instead of a real measurement',
                    )

    def test_no_mock_environment_switch_bypasses_cli_trust(self):
        source = Path(driver.__file__).read_text(encoding='utf-8')
        self.assertNotIn(
            'MAHO_STEALTH_MOCK', source,
            'a mock environment switch must not be able to skip guest-path and CLI trust checks',
        )


class SettleCapabilityTests(unittest.TestCase):
    """A settle helper may not turn an absent browser capability into a failure.

    The real Tart run proved that this build answers op='wait' with
    -32601 'Method not found: unknown tool', because the pipe's wait op calls
    page.wait_for_selector, which the browser does not expose. Every scenario
    died on that first request. The fixture's own trusted kind='ready' receipt
    is the authoritative readiness signal and is awaited before this runs.
    """

    class _RaisingPipe:
        def __init__(self, response):
            self._response = response
            self.requests = []

        async def request(self, **kwargs):
            self.requests.append(kwargs)
            raise driver.PipeRequestError(self._response)

    def test_absent_wait_capability_defers_to_the_fixture_ready_receipt(self):
        pipe = self._RaisingPipe({'error': {
            'code': 'method_not_found',
            'message': 'Method not found: unknown tool',
            'rpc_code': -32601,
        }})
        result = asyncio.run(driver._wait_for_settled_fixture(pipe, '/baseline'))
        self.assertEqual(result['settled_by'], 'fixture-ready-receipt')
        self.assertEqual(result['wait_capability'], 'unavailable')
        self.assertEqual(len(pipe.requests), 1)
        self.assertEqual(pipe.requests[0]['op'], 'wait')

    def test_unrelated_pipe_failures_still_propagate(self):
        pipe = self._RaisingPipe({'error': {
            'code': 'internal_error', 'rpc_code': -32000,
        }})
        with self.assertRaises(driver.PipeRequestError):
            asyncio.run(driver._wait_for_settled_fixture(pipe, '/challenge'))

    def test_a_present_capability_must_actually_report_found(self):
        class _Pipe:
            async def request(self, **kwargs):
                return {'found': False}

        with self.assertRaises(driver.ScenarioBlocked):
            asyncio.run(driver._wait_for_settled_fixture(_Pipe(), '/challenge'))


if __name__ == '__main__':
    unittest.main()
