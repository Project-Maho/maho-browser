// Copyright 2026 Maho Browser. All rights reserved.

// `MahoAIUI::CreatePageHandler` owns a single `page_handler_`, so a second
// `createPageHandler()` from the same document destroys the first handler and
// closes its pipe. Every chrome://maho-ai caller must share this one pair.

import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from './maho_ai.mojom-webui.js';
import {bindMojoPageHandler} from '../maho_common/react/use_mojo.js';

export interface MahoAiPageConnection {
  readonly router: PageCallbackRouter;
  readonly handler: PageHandlerRemote;
}

let connection: MahoAiPageConnection|null = null;

export function getMahoAiPageConnection(): MahoAiPageConnection {
  if (!connection) {
    connection = bindMojoPageHandler(
        PageCallbackRouter, PageHandlerRemote, PageHandlerFactory);
  }
  return connection;
}
