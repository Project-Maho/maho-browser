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

export async function getSettingsWs(): Promise<WebSocket> {
  const targets = await (await fetch(`${CDP_HOST}/json`)).json();
  let t = targets.find((x: any) => x.url.includes("maho-settings"));
  if (!t) {
    await fetch(`${CDP_HOST}/json/new?chrome://maho-settings/?pane=account`, {method: "PUT"});
    await new Promise(r => setTimeout(r, 3000));
    const retry = await (await fetch(`${CDP_HOST}/json`)).json();
    t = retry.find((x: any) => x.url.includes("maho-settings"));
  }
  if (!t) throw new Error("no maho-settings tab");
  const ws = new WebSocket(t.webSocketDebuggerUrl);
  await new Promise<void>(r => (ws.onopen = () => r()));
  return ws;
}

export class CDP {
  ws: WebSocket;
  id = 0;
  pending = new Map<number, (msg: any) => void>();
  constructor(ws: WebSocket) {
    this.ws = ws;
    ws.onmessage = (e) => {
      const m = JSON.parse(e.data);
      if (m.id && this.pending.has(m.id)) {
        this.pending.get(m.id)!(m);
        this.pending.delete(m.id);
      }
    };
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
    return r?.result?.result?.value;
  }
  async navigate(url: string, waitMs = 3500): Promise<void> {
    await this.send("Page.navigate", {url});
    await new Promise(r => setTimeout(r, waitMs));
  }
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

export async function waitForCondition(
  cdp: CDP,
  expression: string,
  timeoutMs = 10_000
): Promise<boolean> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await cdp.eval<boolean>(expression)) return true;
    await Bun.sleep(100);
  }
  return false;
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
