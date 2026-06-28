import {PageHandlerFactory, PageHandlerRemote, PageRemote, RoutineInfo, RoutineRunResult} from './maho_routines.mojom-webui.js';

interface PageHandler {
  listRoutines(): Promise<{routines: RoutineInfo[]}>;
  runRoutine(id: string): Promise<{result: RoutineRunResult | null; error: string | null}>;
  getUserTier(): Promise<{tier: number}>;
}

let handler: PageHandler | null = null;

function getHandler(): PageHandler {
  if (!handler) {
    const factory = PageHandlerFactory.getRemote();
    const pageHandler = new PageHandlerRemote();
    const page = new PageRemote();
    // @ts-ignore Mojo generated binding mismatch
    factory.createPageHandler(page.$.bindNewPipeAndPassRemote(), pageHandler.$.bindNewPipeAndPassReceiver());
    handler = pageHandler as unknown as PageHandler;
  }
  return handler;
}

async function renderApp() {
  const app = document.getElementById('app')!;
  const {tier} = await getHandler().getUserTier();

  if (tier < 2) {
    app.innerHTML = `
      <div style="padding:40px;text-align:center;font-family:system-ui">
        <h1>Maho Routines</h1>
        <div style="background:#f8f4e8;border:1px solid #e8d48b;border-radius:12px;padding:24px;max-width:400px;margin:24px auto">
          <p style="font-size:18px;margin:0 0 12px">Available on Max tier</p>
          <p style="color:#666;margin:0 0 16px">Upgrade to Max to unlock automated browser routines.</p>
          <a href="chrome://maho-settings#pricing" style="display:inline-block;padding:10px 24px;background:#1a1a1a;color:#fff;border-radius:8px;text-decoration:none">Upgrade to Max</a>
        </div>
      </div>`;
    return;
  }

  const {routines} = await getHandler().listRoutines();

  app.innerHTML = `
    <div style="padding:40px;font-family:system-ui;max-width:640px;margin:0 auto">
      <h1 style="margin:0 0 8px">Routines</h1>
      <p style="color:#666;margin:0 0 32px">Automated browser tasks that run on schedule. Max tier only.</p>
      <div id="routine-list"></div>
    </div>`;

  const list = document.getElementById('routine-list')!;
  for (const routine of routines) {
    const card = document.createElement('div');
    card.style.cssText = 'border:1px solid #e0e0e0;border-radius:12px;padding:20px;margin-bottom:16px';
    card.innerHTML = `
      <div style="display:flex;justify-content:space-between;align-items:start">
        <div>
          <h3 style="margin:0 0 4px">${routine.name}</h3>
          <p style="color:#666;margin:0 0 8px;font-size:14px">${routine.description}</p>
          <code style="font-size:12px;color:#999">${routine.cron}</code>
        </div>
        <button class="run-btn" data-id="${routine.id}" style="padding:8px 16px;background:#1a1a1a;color:#fff;border:none;border-radius:6px;cursor:pointer;white-space:nowrap">Run Now</button>
      </div>
      <div class="result" id="result-${routine.id}" style="margin-top:12px;display:none;padding:12px;background:#f5f5f5;border-radius:8px;font-size:13px;white-space:pre-wrap"></div>`;
    list.appendChild(card);
  }

  list.addEventListener('click', async (e: Event) => {
    const btn = (e.target as HTMLElement).closest<HTMLButtonElement>('.run-btn');
    if (!btn) return;
    const id = btn.dataset['id'] ?? '';
    btn.disabled = true;
    btn.textContent = 'Running...';
    const resultDiv = document.getElementById(`result-${id}`)!;

    const {result, error} = await getHandler().runRoutine(id);
    if (error) {
      resultDiv.style.display = 'block';
      resultDiv.style.background = '#fef2f2';
      resultDiv.textContent = `Error: ${error}`;
    } else if (result) {
      resultDiv.style.display = 'block';
      resultDiv.textContent = result.content;
    }
    btn.disabled = false;
    btn.textContent = 'Run Now';
  });
}

renderApp();
