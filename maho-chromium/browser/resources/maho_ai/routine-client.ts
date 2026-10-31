import {
  CompactSurface,
  PageCallbackRouter,
  PageHandlerRemote,
  SurfaceRequest,
} from './maho_ai.mojom-webui.js';

export type RoutineSurface = 'chat'|'routines';

export interface RoutineSurfaceState {
  surface: RoutineSurface;
  generation: bigint;
}

export type RoutineRunState =
    'queued'|'running'|'awaiting_approval'|'succeeded'|'failed';

export interface RoutineInfo {
  id: string;
  name: string;
  cron: string;
  description: string;
  isCustom: boolean;
  schedule: string|null;
  trigger: string|null;
  enabled: boolean;
}

export interface RoutineRunStatus {
  runId: string;
  routineId: string;
  source: string;
  state: RoutineRunState;
  revision: bigint;
  result: string|null;
  error: string|null;
  approvalId: string|null;
}

export interface RoutineRunRecord {
  resultId: bigint;
  routineId: string;
  ranAt: bigint;
  success: boolean;
  content: string;
  source: string;
}

interface WireRoutineRunStatus {
  runId: string;
  routineId: string;
  source: string;
  state: number;
  revision: bigint;
  result: string|null;
  error: string|null;
  approval: {approvalId: string}|null;
}

export interface RoutinePageHandler {
  listAllRoutines(): Promise<{routines: RoutineInfo[]}>;
  createRoutine(
      name: string, prompt: string, schedule: string|null,
      trigger: string|null): Promise<{ok: boolean}>;
  startRoutine(id: string):
      Promise<{runId: string|null; error: string|null}>;
  getRoutineRunStatuses(): Promise<{statuses: WireRoutineRunStatus[]}>;
  getRoutineUserTier?(): Promise<{tier: number}>;
  getUserTier?(): Promise<{tier: number}>;
  respondToRoutineApproval(
      runId: string, approvalId: string,
      approved: boolean): Promise<{ok: boolean}>;
  listRunHistory(routineId: string|null, limit: number):
      Promise<{records: RoutineRunRecord[]}>;
  startTraceRecording(tabId: bigint): Promise<{ok: boolean}>;
  stopTraceRecording(): Promise<{traceJson: string|null; error: string|null}>;
  createRoutineFromTrace(
      name: string, description: string, traceJson: string,
      schedule: string|null, trigger: string|null): Promise<{ok: boolean}>;
  isTraceRecording(): Promise<{isRecording: boolean; activeTabId: bigint}>;
}

interface RoutineStatusCallbackRouter {
  onRoutineRunStatusChanged: {
    addListener(callback: (status: WireRoutineRunStatus) => void): number;
  };
  removeListener(id: number): boolean;
}

function mapRunState(state: number): RoutineRunState {
  switch (state) {
    case 0:
      return 'queued';
    case 1:
      return 'running';
    case 2:
      return 'awaiting_approval';
    case 3:
      return 'succeeded';
    case 4:
      return 'failed';
    default:
      throw new Error(`Unknown Routine run state: ${state}`);
  }
}

export function mapRoutineRunStatus(
    status: WireRoutineRunStatus): RoutineRunStatus {
  return {
    runId: status.runId,
    routineId: status.routineId,
    source: status.source,
    state: mapRunState(status.state),
    revision: status.revision,
    result: status.result,
    error: status.error,
    approvalId: status.approval?.approvalId ?? null,
  };
}

export function mapSurfaceRequest(request: SurfaceRequest): RoutineSurfaceState {
  switch (request.surface) {
    case CompactSurface.kChat:
      return {surface: 'chat', generation: request.generation};
    case CompactSurface.kRoutines:
      return {surface: 'routines', generation: request.generation};
    default:
      return {surface: 'chat', generation: request.generation};
  }
}

export class RoutineSurfaceClient {
  private state: RoutineSurfaceState = {surface: 'chat', generation: 0n};
  private listenerId: number|null = null;

  constructor(
      private readonly pageHandler: PageHandlerRemote,
      private readonly callbackRouter: PageCallbackRouter,
      private readonly onChange?: (state: RoutineSurfaceState) => void) {}

  get current(): RoutineSurfaceState {
    return this.state;
  }

  async initialize(): Promise<RoutineSurfaceState> {
    this.listenerId = this.callbackRouter.onSurfaceRequested.addListener(
        request => this.accept(request));
    const {request} =
        await this.pageHandler.consumePendingSurface(this.state.generation);
    this.accept(request);
    return this.state;
  }

  dispose(): void {
    if (this.listenerId !== null) {
      this.callbackRouter.removeListener(this.listenerId);
      this.listenerId = null;
    }
  }

  private accept(request: SurfaceRequest): void {
    if (request.generation <= this.state.generation) {
      return;
    }
    this.state = mapSurfaceRequest(request);
    this.onChange?.(this.state);
  }
}

export class RoutineOperationsClient {
  constructor(
      private readonly handler: RoutinePageHandler,
      private readonly callbackRouter?: RoutineStatusCallbackRouter) {}

  async list(): Promise<RoutineInfo[]> {
    return (await this.handler.listAllRoutines()).routines;
  }

  async isEligible(): Promise<boolean> {
    const result = this.handler.getUserTier ?
        await this.handler.getUserTier() :
        await this.handler.getRoutineUserTier!();
    return result.tier >= 2;
  }

  async create(
      name: string, prompt: string, schedule: string|null,
      trigger: string|null): Promise<boolean> {
    return (await this.handler.createRoutine(
                name, prompt, schedule, trigger))
        .ok;
  }

  async run(id: string): Promise<string> {
    const {runId, error} = await this.handler.startRoutine(id);
    if (!runId) {
      throw new Error(error ?? 'Routine run was not queued');
    }
    return runId;
  }

  async statuses(): Promise<RoutineRunStatus[]> {
    return (await this.handler.getRoutineRunStatuses())
        .statuses.map(mapRoutineRunStatus);
  }

  subscribe(
      callback: (status: RoutineRunStatus) => void): () => void {
    if (!this.callbackRouter) {
      return () => {};
    }
    const listenerId =
        this.callbackRouter.onRoutineRunStatusChanged.addListener(
            status => callback(mapRoutineRunStatus(status)));
    return () => {
      this.callbackRouter?.removeListener(listenerId);
    };
  }

  async approve(
      runId: string, approvalId: string, approved: boolean): Promise<boolean> {
    return (await this.handler.respondToRoutineApproval(
                runId, approvalId, approved))
        .ok;
  }

  async history(
      routineId: string|null, limit: number): Promise<RoutineRunRecord[]> {
    return (await this.handler.listRunHistory(routineId, limit))
        .records as RoutineRunRecord[];
  }

  async startTraceRecording(tabId: bigint = 0n): Promise<boolean> {
    if (!this.handler.startTraceRecording) {
      return false;
    }
    const res = await this.handler.startTraceRecording(tabId);
    return res.ok;
  }

  async stopTraceRecording(): Promise<{traceJson: string|null; error: string|null}> {
    if (!this.handler.stopTraceRecording) {
      return {traceJson: null, error: 'Not supported'};
    }
    return this.handler.stopTraceRecording();
  }

  async createRoutineFromTrace(
      name: string, description: string, traceJson: string,
      schedule: string|null = null, trigger: string|null = null): Promise<boolean> {
    if (!this.handler.createRoutineFromTrace) {
      return false;
    }
    const res = await this.handler.createRoutineFromTrace(
        name, description, traceJson, schedule, trigger);
    return res.ok;
  }

  async isTraceRecording(): Promise<{isRecording: boolean; activeTabId: bigint}> {
    if (!this.handler.isTraceRecording) {
      return {isRecording: false, activeTabId: 0n};
    }
    return this.handler.isTraceRecording();
  }
}
