import {describe, expect, it} from 'vitest';

import {PageHandlerFactory} from '../../maho_ai.mojom-webui.js';
import {getMahoAiPageConnection} from '../../page_connection.js';
import {createMahoAiStore} from '../../store.js';

function countCreatePageHandlerCalls(run: () => void|Promise<void>):
    Promise<number> {
  let created = 0;
  const factory = PageHandlerFactory as unknown as
      {getRemote(): {createPageHandler(r: unknown, h: unknown): void}};
  const originalGetRemote = factory.getRemote;
  factory.getRemote = () => {
    const remote = originalGetRemote.call(factory);
    return {
      createPageHandler(router: unknown, handler: unknown) {
        created += 1;
        remote.createPageHandler(router, handler);
      },
    };
  };
  return Promise.resolve(run())
      .then(() => created)
      .finally(() => {
        factory.getRemote = originalGetRemote;
      });
}

describe('chrome://maho-ai page-handler connection', () => {
  it('binds one page handler for the store, voice and control-activity callers',
      async () => {
        const created = await countCreatePageHandlerCalls(async () => {
          getMahoAiPageConnection();
          await import('../hooks/use-voice-input.js');
          await import('../hooks/use-control-activity.js');
          createMahoAiStore();
          createMahoAiStore();
        });

        expect(created).toBeLessThanOrEqual(1);
      });

  it('hands every caller the same handler and router instance', () => {
    const connection = getMahoAiPageConnection();
    const store = createMahoAiStore();

    expect(store.getRoutineOperationsClient()).toBeDefined();
    expect(getMahoAiPageConnection().handler).toBe(connection.handler);
    expect(getMahoAiPageConnection().router).toBe(connection.router);
  });
});
