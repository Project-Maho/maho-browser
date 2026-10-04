import {useEffect, useState} from 'react';

import type {ControlActivitySnapshot} from '../../maho_ai.mojom-webui.js';
import {getMahoAiPageConnection} from '../../page_connection.js';

type ActivityMojo = ReturnType<typeof getMahoAiPageConnection>;

// The side panel WebUI can load before the browser window is fully ready, in
// which case the page handler pipe is fail-closed and watch calls are lost.
// Retry until the pipe answers, then share one live subscription across all
// card instances.
let shared: {mojo: ActivityMojo, listenerId: number}|null = null;
let sharedEntries: ControlActivitySnapshot[] = [];
const listeners = new Set<(entries: ControlActivitySnapshot[]) => void>();

function notify(): void {
  for (const listener of listeners) {
    listener(sharedEntries);
  }
}

function routerOf(mojo: ActivityMojo): {
  onControlActivityChanged: {
    addListener: (fn: (entries: ControlActivitySnapshot[]) => void) => number,
    removeListener: (id: number) => void,
  },
} {
  return mojo.router as unknown as {
    onControlActivityChanged: {
      addListener: (fn: (entries: ControlActivitySnapshot[]) => void) => number,
      removeListener: (id: number) => void,
    },
  };
}

async function connectWithRetry(): Promise<void> {
  for (let attempt = 0; attempt < 12; attempt++) {
    try {
      const mojo = getMahoAiPageConnection();
      const response = await mojo.handler.watchControlActivity();
      sharedEntries = response.entries;
      const id = routerOf(mojo).onControlActivityChanged.addListener(
          (entries: ControlActivitySnapshot[]) => {
            sharedEntries = entries;
            notify();
          });
      shared = {mojo, listenerId: id};
      notify();
      return;
    } catch (error: unknown) {
      await new Promise((resolve) => setTimeout(resolve, 2000));
    }
  }
}

export function useControlActivityTimeline():
    ControlActivitySnapshot[] {
  const [entries, setEntries] = useState<ControlActivitySnapshot[]>([]);

  useEffect(() => {
    listeners.add(setEntries);
    setEntries(sharedEntries);

    if (!shared) {
      void connectWithRetry();
    } else {
      setEntries(sharedEntries);
    }

    return () => {
      listeners.delete(setEntries);
    };
  }, []);

  return entries;
}
