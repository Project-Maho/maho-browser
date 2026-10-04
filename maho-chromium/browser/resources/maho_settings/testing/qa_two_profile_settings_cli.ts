import type {Task12DriverStatus} from './qa_two_profile_settings.js';
import {runDefaultTask12Qa} from './task12_default_runtime.js';

type Task12Main = () => Promise<Task12DriverStatus>;
type Task12Stdout = (value: string) => void;

export async function runTask12QaCli(
  invokeMain: Task12Main = runDefaultTask12Qa,
  writeStdout: Task12Stdout = value => process.stdout.write(value),
): Promise<number> {
  let result: Task12DriverStatus;
  try {
    result = await invokeMain();
  } catch (error) {
    result = {
      status: 'failed',
      message: error instanceof Error ? error.message : String(error),
    };
  }
  writeStdout(`${JSON.stringify(result)}\n`);
  return result.status === 'passed' ? 0 : 1;
}
