export const RELAY = "http://127.0.0.1:18765";
export const CDP_HOST = "http://127.0.0.1:9222";
export const CDP_URL_ROOT = "chrome://maho-settings/?pane=";

export const RESET = "\x1b[0m";
export const GREEN = "\x1b[32m";
export const RED = "\x1b[31m";
export const CYAN = "\x1b[36m";

export let passed = 0;
export let failed = 0;
export const failures: string[] = [];

export function assert(cond: unknown, label: string): void {
  if (cond) {
    console.log(`${GREEN}  ✓${RESET} ${label}`);
    passed++;
  } else {
    console.log(`${RED}  ✗${RESET} ${label}`);
    failures.push(label);
    failed++;
  }
}

export function step(label: string): void {
  console.log(`\n${CYAN}=== ${label} ===${RESET}`);
}

type CDPTargetInfo = {readonly type: string; readonly url: string};
type BrowserSocket = Pick<WebSocket, "addEventListener" | "removeEventListener" | "send">;

function waitForPageTarget(
  ws: BrowserSocket,
  trigger: () => Promise<void>,
  matches: (target: CDPTargetInfo) => boolean,
  timeoutMessage: string,
  timeoutMs: number,
): Promise<CDPTargetInfo> {
  if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) {
    return Promise.reject(new Error(`Invalid target wait timeout: ${timeoutMs}`));
  }
  return new Promise<CDPTargetInfo>((resolve, reject) => {
    const commandId = 1;
    let settled = false;
    const finish = (error?: Error, target?: CDPTargetInfo) => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      ws.removeEventListener("message", onMessage);
      if (error) reject(error);
      else if (target) resolve(target);
      else reject(new Error("Target wait completed without a target"));
    };
    const onMessage = (event: MessageEvent) => {
      const message = JSON.parse(String(event.data));
      if (message.id === commandId) {
        if (message.error) {
          finish(new Error(`Target.setDiscoverTargets failed: ${message.error.message ?? "unknown CDP error"}`));
        } else {
          void trigger().catch((error: unknown) => finish(error instanceof Error ? error : new Error(String(error))));
        }
        return;
      }
      if (message.method !== "Target.targetCreated" && message.method !== "Target.targetInfoChanged") return;
      const target = message.params?.targetInfo as CDPTargetInfo | undefined;
      if (target?.type === "page" && matches(target)) finish(undefined, target);
    };
    const timeout = setTimeout(() => finish(new Error(timeoutMessage)), timeoutMs);
    ws.addEventListener("message", onMessage);
    ws.send(JSON.stringify({id: commandId, method: "Target.setDiscoverTargets", params: {discover: true}}));
  });
}

export function waitForSettingsTarget(
  ws: BrowserSocket,
  createTarget: () => Promise<void>,
  timeoutMs = 10_000,
): Promise<CDPTargetInfo> {
  return waitForPageTarget(
    ws,
    createTarget,
    target => target.url.includes("maho-settings"),
    "Timed out waiting for maho-settings target",
    timeoutMs,
  );
}

export async function waitForPageTargetUrl(
  urlPrefix: string,
  trigger: () => Promise<void>,
  timeoutMs = 10_000,
): Promise<CDPTargetInfo> {
  const versionResponse = await fetch(`${CDP_HOST}/json/version`);
  if (!versionResponse.ok) {
    throw new Error(`Failed to read browser CDP version: HTTP ${versionResponse.status}`);
  }
  const version = await versionResponse.json() as {webSocketDebuggerUrl?: string};
  if (!version.webSocketDebuggerUrl) throw new Error("Browser CDP websocket URL is unavailable");
  const browserWs = await openWebSocket(version.webSocketDebuggerUrl);
  try {
    return await waitForPageTarget(
      browserWs,
      trigger,
      target => target.url.startsWith(urlPrefix),
      `Timed out waiting for page target URL prefix ${urlPrefix}`,
      timeoutMs,
    );
  } finally {
    browserWs.close();
  }
}

async function openWebSocket(url: string): Promise<WebSocket> {
  const ws = new WebSocket(url);
  await new Promise<void>((resolve, reject) => {
    ws.addEventListener("open", () => resolve(), {once: true});
    ws.addEventListener("error", () => reject(new Error(`Failed to open CDP websocket ${url}`)), {once: true});
  });
  return ws;
}

export async function getSettingsWs(): Promise<WebSocket> {
  const targets = await (await fetch(`${CDP_HOST}/json`)).json();
  let t = targets.find((x: any) => x.url.includes("maho-settings"));
  if (!t) {
    const version = await (await fetch(`${CDP_HOST}/json/version`)).json();
    const browserWs = await openWebSocket(version.webSocketDebuggerUrl);
    try {
      await waitForSettingsTarget(browserWs, async () => {
        const response = await fetch(`${CDP_HOST}/json/new?chrome://maho-settings/?pane=account`, {method: "PUT"});
        if (!response.ok) throw new Error(`Failed to create maho-settings target: HTTP ${response.status}`);
      });
    } finally {
      browserWs.close();
    }
    const retry = await (await fetch(`${CDP_HOST}/json`)).json();
    t = retry.find((x: any) => x.url.includes("maho-settings"));
  }
  if (!t) throw new Error("no maho-settings tab");
  return openWebSocket(t.webSocketDebuggerUrl);
}

type CDPEventListener = (params: any) => void;

export class CDP {
  ws: WebSocket;
  id = 0;
  pending = new Map<number, (msg: any) => void>();
  eventListeners = new Map<string, Set<CDPEventListener>>();
  constructor(ws: WebSocket) {
    this.ws = ws;
    ws.onmessage = (e) => {
      const m = JSON.parse(e.data);
      if (m.id && this.pending.has(m.id)) {
        this.pending.get(m.id)!(m);
        this.pending.delete(m.id);
        return;
      }
      if (m.method) {
        for (const listener of this.eventListeners.get(m.method) ?? []) {
          listener(m.params);
        }
      }
    };
  }
  on(method: string, listener: CDPEventListener): void {
    const listeners = this.eventListeners.get(method) ?? new Set<CDPEventListener>();
    listeners.add(listener);
    this.eventListeners.set(method, listeners);
  }
  off(method: string, listener: CDPEventListener): void {
    const listeners = this.eventListeners.get(method);
    listeners?.delete(listener);
    if (listeners?.size === 0) this.eventListeners.delete(method);
  }
  send(method: string, params: any = {}): Promise<any> {
    const i = ++this.id;
    return new Promise((res) => {
      this.pending.set(i, res);
      this.ws.send(JSON.stringify({id: i, method, params}));
    });
  }
  async eval<T = any>(expr: string, awaitPromise = false): Promise<T | undefined> {
    const r = await this.send("Runtime.evaluate", {expression: expr, returnByValue: true, awaitPromise});
    if (r?.error) {
      throw new Error(r.error.message ?? "Runtime.evaluate failed with an unknown CDP error");
    }
    if (r?.result?.exceptionDetails) {
      const details = r.result.exceptionDetails;
      throw new Error(details.exception?.description ?? details.text ?? "Runtime.evaluate failed with an exception");
    }
    return r?.result?.result?.value;
  }
  async navigate(url: string, _waitMs = 3500): Promise<void> {
    await waitForCDPEvent(this, "Page.loadEventFired", () => this.send("Page.navigate", {url}));
  }
}

export function waitForCDPEvent<T>(cdp: CDP, method: string, trigger: () => Promise<T>, timeoutMs = 10_000): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    let result: T | undefined;
    let eventReceived = false;
    let triggerCompleted = false;
    let settled = false;
    const finish = (error?: Error) => {
      if (settled) return;
      if (!error && (!eventReceived || !triggerCompleted)) return;
      settled = true;
      clearTimeout(timeout);
      cdp.off(method, onEvent);
      if (error) reject(error); else resolve(result!);
    };
    const onEvent: CDPEventListener = () => { eventReceived = true; finish(); };
    const timeout = setTimeout(() => finish(new Error(`Timed out waiting for CDP event ${method}`)), timeoutMs);
    cdp.on(method, onEvent);
    void trigger().then((value) => { result = value; triggerCompleted = true; finish(); })
      .catch((error: unknown) => finish(error instanceof Error ? error : new Error(String(error))));
  });
}

type ElementCenter = {
  readonly x: number;
  readonly y: number;
};

export async function clickElementByText(
  cdp: CDP,
  selector: string,
  exactText: string
): Promise<boolean> {
  const center = await cdp.eval<ElementCenter>(`(function(){
    var elements = Array.from(document.querySelectorAll(${JSON.stringify(selector)}));
    var element = elements.find(function(candidate){
      return (candidate.textContent || '').trim() === ${JSON.stringify(exactText)};
    });
    if (!element) return undefined;
    var rect = element.getBoundingClientRect();
    return {x: rect.left + rect.width / 2, y: rect.top + rect.height / 2};
  })()`);
  if (!center) return false;

  await cdp.send("Input.dispatchMouseEvent", {
    type: "mousePressed",
    x: center.x,
    y: center.y,
    button: "left",
    clickCount: 1
  });
  await cdp.send("Input.dispatchMouseEvent", {
    type: "mouseReleased",
    x: center.x,
    y: center.y,
    button: "left",
    clickCount: 1
  });
  return true;
}

type CDPFrame = {
  readonly url: string;
  readonly parentId?: string;
};

type CDPFrameTree = {
  readonly frame: CDPFrame;
  readonly childFrames?: readonly CDPFrameTree[];
};

function findChildFrameUrl(frameTree: CDPFrameTree, urlPrefix: string): string | undefined {
  for (const child of frameTree.childFrames ?? []) {
    if (child.frame.url.startsWith(urlPrefix)) return child.frame.url;
    const match = findChildFrameUrl(child, urlPrefix);
    if (match) return match;
  }
  return undefined;
}

export function waitForFrameUrl(
  cdp: CDP,
  urlPrefix: string,
  timeoutMs = 10_000,
): Promise<string> {
  if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) {
    return Promise.reject(new Error(`Invalid frame wait timeout: ${timeoutMs}`));
  }
  return new Promise<string>((resolve, reject) => {
    let settled = false;
    const finish = (error: Error | undefined, url?: string) => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      cdp.off("Page.frameNavigated", onFrameNavigated);
      if (error) reject(error);
      else resolve(url!);
    };
    const onFrameNavigated: CDPEventListener = (params) => {
      const frame = params?.frame as CDPFrame | undefined;
      if (
        typeof frame?.parentId === "string" &&
        typeof frame.url === "string" &&
        frame.url.startsWith(urlPrefix)
      ) {
        finish(undefined, frame.url);
      }
    };
    const timeout = setTimeout(() => {
      finish(new Error(`Timed out waiting for frame URL prefix ${urlPrefix}`));
    }, timeoutMs);

    cdp.on("Page.frameNavigated", onFrameNavigated);
    void cdp.send("Page.getFrameTree").then((response) => {
      if (response?.error) {
        finish(new Error(`Page.getFrameTree failed: ${response.error.message ?? "unknown CDP error"}`));
        return;
      }
      const frameTree = response?.result?.frameTree as CDPFrameTree | undefined;
      if (!frameTree) {
        finish(new Error("Page.getFrameTree returned no frame tree"));
        return;
      }
      const url = findChildFrameUrl(frameTree, urlPrefix);
      if (url) finish(undefined, url);
    }).catch((error: unknown) => {
      finish(error instanceof Error ? error : new Error(String(error)));
    });
  });
}

export async function waitForDomMutation<T>(
  cdp: CDP,
  predicateExpression: string,
  trigger: () => Promise<T>,
  timeoutMs = 10_000,
): Promise<T> {
  if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) {
    throw new Error(`Invalid DOM mutation wait timeout: ${timeoutMs}`);
  }
  const token = `task12-${crypto.randomUUID()}`;
  const armed = await cdp.eval<boolean>(`(function(){
    const token = ${JSON.stringify(token)};
    const predicate = function(){ return !!(${predicateExpression}); };
    window.__mahoTask12DomWaits = window.__mahoTask12DomWaits || new Map();
    if (window.__mahoTask12DomWaits.has(token)) return false;
    let resolveWait;
    let rejectWait;
    const promise = new Promise((resolve, reject) => { resolveWait = resolve; rejectWait = reject; });
    const observer = new MutationObserver(() => {
      try {
        if (predicate()) finish(true);
      } catch (error) {
        finish(false, error instanceof Error ? error.message : String(error));
      }
    });
    const timeout = setTimeout(() => finish(false, 'Timed out waiting for DOM mutation condition'), ${timeoutMs});
    function finish(ok, message) {
      clearTimeout(timeout);
      observer.disconnect();
      window.__mahoTask12DomWaits.delete(token);
      if (ok) resolveWait(true); else rejectWait(new Error(message));
    }
    window.__mahoTask12DomWaits.set(token, promise);
    observer.observe(document.documentElement, {attributes: true, childList: true, characterData: true, subtree: true});
    try { if (predicate()) finish(true); } catch (error) { finish(false, String(error)); }
    return true;
  })()`);
  if (!armed) throw new Error('Failed to arm DOM mutation wait');

  try {
    const result = await trigger();
    await cdp.eval(`window.__mahoTask12DomWaits.get(${JSON.stringify(token)})`, true);
    return result;
  } catch (error) {
    await cdp.eval(`(function(){
      const waits = window.__mahoTask12DomWaits;
      if (waits) waits.delete(${JSON.stringify(token)});
    })()`);
    throw error;
  }
}

export async function waitForCondition(
  cdp: CDP,
  expression: string,
  timeoutMs = 10_000
): Promise<boolean> {
  try {
    await waitForDomMutation(cdp, expression, async () => {}, timeoutMs);
    return true;
  } catch (error) {
    if (error instanceof Error && error.message.includes("Timed out waiting for DOM mutation condition")) return false;
    throw error;
  }
}

export async function waitForSettingsPane(
  cdp: CDP,
  paneKey: string,
  timeoutMs = 5_000
): Promise<boolean> {
  return waitForCondition(cdp, `(function(){
    var active = document.querySelector('nav[aria-label="Settings panes"] [aria-current="page"]');
    var section = document.querySelector('main [data-pane=${JSON.stringify(paneKey)}]');
    return !!active && active.getAttribute('data-pane') === ${JSON.stringify(paneKey)} && !!section;
  })()`, timeoutMs);
}

export async function checkRelayOrExit(): Promise<void> {
  const healthOk = await fetch(`${RELAY}/health`).then(r => r.text()).catch(() => "");
  if (healthOk.trim() !== "ok") {
    console.error(`${RED}Relay not reachable at ${RELAY}. Start it first.${RESET}`);
    process.exit(2);
  }
}

export async function checkBrowserOrExit(): Promise<void> {
  const reachable = await fetch(`${CDP_HOST}/json/version`).then(() => true).catch(() => false);
  if (!reachable) {
    console.error(`${RED}Browser CDP not reachable at ${CDP_HOST}. Start Maho with --remote-debugging-port=9222.${RESET}`);
    process.exit(2);
  }
}

export function reportResultsAndExit(): void {
  console.log(`\n${CYAN}=== Result ===${RESET}`);
  console.log(`${GREEN}Passed: ${passed}${RESET}`);
  console.log(`${RED}Failed: ${failed}${RESET}`);
  if (failed > 0) {
    console.log(`${RED}Failures:${RESET}`);
    for (const f of failures) console.log(`  - ${f}`);
    process.exit(1);
  }
  process.exit(0);
}
