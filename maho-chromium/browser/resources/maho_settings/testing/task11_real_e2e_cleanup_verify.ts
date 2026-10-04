import {readFile} from 'node:fs/promises';
import {isAbsolute} from 'node:path';

import {type CleanupReceipt, type OwnedMahoProfileReceipt, type OwnedProcessReceipt, type OwnedProfileReceipt, parseCleanupReceipt, parseCleanupReceiptText} from './task11_real_e2e_contract.js';
import type {ProcessRecord} from './task12_process_ownership.js';

export type CleanupVerifierArguments =
  | {readonly kind: 'help'}
  | {readonly kind: 'verify'; readonly profilesReceipt: string; readonly chromiumSettingsReceipt: string};

export interface CleanupObservationHooks {
  listProcesses(): Promise<readonly ProcessRecord[]>;
  pathExists(path: string): Promise<boolean>;
}

export interface AggregateCleanupResult {
  readonly schema_version: 1;
  readonly status: 'PASS' | 'FAIL';
  readonly run_id: string;
  readonly errors: readonly string[];
  readonly observations: {
    readonly remaining_owned_processes: readonly OwnedProcessReceipt[];
    readonly remaining_temp_roots: readonly string[];
    readonly remaining_chromium_profiles: readonly OwnedProfileReceipt[];
    readonly remaining_maho_profiles: readonly OwnedMahoProfileReceipt[];
  };
}

export interface CleanupVerifierDependencies {
  readText(path: string): Promise<string>;
  readonly hooks: CleanupObservationHooks;
  stdout(value: string): void;
  stderr(value: string): void;
  now?: () => Date;
}

const HELP = 'Usage: task11_real_e2e_cleanup_verify --profiles-receipt <absolute-path> --chromium-settings-receipt <absolute-path>\n';
const MAX_RECEIPT_SKEW_MS = 5 * 60 * 1000;

export function parseCleanupVerifierArguments(argv: readonly string[]): CleanupVerifierArguments {
  if (argv.length === 1 && argv[0] === '--help') return {kind: 'help'};
  const values = new Map<string, string>();
  const allowed = new Set(['--profiles-receipt', '--chromium-settings-receipt']);
  for (let index = 0; index < argv.length; index += 2) {
    const flag = argv[index];
    const value = argv[index + 1];
    if (!flag || !allowed.has(flag)) throw new Error(`Unknown argument: ${flag ?? '<missing>'}`);
    if (values.has(flag)) throw new Error(`Duplicate argument: ${flag}`);
    if (!value || value.startsWith('--')) throw new Error(`Missing value for ${flag}`);
    if (!isAbsolute(value)) throw new Error(`${flag} must be absolute`);
    values.set(flag, value);
  }
  for (const flag of allowed) if (!values.has(flag)) throw new Error(`Missing argument: ${flag}`);
  return {kind: 'verify', profilesReceipt: values.get('--profiles-receipt') as string, chromiumSettingsReceipt: values.get('--chromium-settings-receipt') as string};
}

function uniqueBy<T>(values: readonly T[], key: (value: T) => string): T[] {
  const result = new Map<string, T>();
  for (const value of values) result.set(key(value), value);
  return [...result.values()];
}

function processRemains(process: OwnedProcessReceipt, inventory: readonly ProcessRecord[], markers: ReadonlySet<string>): boolean {
  return inventory.some(candidate => candidate.pid === process.pid && candidate.startToken === process.start_token && candidate.commandLine.some(argument => argument === process.marker && markers.has(argument)));
}

export async function verifyAggregateCleanup(
  profilesValue: unknown,
  chromiumSettingsValue: unknown,
  hooks: CleanupObservationHooks,
  now?: () => Date,
): Promise<AggregateCleanupResult> {
  const errors: string[] = [];
  let profiles: CleanupReceipt;
  let chromiumSettings: CleanupReceipt;
  try { profiles = parseCleanupReceipt(profilesValue); } catch (error) {
    return failedResult('', [`profiles receipt invalid: ${error instanceof Error ? error.message : String(error)}`]);
  }
  try { chromiumSettings = parseCleanupReceipt(chromiumSettingsValue); } catch (error) {
    return failedResult(profiles.run_id, [`chromium-settings receipt invalid: ${error instanceof Error ? error.message : String(error)}`]);
  }

  if (profiles.scenario !== 'profiles') errors.push('profiles receipt has the wrong scenario');
  if (chromiumSettings.scenario !== 'chromium-settings') errors.push('chromium-settings receipt has the wrong scenario');
  if (profiles.run_id !== chromiumSettings.run_id) errors.push('receipt run_id mismatch');
  for (const receipt of [profiles, chromiumSettings]) {
    if (receipt.status !== 'PASS') errors.push(`${receipt.scenario} receipt status is not PASS`);
    if (!receipt.cleanup.passed) errors.push(`${receipt.scenario} cleanup did not pass`);
    if (Date.parse(receipt.completed_at) < Date.parse(receipt.started_at)) errors.push(`${receipt.scenario} receipt timestamps are invalid`);
  }
  const starts = [Date.parse(profiles.started_at), Date.parse(chromiumSettings.started_at)];
  if (Math.max(...starts) - Math.min(...starts) > MAX_RECEIPT_SKEW_MS) errors.push('receipts are stale relative to each other');
  if (now) {
    const observedAt = now().getTime();
    for (const receipt of [profiles, chromiumSettings]) {
      const age = observedAt - Date.parse(receipt.completed_at);
      if (age < 0 || age > MAX_RECEIPT_SKEW_MS) errors.push(`${receipt.scenario} receipt is stale`);
    }
  }

  const receipts = [profiles, chromiumSettings] as const;
  const ownedProcesses = uniqueBy(receipts.flatMap(receipt => receipt.ownership.owned_processes), value => `${value.pid}\0${value.start_token}\0${value.marker}`);
  const markers = new Set(receipts.flatMap(receipt => receipt.ownership.markers));
  let inventory: readonly ProcessRecord[] = [];
  try { inventory = await hooks.listProcesses(); } catch (error) { errors.push(`process observation failed: ${error instanceof Error ? error.message : String(error)}`); }
  const remainingOwnedProcesses = ownedProcesses.filter(process => processRemains(process, inventory, markers));
  const remainingMarkers = new Set(remainingOwnedProcesses.map(process => process.marker));
  const receiptsWithStableOwnedProcesses = receipts.filter(receipt => receipt.ownership.markers.some(marker => remainingMarkers.has(marker)));
  const pathReceipts = receiptsWithStableOwnedProcesses.length > 0 ? receiptsWithStableOwnedProcesses : receipts;

  const tempRoots = uniqueBy(receipts.map(receipt => receipt.ownership.temp_root), value => value);
  const chromiumProfiles = uniqueBy(pathReceipts.flatMap(receipt => receipt.ownership.chromium_profiles), value => `${value.id}\0${value.path}`);
  const mahoProfiles = uniqueBy(pathReceipts.flatMap(receipt => receipt.ownership.maho_profiles), value => `${value.id}\0${value.path}`);
  const remainingTempRoots: string[] = [];
  const remainingChromiumProfiles: OwnedProfileReceipt[] = [];
  const remainingMahoProfiles: OwnedMahoProfileReceipt[] = [];
  for (const path of tempRoots) {
    try { if (await hooks.pathExists(path)) remainingTempRoots.push(path); } catch (error) { errors.push(`path observation failed for ${path}: ${error instanceof Error ? error.message : String(error)}`); }
  }
  for (const profile of chromiumProfiles) {
    try { if (await hooks.pathExists(profile.path)) remainingChromiumProfiles.push(profile); } catch (error) { errors.push(`path observation failed for ${profile.path}: ${error instanceof Error ? error.message : String(error)}`); }
  }
  for (const profile of mahoProfiles) {
    try { if (await hooks.pathExists(profile.path)) remainingMahoProfiles.push(profile); } catch (error) { errors.push(`path observation failed for ${profile.path}: ${error instanceof Error ? error.message : String(error)}`); }
  }
  if (remainingOwnedProcesses.length > 0) errors.push('owned processes remain');
  if (remainingTempRoots.length > 0) errors.push('temporary roots remain');
  if (remainingChromiumProfiles.length > 0) errors.push('Chromium profiles remain');
  if (remainingMahoProfiles.length > 0) errors.push('Maho profiles remain');
  return {schema_version: 1, status: errors.length === 0 ? 'PASS' : 'FAIL', run_id: profiles.run_id, errors, observations: {remaining_owned_processes: remainingOwnedProcesses, remaining_temp_roots: remainingTempRoots, remaining_chromium_profiles: remainingChromiumProfiles, remaining_maho_profiles: remainingMahoProfiles}};
}

function failedResult(runId: string, errors: readonly string[]): AggregateCleanupResult {
  return {schema_version: 1, status: 'FAIL', run_id: runId, errors, observations: {remaining_owned_processes: [], remaining_temp_roots: [], remaining_chromium_profiles: [], remaining_maho_profiles: []}};
}

const defaultDependencies: CleanupVerifierDependencies = {
  readText: path => readFile(path, 'utf8'),
  hooks: {
    listProcesses: async () => { throw new Error('live process inventory is not configured'); },
    pathExists: async () => { throw new Error('live path observation is not configured'); },
  },
  stdout: value => process.stdout.write(value),
  stderr: value => process.stderr.write(value),
};

export async function runCleanupVerifier(argv: readonly string[], dependencies: CleanupVerifierDependencies = defaultDependencies): Promise<number> {
  let parsed: CleanupVerifierArguments;
  try { parsed = parseCleanupVerifierArguments(argv); } catch (error) {
    dependencies.stdout(`${JSON.stringify(failedResult('', [error instanceof Error ? error.message : String(error)]))}\n`);
    return 2;
  }
  if (parsed.kind === 'help') {
    dependencies.stdout(HELP);
    return 0;
  }
  try {
    const [profilesText, chromiumSettingsText] = await Promise.all([dependencies.readText(parsed.profilesReceipt), dependencies.readText(parsed.chromiumSettingsReceipt)]);
    let profiles: CleanupReceipt;
    try { profiles = parseCleanupReceiptText(profilesText); } catch (error) {
      const result = failedResult('', [`profiles receipt invalid: ${error instanceof Error ? error.message : String(error)}`]);
      dependencies.stdout(`${JSON.stringify(result)}\n`);
      return 1;
    }
    let chromiumSettings: CleanupReceipt;
    try { chromiumSettings = parseCleanupReceiptText(chromiumSettingsText); } catch (error) {
      const result = failedResult(profiles.run_id, [`chromium-settings receipt invalid: ${error instanceof Error ? error.message : String(error)}`]);
      dependencies.stdout(`${JSON.stringify(result)}\n`);
      return 1;
    }
    const result = await verifyAggregateCleanup(profiles, chromiumSettings, dependencies.hooks, dependencies.now);
    dependencies.stdout(`${JSON.stringify(result)}\n`);
    return result.status === 'PASS' ? 0 : 1;
  } catch (error) {
    dependencies.stdout(`${JSON.stringify(failedResult('', [error instanceof Error ? error.message : String(error)]))}\n`);
    return 2;
  }
}

if (import.meta.main) process.exitCode = await runCleanupVerifier(process.argv.slice(2));
