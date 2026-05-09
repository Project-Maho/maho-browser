import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from './maho_ai.mojom-webui.js';
import {MahoAiStore} from './store.js';
import {renderConsoleShell} from './views/console_shell.js';

function createStore(): MahoAiStore {
  const callbackRouter = new PageCallbackRouter();
  const pageHandler = new PageHandlerRemote();

  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      pageHandler.$.bindNewPipeAndPassReceiver());

  return new MahoAiStore(pageHandler, callbackRouter);
}

async function initialize() {
  const app = document.getElementById('app');
  if (!app) {
    throw new Error('Missing #app root');
  }

  const store = createStore();
  const initialPrompt = (() => {
    try {
      return loadTimeData.getString('pendingAiQuery').trim();
    } catch {
      return '';
    }
  })();

  renderConsoleShell(app, store);
  await store.bootstrap(initialPrompt || undefined);
}

document.addEventListener('DOMContentLoaded', () => {
  initialize().catch((error: unknown) => {
    const app = document.getElementById('app');
    if (!app) {
      return;
    }

    app.replaceChildren();
    const card = document.createElement('section');
    card.className = 'boot-error';

    const title = document.createElement('h1');
    title.textContent = 'Maho AI failed to start';

    const body = document.createElement('pre');
    body.textContent =
        error instanceof Error ? `${error.message}\n${error.stack || ''}` : String(error);

    card.append(title, body);
    app.appendChild(card);
    console.error('[maho-ai] Bootstrap failed:', error);
  });
});
