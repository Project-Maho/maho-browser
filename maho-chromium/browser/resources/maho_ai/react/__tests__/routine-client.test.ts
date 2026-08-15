import {describe, expect, it, vi} from 'vitest';

import {
  CompactSurface,
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
  SurfaceRequest,
} from '../../maho_ai.mojom-webui.js';
import {
  mapSurfaceRequest,
  RoutineOperationsClient,
  RoutineSurfaceClient,
} from '../../routine-client.js';

describe('RoutineSurfaceClient', () => {
  it('maps known and unknown surfaces fail closed', () => {
    expect(mapSurfaceRequest(
               {surface: CompactSurface.kRoutines, generation: 2n}))
        .toEqual({surface: 'routines', generation: 2n});
    expect(mapSurfaceRequest(
               {surface: 99 as CompactSurface, generation: 3n}))
        .toEqual({surface: 'chat', generation: 3n});
  });

  it('consumes cold state and rejects stale warm notifications', async () => {
    let listener: ((request: SurfaceRequest) => void)|undefined;
    const pageHandler = {
      consumePendingSurface: vi.fn().mockResolvedValue({
        request: {surface: CompactSurface.kRoutines, generation: 4n},
      }),
    };
    const callbackRouter = {
      onSurfaceRequested: {
        addListener: vi.fn((callback: (request: SurfaceRequest) => void) => {
          listener = callback;
          return 7;
        }),
      },
      removeListener: vi.fn(),
    };
    const client =
        new RoutineSurfaceClient(pageHandler as never, callbackRouter as never);

    await expect(client.initialize())
        .resolves.toEqual({surface: 'routines', generation: 4n});
    expect(pageHandler.consumePendingSurface).toHaveBeenCalledWith(0n);

    listener?.({surface: CompactSurface.kChat, generation: 3n});
    expect(client.current).toEqual({surface: 'routines', generation: 4n});

    listener?.({surface: CompactSurface.kChat, generation: 5n});
    expect(client.current).toEqual({surface: 'chat', generation: 5n});

    client.dispose();
    expect(callbackRouter.removeListener).toHaveBeenCalledWith(7);
  });

  it('subscribes before consuming pending state', async () => {
    let listener: ((request: SurfaceRequest) => void)|undefined;
    let resolvePending:
        ((value: {request: SurfaceRequest}) => void)|undefined;
    const pageHandler = {
      consumePendingSurface: vi.fn().mockImplementation(
          () => new Promise<{request: SurfaceRequest}>(resolve => {
            resolvePending = resolve;
          })),
    };
    const callbackRouter = {
      onSurfaceRequested: {
        addListener: vi.fn((callback: (request: SurfaceRequest) => void) => {
          listener = callback;
          return 8;
        }),
      },
      removeListener: vi.fn(),
    };
    const client =
        new RoutineSurfaceClient(pageHandler as never, callbackRouter as never);

    const initialization = client.initialize();
    listener?.({surface: CompactSurface.kRoutines, generation: 2n});
    resolvePending?.({
      request: {surface: CompactSurface.kChat, generation: 1n},
    });

    await expect(initialization)
        .resolves.toEqual({surface: 'routines', generation: 2n});
  });
});

describe('RoutineOperationsClient', () => {
  it('isolates state and callbacks between independently bound AI handlers', async () => {
    const bindClient = () => {
      const router = new PageCallbackRouter();
      const handler = new PageHandlerRemote();
      PageHandlerFactory.getRemote().createPageHandler(
          router.$.bindNewPipeAndPassRemote(),
          handler.$.bindNewPipeAndPassReceiver());
      return new RoutineOperationsClient(handler as never, router);
    };
    const first = bindClient();
    const second = bindClient();
    const firstStatuses: string[] = [];
    const secondStatuses: string[] = [];
    const disposeFirst = first.subscribe(
        status => firstStatuses.push(status.runId));
    const disposeSecond = second.subscribe(
        status => secondStatuses.push(status.runId));

    await first.create('Private', 'First client only', null, null);
    const firstRunId = await first.run('routine-2');
    await first.approve('run-approval', 'approval-1', true);
    const firstHistory = await first.history(null, 5);
    firstHistory[0]!.content = 'First client mutation';

    expect((await first.list()).map(routine => routine.id))
        .toEqual(['routine-briefing', 'routine-2']);
    expect((await second.list()).map(routine => routine.id))
        .toEqual(['routine-briefing']);
    expect((await first.statuses()).map(status => status.runId))
        .toEqual(['run-approval', firstRunId]);
    expect(await second.statuses()).toEqual([
      expect.objectContaining({
        runId: 'run-approval',
        state: 'awaiting_approval',
        revision: 3n,
      }),
    ]);
    expect((await first.history(null, 5))[0]?.content)
        .toBe('Briefing prepared with three priorities.');
    expect((await second.history(null, 5))[0]?.content)
        .toBe('Briefing prepared with three priorities.');
    expect(firstStatuses).toEqual([firstRunId, 'run-approval']);
    expect(secondStatuses).toEqual([]);

    const secondRunId = await second.run('routine-briefing');
    expect(firstStatuses).toEqual([firstRunId, 'run-approval']);
    expect(secondStatuses).toEqual([secondRunId]);
    disposeFirst();
    disposeSecond();
  });

  it('settles cold-open eligibility on the bound AI handler', async () => {
    const router = new PageCallbackRouter();
    const handler = new PageHandlerRemote();
    PageHandlerFactory.getRemote().createPageHandler(
        router.$.bindNewPipeAndPassRemote(),
        handler.$.bindNewPipeAndPassReceiver());
    const client = new RoutineOperationsClient(handler as never, router);

    await expect(client.isEligible()).resolves.toBe(true);
    await expect(Promise.all([
      client.list(),
      client.statuses(),
      client.history(null, 5),
    ])).resolves.toEqual([
      [expect.objectContaining({id: 'routine-briefing'})],
      [expect.objectContaining({runId: 'run-approval'})],
      [expect.objectContaining({resultId: 1n})],
    ]);
  });

  it('maps operations and rejects unknown status values', async () => {
    const handler = {
      listAllRoutines: vi.fn().mockResolvedValue({routines: [{id: 'daily'}]}),
      createRoutine: vi.fn().mockResolvedValue({ok: true}),
      startRoutine: vi.fn().mockResolvedValue({runId: 'run-1', error: null}),
      getRoutineRunStatuses: vi.fn().mockResolvedValue({
        statuses: [{
          runId: 'run-1',
          routineId: 'daily',
          source: 'manual',
          state: 2,
          revision: 3n,
          result: null,
          error: null,
          approval: {approvalId: 'approval-1'},
        }],
      }),
      respondToRoutineApproval: vi.fn().mockResolvedValue({ok: true}),
      listRunHistory: vi.fn().mockResolvedValue({records: [{resultId: 1n}]}),
    };
    const client = new RoutineOperationsClient(handler as never);

    await expect(client.list()).resolves.toEqual([{id: 'daily'}]);
    await expect(client.create('Daily', 'Prompt', null, null))
        .resolves.toBe(true);
    await expect(client.run('daily')).resolves.toBe('run-1');
    await expect(client.statuses()).resolves.toEqual([{
      runId: 'run-1',
      routineId: 'daily',
      source: 'manual',
      state: 'awaiting_approval',
      revision: 3n,
      result: null,
      error: null,
      approvalId: 'approval-1',
    }]);
    await expect(client.approve('run-1', 'approval-1', true))
        .resolves.toBe(true);
    await expect(client.history('daily', 5))
        .resolves.toEqual([{resultId: 1n}]);

    handler.getRoutineRunStatuses.mockResolvedValueOnce({
      statuses: [{
        runId: 'run-2',
        routineId: 'daily',
        source: 'manual',
        state: 99,
        revision: 1n,
        result: null,
        error: null,
        approval: null,
      }],
    });
    await expect(client.statuses()).rejects.toThrow(
        'Unknown Routine run state: 99');
  });

  it('maps and disposes live status callbacks', () => {
    let listener: ((status: Record<string, unknown>) => void)|undefined;
    const callbackRouter = {
      onRoutineRunStatusChanged: {
        addListener: vi.fn((callback: typeof listener) => {
          listener = callback;
          return 12;
        }),
      },
      removeListener: vi.fn(),
    };
    const client =
        new RoutineOperationsClient({} as never, callbackRouter as never);
    const onStatus = vi.fn();
    const dispose = client.subscribe(onStatus);
    listener?.({
      runId: 'run-live',
      routineId: 'daily',
      source: 'event',
      state: 3,
      revision: 9n,
      result: 'done',
      error: null,
      approval: null,
    });
    expect(onStatus).toHaveBeenCalledWith(expect.objectContaining({
      runId: 'run-live',
      state: 'succeeded',
      revision: 9n,
    }));
    dispose();
    expect(callbackRouter.removeListener).toHaveBeenCalledWith(12);
  });
});
