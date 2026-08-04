import {PageHandlerFactory, PageHandlerRemote, PageRemote, RoutineInfo, RoutineRunResult} from './maho_routines.mojom-webui.js';

interface PageHandler {
  listRoutines(): Promise<{routines: RoutineInfo[]}>;
  listAllRoutines(): Promise<{routines: RoutineInfo[]}>;
  createRoutine(
      name: string, prompt: string, schedule: string | null,
      trigger: string | null): Promise<{ok: boolean}>;
  deleteRoutine(id: string): Promise<{ok: boolean}>;
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

function esc(text: string): string {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

// Human-readable trigger label for a routine card.
function triggerLabel(routine: RoutineInfo): string {
  if (routine.schedule) {
    return `cron: ${routine.schedule}`;
  }
  if (routine.trigger) {
    return `event: ${routine.trigger}`;
  }
  if (routine.cron) {
    return `cron: ${routine.cron}`;
  }
  return 'manual run only';
}

function renderUpgrade(app: HTMLElement) {
  app.innerHTML = `
    <div style="padding:40px;text-align:center;font-family:system-ui">
      <h1>Maho Routines</h1>
      <div style="background:#f8f4e8;border:1px solid #e8d48b;border-radius:12px;padding:24px;max-width:400px;margin:24px auto">
        <p style="font-size:18px;margin:0 0 12px">Available on Max tier</p>
        <p style="color:#666;margin:0 0 16px">Upgrade to Max to unlock automated browser routines.</p>
        <a href="chrome://maho-settings?pane=billing" style="display:inline-block;padding:10px 24px;background:#1a1a1a;color:#fff;border-radius:8px;text-decoration:none">Upgrade to Max</a>
      </div>
    </div>`;
}

function renderCard(routine: RoutineInfo): HTMLElement {
  const card = document.createElement('div');
  card.style.cssText = 'border:1px solid #e0e0e0;border-radius:12px;padding:20px;margin-bottom:16px';
  const badge = routine.isCustom ?
      `<span style="font-size:11px;font-weight:600;color:#7a5cff;background:#efeaff;border-radius:6px;padding:2px 8px;margin-left:8px">CUSTOM</span>` :
      '';
  const deleteBtn = routine.isCustom ?
      `<button class="delete-btn" data-id="${esc(routine.id)}" title="Delete routine" style="padding:8px 12px;background:#fff;color:#c0392b;border:1px solid #f0c9c3;border-radius:6px;cursor:pointer">Delete</button>` :
      '';
  card.innerHTML = `
    <div style="display:flex;justify-content:space-between;align-items:start;gap:12px">
      <div style="min-width:0">
        <h3 style="margin:0 0 4px">${esc(routine.name)}${badge}</h3>
        <p style="color:#666;margin:0 0 8px;font-size:14px">${esc(routine.description)}</p>
        <code style="font-size:12px;color:#999">${esc(triggerLabel(routine))}</code>
      </div>
      <div style="display:flex;gap:8px;flex-shrink:0">
        ${deleteBtn}
        <button class="run-btn" data-id="${esc(routine.id)}" style="padding:8px 16px;background:#1a1a1a;color:#fff;border:none;border-radius:6px;cursor:pointer;white-space:nowrap">Run Now</button>
      </div>
    </div>
    <div class="result" id="result-${esc(routine.id)}" style="margin-top:12px;display:none;padding:12px;background:#f5f5f5;border-radius:8px;font-size:13px;white-space:pre-wrap"></div>`;
  return card;
}

async function refreshList() {
  const {routines} = await getHandler().listAllRoutines();
  const list = document.getElementById('routine-list')!;
  list.innerHTML = '';
  for (const routine of routines) {
    list.appendChild(renderCard(routine));
  }
}

async function renderApp() {
  const app = document.getElementById('app')!;
  const {tier} = await getHandler().getUserTier();

  if (tier < 2) {
    renderUpgrade(app);
    return;
  }

  app.innerHTML = `
    <div style="padding:40px;font-family:system-ui;max-width:640px;margin:0 auto">
      <h1 style="margin:0 0 8px">Routines</h1>
      <p style="color:#666;margin:0 0 24px">Automated browser tasks that run on a schedule or in response to browser events. Max tier only.</p>

      <details id="new-routine" style="border:1px solid #e0e0e0;border-radius:12px;padding:16px 20px;margin-bottom:24px">
        <summary style="cursor:pointer;font-weight:600">New Routine</summary>
        <div style="margin-top:16px;display:flex;flex-direction:column;gap:12px">
          <label style="font-size:13px;color:#444">Name
            <input id="nr-name" type="text" placeholder="Weekly digest" style="display:block;width:100%;box-sizing:border-box;margin-top:4px;padding:8px;border:1px solid #ccc;border-radius:6px">
          </label>
          <label style="font-size:13px;color:#444">Instructions (prompt)
            <textarea id="nr-prompt" rows="3" placeholder="Summarize my open tabs and email me the digest." style="display:block;width:100%;box-sizing:border-box;margin-top:4px;padding:8px;border:1px solid #ccc;border-radius:6px;font-family:inherit"></textarea>
          </label>
          <label style="font-size:13px;color:#444">Trigger type
            <select id="nr-trigger-type" style="display:block;width:100%;box-sizing:border-box;margin-top:4px;padding:8px;border:1px solid #ccc;border-radius:6px">
              <option value="cron">Schedule (cron)</option>
              <option value="on_startup">Event: on startup</option>
              <option value="on_many_tabs">Event: too many tabs</option>
            </select>
          </label>
          <div id="nr-cron-row">
            <label style="font-size:13px;color:#444">Cron schedule
              <input id="nr-cron" type="text" placeholder="0 9 * * 1" value="0 9 * * *" style="display:block;width:100%;box-sizing:border-box;margin-top:4px;padding:8px;border:1px solid #ccc;border-radius:6px;font-family:monospace">
            </label>
            <p style="font-size:12px;color:#999;margin:4px 0 0">Format: minute hour day-of-month month day-of-week</p>
          </div>
          <div id="nr-threshold-row" style="display:none">
            <label style="font-size:13px;color:#444">Tab count threshold
              <input id="nr-threshold" type="number" min="1" value="20" style="display:block;width:100%;box-sizing:border-box;margin-top:4px;padding:8px;border:1px solid #ccc;border-radius:6px">
            </label>
          </div>
          <div>
            <button id="nr-create" style="padding:10px 20px;background:#1a1a1a;color:#fff;border:none;border-radius:6px;cursor:pointer">Create Routine</button>
            <span id="nr-error" style="color:#c0392b;font-size:13px;margin-left:12px"></span>
          </div>
        </div>
      </details>

      <div id="routine-list"></div>
    </div>`;

  // Toggle trigger-specific inputs.
  const triggerType = document.getElementById('nr-trigger-type') as HTMLSelectElement;
  const cronRow = document.getElementById('nr-cron-row')!;
  const thresholdRow = document.getElementById('nr-threshold-row')!;
  triggerType.addEventListener('change', () => {
    cronRow.style.display = triggerType.value === 'cron' ? 'block' : 'none';
    thresholdRow.style.display =
        triggerType.value === 'on_many_tabs' ? 'block' : 'none';
  });

  // Create handler.
  const createBtn = document.getElementById('nr-create') as HTMLButtonElement;
  const errorSpan = document.getElementById('nr-error')!;
  createBtn.addEventListener('click', async () => {
    errorSpan.textContent = '';
    const name = (document.getElementById('nr-name') as HTMLInputElement).value.trim();
    const prompt = (document.getElementById('nr-prompt') as HTMLTextAreaElement).value.trim();
    if (!name || !prompt) {
      errorSpan.textContent = 'Name and instructions are required.';
      return;
    }
    let schedule: string | null = null;
    let trigger: string | null = null;
    const type = triggerType.value;
    if (type === 'cron') {
      schedule = (document.getElementById('nr-cron') as HTMLInputElement).value.trim();
      if (!schedule) {
        errorSpan.textContent = 'A cron schedule is required.';
        return;
      }
    } else if (type === 'on_startup') {
      trigger = 'on_startup';
    } else if (type === 'on_many_tabs') {
      const threshold = (document.getElementById('nr-threshold') as HTMLInputElement).value.trim();
      trigger = `on_many_tabs:${threshold || '20'}`;
    }

    createBtn.disabled = true;
    createBtn.textContent = 'Creating...';
    const {ok} = await getHandler().createRoutine(name, prompt, schedule, trigger);
    createBtn.disabled = false;
    createBtn.textContent = 'Create Routine';
    if (!ok) {
      errorSpan.textContent = 'Could not create routine. Check the schedule/trigger format.';
      return;
    }
    // Reset the form and refresh.
    (document.getElementById('nr-name') as HTMLInputElement).value = '';
    (document.getElementById('nr-prompt') as HTMLTextAreaElement).value = '';
    (document.getElementById('new-routine') as HTMLDetailsElement).open = false;
    await refreshList();
  });

  // Delegated run / delete handlers.
  const list = document.getElementById('routine-list')!;
  list.addEventListener('click', async (e: Event) => {
    const target = e.target as HTMLElement;

    const deleteBtn = target.closest<HTMLButtonElement>('.delete-btn');
    if (deleteBtn) {
      const id = deleteBtn.dataset['id'] ?? '';
      deleteBtn.disabled = true;
      const {ok} = await getHandler().deleteRoutine(id);
      if (ok) {
        await refreshList();
      } else {
        deleteBtn.disabled = false;
      }
      return;
    }

    const runBtn = target.closest<HTMLButtonElement>('.run-btn');
    if (!runBtn) {
      return;
    }
    const id = runBtn.dataset['id'] ?? '';
    runBtn.disabled = true;
    runBtn.textContent = 'Running...';
    const resultDiv = document.getElementById(`result-${id}`)!;

    const {result, error} = await getHandler().runRoutine(id);
    if (error) {
      resultDiv.style.display = 'block';
      resultDiv.style.background = '#fef2f2';
      resultDiv.textContent = `Error: ${error}`;
    } else if (result) {
      resultDiv.style.display = 'block';
      resultDiv.style.background = '#f5f5f5';
      resultDiv.textContent = result.content;
    }
    runBtn.disabled = false;
    runBtn.textContent = 'Run Now';
  });

  await refreshList();
}

renderApp();
