import {randomUUID} from 'node:crypto';
import {rename, rm, writeFile} from 'node:fs/promises';
import {basename, dirname, isAbsolute, join} from 'node:path';

export type Task11Scenario = 'profiles' | 'chromium-settings';

export interface ScenarioEntryArguments {
  readonly scenario: Task11Scenario;
  readonly app: string;
  readonly browserTests: string;
  readonly evidenceRoot: string;
  readonly runId: string;
}

export interface ArtifactReceipt {
  readonly path: string;
  readonly sha256: string;
  readonly size: number;
  readonly mtime_utc: string;
}

export interface OwnedProcessReceipt {
  readonly role: string;
  readonly pid: number;
  readonly start_token: string;
  readonly marker: string;
}

export interface OwnedProfileReceipt {
  readonly id: string;
  readonly path: string;
}

export interface OwnedMahoProfileReceipt extends OwnedProfileReceipt {
  readonly name: string;
}

export interface OwnershipReceipt {
  readonly markers: readonly string[];
  readonly owned_processes: readonly OwnedProcessReceipt[];
  readonly temp_root: string;
  readonly chromium_profiles: readonly OwnedProfileReceipt[];
  readonly maho_profiles: readonly OwnedMahoProfileReceipt[];
}

export interface AssertionReceipt {
  readonly id: string;
  readonly passed: boolean;
  readonly evidence_paths: readonly string[];
  readonly detail: string;
}

interface CommonReceipt {
  readonly schema_version: 1;
  readonly status: 'PASS' | 'FAIL';
  readonly run_id: string;
  readonly scenario: Task11Scenario;
  readonly started_at: string;
  readonly completed_at: string;
  readonly artifacts: {readonly app: ArtifactReceipt; readonly browser_tests: ArtifactReceipt};
  readonly command: readonly string[];
  readonly source_versions: Readonly<Record<string, string>>;
  readonly tool_versions: Readonly<Record<string, string>>;
  readonly before_observables: Readonly<Record<string, unknown>>;
  readonly after_observables: Readonly<Record<string, unknown>>;
  readonly assertions: readonly AssertionReceipt[];
  readonly ownership: OwnershipReceipt;
  readonly raw_evidence_paths: readonly string[];
  readonly cleanup_receipt_path: string;
  readonly errors: readonly string[];
}

export interface ScenarioReceipt extends CommonReceipt {
  readonly receipt_type: 'scenario';
}

export interface CleanupStateReceipt {
  readonly passed: boolean;
  readonly remaining_owned_processes: readonly OwnedProcessReceipt[];
  readonly remaining_temp_roots: readonly string[];
  readonly remaining_chromium_profiles: readonly OwnedProfileReceipt[];
  readonly remaining_maho_profiles: readonly OwnedMahoProfileReceipt[];
}

export interface CleanupReceipt extends CommonReceipt {
  readonly receipt_type: 'cleanup';
  readonly cleanup: CleanupStateReceipt;
}

export interface AtomicJsonHooks {
  writeFile(path: string, data: string): Promise<void>;
  rename(from: string, to: string): Promise<void>;
  rm(path: string): Promise<void>;
  uuid(): string;
}

const object = (value: unknown, label: string): Record<string, unknown> => {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) throw new Error(`${label} must be an object`);
  return value as Record<string, unknown>;
};

function exact(value: Record<string, unknown>, keys: readonly string[], label: string): void {
  const expected = new Set(keys);
  for (const key of Object.keys(value)) if (!expected.has(key)) throw new Error(`Unknown ${label} field: ${key}`);
  for (const key of keys) if (!(key in value)) throw new Error(`Missing ${label} field: ${key}`);
}

function text(value: unknown, label: string): string {
  if (typeof value !== 'string' || value.length === 0) throw new Error(`${label} must be a non-empty string`);
  return value;
}

function absolutePath(value: unknown, label: string): string {
  const path = text(value, label);
  if (!isAbsolute(path)) throw new Error(`${label} must be absolute`);
  return path;
}

function timestamp(value: unknown, label: string): string {
  const result = text(value, label);
  if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/.test(result) || !Number.isFinite(Date.parse(result))) throw new Error(`${label} must be an ISO UTC timestamp`);
  return result;
}

function strings(value: unknown, label: string, paths = false): string[] {
  if (!Array.isArray(value)) throw new Error(`${label} must be an array`);
  return value.map((entry, index) => paths ? absolutePath(entry, `${label}[${index}]`) : text(entry, `${label}[${index}]`));
}

function stringRecord(value: unknown, label: string): Record<string, string> {
  const source = object(value, label);
  const result: Record<string, string> = {};
  for (const [key, entry] of Object.entries(source)) result[text(key, `${label} key`)] = text(entry, `${label}.${key}`);
  return result;
}

function artifact(value: unknown, label: string): ArtifactReceipt {
  const source = object(value, label);
  exact(source, ['path', 'sha256', 'size', 'mtime_utc'], label);
  const sha256 = text(source.sha256, `${label}.sha256`);
  if (!/^[0-9a-f]{64}$/.test(sha256)) throw new Error(`${label}.sha256 must be 64 lowercase hexadecimal characters`);
  if (!Number.isSafeInteger(source.size) || Number(source.size) < 0) throw new Error(`${label}.size must be a non-negative safe integer`);
  return {path: absolutePath(source.path, `${label}.path`), sha256, size: Number(source.size), mtime_utc: timestamp(source.mtime_utc, `${label}.mtime_utc`)};
}

function ownedProcess(value: unknown, label: string): OwnedProcessReceipt {
  const source = object(value, label);
  exact(source, ['role', 'pid', 'start_token', 'marker'], label);
  if (!Number.isSafeInteger(source.pid) || Number(source.pid) <= 0) throw new Error(`${label}.pid must be a positive safe integer`);
  return {role: text(source.role, `${label}.role`), pid: Number(source.pid), start_token: text(source.start_token, `${label}.start_token`), marker: text(source.marker, `${label}.marker`)};
}

function profile(value: unknown, label: string): OwnedProfileReceipt {
  const source = object(value, label);
  exact(source, ['id', 'path'], label);
  return {id: text(source.id, `${label}.id`), path: absolutePath(source.path, `${label}.path`)};
}

function mahoProfile(value: unknown, label: string): OwnedMahoProfileReceipt {
  const source = object(value, label);
  exact(source, ['id', 'name', 'path'], label);
  return {id: text(source.id, `${label}.id`), name: text(source.name, `${label}.name`), path: absolutePath(source.path, `${label}.path`)};
}

function list<T>(value: unknown, label: string, parse: (entry: unknown, label: string) => T): T[] {
  if (!Array.isArray(value)) throw new Error(`${label} must be an array`);
  return value.map((entry, index) => parse(entry, `${label}[${index}]`));
}

function ownership(value: unknown): OwnershipReceipt {
  const source = object(value, 'ownership');
  exact(source, ['markers', 'owned_processes', 'temp_root', 'chromium_profiles', 'maho_profiles'], 'ownership');
  return {markers: strings(source.markers, 'ownership.markers'), owned_processes: list(source.owned_processes, 'ownership.owned_processes', ownedProcess), temp_root: absolutePath(source.temp_root, 'ownership.temp_root'), chromium_profiles: list(source.chromium_profiles, 'ownership.chromium_profiles', profile), maho_profiles: list(source.maho_profiles, 'ownership.maho_profiles', mahoProfile)};
}

function assertion(value: unknown, label: string): AssertionReceipt {
  const source = object(value, label);
  exact(source, ['id', 'passed', 'evidence_paths', 'detail'], label);
  if (typeof source.passed !== 'boolean') throw new Error(`${label}.passed must be boolean`);
  return {id: text(source.id, `${label}.id`), passed: source.passed, evidence_paths: strings(source.evidence_paths, `${label}.evidence_paths`, true), detail: text(source.detail, `${label}.detail`)};
}

const commonKeys = ['schema_version', 'receipt_type', 'status', 'run_id', 'scenario', 'started_at', 'completed_at', 'artifacts', 'command', 'source_versions', 'tool_versions', 'before_observables', 'after_observables', 'assertions', 'ownership', 'raw_evidence_paths', 'cleanup_receipt_path', 'errors'] as const;

function parseCommon(value: unknown, receiptType: 'scenario' | 'cleanup'): CommonReceipt {
  const source = object(value, `${receiptType} receipt`);
  exact(source, receiptType === 'cleanup' ? [...commonKeys, 'cleanup'] : commonKeys, `${receiptType} receipt`);
  if (source.schema_version !== 1) throw new Error('schema_version must be 1');
  if (source.receipt_type !== receiptType) throw new Error(`receipt_type must be ${receiptType}`);
  if (source.status !== 'PASS' && source.status !== 'FAIL') throw new Error('status must be PASS or FAIL');
  if (source.scenario !== 'profiles' && source.scenario !== 'chromium-settings') throw new Error('scenario is unknown');
  const artifacts = object(source.artifacts, 'artifacts');
  exact(artifacts, ['app', 'browser_tests'], 'artifacts');
  return {
    schema_version: 1, status: source.status, run_id: text(source.run_id, 'run_id'), scenario: source.scenario,
    started_at: timestamp(source.started_at, 'started_at'), completed_at: timestamp(source.completed_at, 'completed_at'),
    artifacts: {app: artifact(artifacts.app, 'artifacts.app'), browser_tests: artifact(artifacts.browser_tests, 'artifacts.browser_tests')},
    command: strings(source.command, 'command'), source_versions: stringRecord(source.source_versions, 'source_versions'), tool_versions: stringRecord(source.tool_versions, 'tool_versions'),
    before_observables: object(source.before_observables, 'before_observables'), after_observables: object(source.after_observables, 'after_observables'),
    assertions: list(source.assertions, 'assertions', assertion), ownership: ownership(source.ownership), raw_evidence_paths: strings(source.raw_evidence_paths, 'raw_evidence_paths', true),
    cleanup_receipt_path: absolutePath(source.cleanup_receipt_path, 'cleanup_receipt_path'), errors: strings(source.errors, 'errors'),
  };
}

export function parseScenarioEntryArguments(argv: readonly string[]): ScenarioEntryArguments {
  const values = new Map<string, string>();
  const allowed = new Set(['--scenario', '--app', '--browser-tests', '--evidence-root', '--run-id']);
  for (let index = 0; index < argv.length; index += 2) {
    const flag = argv[index];
    const value = argv[index + 1];
    if (!flag || !allowed.has(flag)) throw new Error(`Unknown argument: ${flag ?? '<missing>'}`);
    if (values.has(flag)) throw new Error(`Duplicate argument: ${flag}`);
    if (value === undefined || value.startsWith('--')) throw new Error(`Missing value for ${flag}`);
    values.set(flag, value);
  }
  for (const flag of allowed) if (!values.has(flag)) throw new Error(`Missing argument: ${flag}`);
  const scenario = values.get('--scenario');
  if (scenario !== 'profiles' && scenario !== 'chromium-settings') throw new Error(`Unknown scenario: ${scenario}`);
  return {scenario, app: absolutePath(values.get('--app'), '--app'), browserTests: absolutePath(values.get('--browser-tests'), '--browser-tests'), evidenceRoot: absolutePath(values.get('--evidence-root'), '--evidence-root'), runId: text(values.get('--run-id'), '--run-id')};
}

export function parseScenarioReceipt(value: unknown): ScenarioReceipt {
  return {...parseCommon(value, 'scenario'), receipt_type: 'scenario'};
}

function rejectDuplicateJsonKeys(source: string): void {
  let index = 0;
  const whitespace = () => {
    while (/\s/.test(source[index] ?? '')) index++;
  };
  const string = (): string => {
    const start = index++;
    while (index < source.length) {
      const character = source[index++];
      if (character === '"') return JSON.parse(source.slice(start, index)) as string;
      if (character === '\\') index++;
    }
    throw new SyntaxError('Unterminated JSON string');
  };
  const value = (): void => {
    whitespace();
    if (source[index] === '{') {
      index++;
      whitespace();
      const keys = new Set<string>();
      if (source[index] === '}') { index++; return; }
      while (true) {
        whitespace();
        const key = string();
        if (keys.has(key)) throw new Error(`Duplicate JSON object key: ${key}`);
        keys.add(key);
        whitespace();
        if (source[index++] !== ':') throw new SyntaxError('Expected colon after JSON object key');
        value();
        whitespace();
        const separator = source[index++];
        if (separator === '}') return;
        if (separator !== ',') throw new SyntaxError('Expected comma in JSON object');
      }
    }
    if (source[index] === '[') {
      index++;
      whitespace();
      if (source[index] === ']') { index++; return; }
      while (true) {
        value();
        whitespace();
        const separator = source[index++];
        if (separator === ']') return;
        if (separator !== ',') throw new SyntaxError('Expected comma in JSON array');
      }
    }
    if (source[index] === '"') { string(); return; }
    while (index < source.length && !/[\s,}\]]/.test(source[index] ?? '')) index++;
  };
  value();
}

function parseJsonTextWithoutDuplicateKeys(source: string): unknown {
  const parsed = JSON.parse(source) as unknown;
  rejectDuplicateJsonKeys(source);
  return parsed;
}

export function parseScenarioReceiptText(source: string): ScenarioReceipt {
  return parseScenarioReceipt(parseJsonTextWithoutDuplicateKeys(source));
}

export function parseCleanupReceipt(value: unknown): CleanupReceipt {
  const common = parseCommon(value, 'cleanup');
  const source = object(object(value, 'cleanup receipt').cleanup, 'cleanup');
  exact(source, ['passed', 'remaining_owned_processes', 'remaining_temp_roots', 'remaining_chromium_profiles', 'remaining_maho_profiles'], 'cleanup');
  if (typeof source.passed !== 'boolean') throw new Error('cleanup.passed must be boolean');
  return {...common, receipt_type: 'cleanup', cleanup: {passed: source.passed, remaining_owned_processes: list(source.remaining_owned_processes, 'cleanup.remaining_owned_processes', ownedProcess), remaining_temp_roots: strings(source.remaining_temp_roots, 'cleanup.remaining_temp_roots', true), remaining_chromium_profiles: list(source.remaining_chromium_profiles, 'cleanup.remaining_chromium_profiles', profile), remaining_maho_profiles: list(source.remaining_maho_profiles, 'cleanup.remaining_maho_profiles', mahoProfile)}};
}

export function parseCleanupReceiptText(source: string): CleanupReceipt {
  return parseCleanupReceipt(parseJsonTextWithoutDuplicateKeys(source));
}

const defaultAtomicHooks: AtomicJsonHooks = {
  writeFile: (path, data) => writeFile(path, data, {encoding: 'utf8', flag: 'wx'}),
  rename,
  rm: path => rm(path, {force: true}),
  uuid: randomUUID,
};

export async function writeAtomicJsonReceipt(path: string, value: unknown, hooks: AtomicJsonHooks = defaultAtomicHooks): Promise<void> {
  absolutePath(path, 'receipt path');
  const temporary = join(dirname(path), `.${basename(path)}.${hooks.uuid()}.tmp`);
  try {
    await hooks.writeFile(temporary, `${JSON.stringify(value, null, 2)}\n`);
    await hooks.rename(temporary, path);
  } catch (error) {
    try { await hooks.rm(temporary); } catch { /* Preserve the primary persistence error. */ }
    throw error;
  }
}
