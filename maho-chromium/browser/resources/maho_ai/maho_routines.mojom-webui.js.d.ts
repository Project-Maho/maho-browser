import type {RoutineInfo, RoutineRunRecord} from './routine-client.js';

export interface RoutineRunStatus {
  runId: string;
  routineId: string;
  source: string;
  state: number;
  revision: bigint;
  result: string|null;
  error: string|null;
  approval: {approvalId: string}|null;
}

export class PageCallbackRouter {
  $: {bindNewPipeAndPassRemote(): unknown};
  onRoutineRunStatusChanged: {
    addListener(listener: (status: RoutineRunStatus) => void): number;
  };
  removeListener(id: number): boolean;
}

export class PageHandlerRemote {
  $: {bindNewPipeAndPassReceiver(): unknown};
  listAllRoutines(): Promise<{routines: RoutineInfo[]}>;
  createRoutine(
      name: string, prompt: string, schedule: string|null,
      trigger: string|null): Promise<{ok: boolean}>;
  startRoutine(id: string):
      Promise<{runId: string|null; error: string|null}>;
  getRoutineRunStatuses(): Promise<{statuses: RoutineRunStatus[]}>;
  getUserTier(): Promise<{tier: number}>;
  respondToRoutineApproval(
      runId: string, approvalId: string,
      approved: boolean): Promise<{ok: boolean}>;
  listRunHistory(
      routineId: string|null,
      limit: number): Promise<{records: RoutineRunRecord[]}>;
}

export class PageHandlerFactory {
  static getRemote(): {
    createPageHandler(page: unknown, handler: unknown): void;
  };
}
