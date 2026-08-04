import {join} from 'node:path';

import {
  type ArtifactReceipt,
  type AssertionReceipt,
  type CleanupStateReceipt,
  type OwnershipReceipt,
  type ScenarioEntryArguments,
  parseScenarioEntryArguments,
  writeAtomicJsonReceipt,
} from './task11_real_e2e_contract.ts';

export interface Task11OwnedRuntime {
  readonly command: readonly string[];
  readonly sourceVersions: Readonly<Record<string, string>>;
  readonly toolVersions: Readonly<Record<string, string>>;
  readonly beforeObservables: Readonly<Record<string, unknown>>;
  readonly ownership: OwnershipReceipt;
  readonly rawEvidencePaths: readonly string[];
}

export interface Task11ScenarioObservation {
  readonly afterObservables: Readonly<Record<string, unknown>>;
  readonly assertions: readonly AssertionReceipt[];
  readonly rawEvidencePaths: readonly string[];
}

export interface Task11CleanupObservation extends Task11ScenarioObservation {
  readonly cleanup: CleanupStateReceipt;
}

export interface Task11RealE2eHost {
  now(): string;
  verifyArtifact(path: string): Promise<ArtifactReceipt>;
  startOwnedRuntime(args: ScenarioEntryArguments): Promise<Task11OwnedRuntime>;
  awaitOwnedSettingsTarget(runtime: Task11OwnedRuntime): Promise<void>;
  provisionOwnedProfiles(runtime: Task11OwnedRuntime): Promise<void>;
  runProfilesScenario(runtime: Task11OwnedRuntime): Promise<Task11ScenarioObservation>;
  runChromiumSettingsScenario(runtime: Task11OwnedRuntime): Promise<Task11ScenarioObservation>;
  verifiedCleanup(runtime: Task11OwnedRuntime | undefined): Promise<Task11CleanupObservation | undefined>;
}

export interface Task11RealE2eResult {
  readonly status: 'PASS' | 'FAIL';
  readonly scenarioReceiptPath?: string;
  readonly cleanupReceiptPath?: string;
  readonly errors: readonly string[];
}

const errorText = (error: unknown): string => error instanceof Error ? error.message : String(error);

export interface Task11StdoutWriter {
  write(value: string, callback: (error?: Error | null) => void): boolean;
  once(event: 'drain', listener: () => void): this;
  once(event: 'error', listener: (error: Error) => void): this;
  removeListener(event: 'drain', listener: () => void): this;
  removeListener(event: 'error', listener: (error: Error) => void): this;
}

export const writeStdout = async (
    value: unknown, writer?: Task11StdoutWriter): Promise<void> => {
  const output = `${JSON.stringify(value)}\n`;
  if (!writer) {
    const sink = Bun.stdout.writer();
    sink.write(output);
    await sink.end();
    return;
  }

  await new Promise<void>((resolve, reject) => {
    let callbackCompleted = false;
    let drainCompleted = false;
    let settled = false;

    const cleanup = () => {
      writer.removeListener('drain', onDrain);
      writer.removeListener('error', onError);
    };
    const complete = () => {
      if (!settled && callbackCompleted && drainCompleted) {
        settled = true;
        cleanup();
        resolve();
      }
    };
    const fail = (error: Error) => {
      if (!settled) {
        settled = true;
        cleanup();
        reject(error);
      }
    };
    const onDrain = () => {
      drainCompleted = true;
      complete();
    };
    const onError = (error: Error) => fail(error);

    writer.once('error', onError);
    try {
      const accepted = writer.write(output, error => {
        if (error) {
          fail(error);
          return;
        }
        callbackCompleted = true;
        complete();
      });
      drainCompleted = accepted;
      if (!accepted) writer.once('drain', onDrain);
      complete();
    } catch (error) {
      fail(error instanceof Error ? error : new Error(String(error)));
    }
  });
};

export async function runTask11RealE2e(
    args: ScenarioEntryArguments, host: Task11RealE2eHost): Promise<Task11RealE2eResult> {
  const scenarioReceiptPath = join(args.evidenceRoot, `${args.runId}.${args.scenario}.scenario.json`);
  const cleanupReceiptPath = join(args.evidenceRoot, `${args.runId}.${args.scenario}.cleanup.json`);
  const startedAt = host.now();
  const errors: string[] = [];
  let app: ArtifactReceipt | undefined;
  let browserTests: ArtifactReceipt | undefined;
  let runtime: Task11OwnedRuntime | undefined;
  let scenario: Task11ScenarioObservation | undefined;

  try {
    // Keep these sequential: both verifications must complete before launch.
    app = await host.verifyArtifact(args.app);
    browserTests = await host.verifyArtifact(args.browserTests);
    runtime = await host.startOwnedRuntime(args);
    await host.awaitOwnedSettingsTarget(runtime);
    if (args.scenario === 'profiles') await host.provisionOwnedProfiles(runtime);
    scenario = args.scenario === 'profiles' ?
      await host.runProfilesScenario(runtime) :
      await host.runChromiumSettingsScenario(runtime);
  } catch (error) {
    errors.push(errorText(error));
  }

  if (app && browserTests && runtime) {
    const assertions = scenario?.assertions ?? [];
    const scenarioPassed = scenario !== undefined && assertions.every(assertion => assertion.passed);
    await writeAtomicJsonReceipt(scenarioReceiptPath, {
      schema_version: 1,
      receipt_type: 'scenario',
      status: scenarioPassed ? 'PASS' : 'FAIL',
      run_id: args.runId,
      scenario: args.scenario,
      started_at: startedAt,
      completed_at: host.now(),
      artifacts: {app, browser_tests: browserTests},
      command: runtime.command,
      source_versions: runtime.sourceVersions,
      tool_versions: runtime.toolVersions,
      before_observables: runtime.beforeObservables,
      after_observables: scenario?.afterObservables ?? {},
      assertions,
      ownership: runtime.ownership,
      raw_evidence_paths: [...runtime.rawEvidencePaths, ...(scenario?.rawEvidencePaths ?? [])],
      cleanup_receipt_path: cleanupReceiptPath,
      errors,
    });
  }

  let cleanup: Task11CleanupObservation | undefined;
  try {
    cleanup = await host.verifiedCleanup(runtime);
  } catch (error) {
    errors.push(errorText(error));
  }

  if (app && browserTests && runtime && cleanup) {
    const cleanupPassed = cleanup.cleanup.passed &&
      cleanup.assertions.every(assertion => assertion.passed);
    if (!cleanupPassed) errors.push('Verified cleanup failed');
    await writeAtomicJsonReceipt(cleanupReceiptPath, {
      schema_version: 1,
      receipt_type: 'cleanup',
      status: cleanupPassed ? 'PASS' : 'FAIL',
      run_id: args.runId,
      scenario: args.scenario,
      started_at: startedAt,
      completed_at: host.now(),
      artifacts: {app, browser_tests: browserTests},
      command: runtime.command,
      source_versions: runtime.sourceVersions,
      tool_versions: runtime.toolVersions,
      before_observables: runtime.beforeObservables,
      after_observables: cleanup.afterObservables,
      assertions: cleanup.assertions,
      ownership: runtime.ownership,
      raw_evidence_paths: [...runtime.rawEvidencePaths, ...cleanup.rawEvidencePaths],
      cleanup_receipt_path: cleanupReceiptPath,
      errors,
      cleanup: cleanup.cleanup,
    });
  } else if (runtime && !cleanup) {
    errors.push('Verified cleanup did not produce a receipt');
  }

  const passed = errors.length === 0 && scenario !== undefined &&
    scenario.assertions.every(assertion => assertion.passed) && cleanup?.cleanup.passed === true &&
    cleanup.assertions.every(assertion => assertion.passed);
  return {
    status: passed ? 'PASS' : 'FAIL',
    ...(app && browserTests && runtime ? {scenarioReceiptPath} : {}),
    ...(app && browserTests && runtime && cleanup ? {cleanupReceiptPath} : {}),
    errors,
  };
}

const usage = 'Usage: task11_real_e2e_entry.ts --scenario <profiles|chromium-settings> --app <absolute-path> --browser-tests <absolute-path> --evidence-root <absolute-path> --run-id <id>';

if (import.meta.main || process.argv.slice(2).includes('--scenario')) {
  if (process.argv.slice(2).includes('--help')) {
    console.log(usage);
  } else {
    try {
      const args = parseScenarioEntryArguments(process.argv.slice(2));
      const {createDefaultTask11RealE2eHost} = await import('./task11_canonical_live_executor.ts');
      const result = await runTask11RealE2e(args, createDefaultTask11RealE2eHost());
      await writeStdout(result);
      if (result.status !== 'PASS') process.exitCode = 1;
    } catch (error) {
      await writeStdout({status: 'FAIL', errors: [errorText(error)]});
      process.exitCode = 1;
    }
  }
}
