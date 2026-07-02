#!/usr/bin/env bun
// CDP E2E verification harness for maho_settings auth/billing flow.
// Usage:
//   1. Start local relay:
//        MAHO_RELAY_SERVICE_TOKEN=qa-test \
//          maho/relay/target/release/maho-relay \
//          --port 18765 --db-path /tmp/relay.db --open-registration \
//          --min-password-length 8 &
//   2. Signup test user:
//        curl -X POST http://127.0.0.1:18765/auth/signup \
//          -H "Content-Type: application/json" \
//          -d '{"email":"qa@maho.local","password":"test12345678","display_name":"QA","device_name":"CDP","device_type":"desktop"}'
//   3. Launch Maho with MAHO_RELAY_URL=http://127.0.0.1:18765 --remote-debugging-port=9222
//   4. Run this script: bunx tsx .tmp/test_settings_auth_billing_flow.ts

const RELAY = "http://127.0.0.1:18765";
const CDP_HOST = "http://127.0.0.1:9222";
const CDP_URL_ROOT = "chrome://maho-settings/?pane=";

const RESET = "\x1b[0m";
const GREEN = "\x1b[32m";
const RED = "\x1b[31m";
const CYAN = "\x1b[36m";

let passed = 0;
let failed = 0;
const failures: string[] = [];

function assert(cond: unknown, label: string): void {
  if (cond) {
    console.log(`${GREEN}  ✓${RESET} ${label}`);
    passed++;
  } else {
    console.log(`${RED}  ✗${RESET} ${label}`);
    failures.push(label);
    failed++;
  }
}

function step(label: string): void {
  console.log(`\n${CYAN}=== ${label} ===${RESET}`);
}

async function getSettingsWs(): Promise<WebSocket> {
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

class CDP {
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

async function main() {
  console.log(`${CYAN}Maho Settings E2E harness${RESET}`);
  console.log(`Relay: ${RELAY}`);
  console.log(`CDP:   ${CDP_HOST}`);

  const healthOk = await fetch(`${RELAY}/health`).then(r => r.text()).catch(() => "");
  if (healthOk.trim() !== "ok") {
    console.error(`${RED}Relay not reachable at ${RELAY}. Start it first.${RESET}`);
    process.exit(2);
  }

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise(r => setTimeout(r, 500));

  step("Step 1 — Sign in (Bug #1, #2)");
  await cdp.navigate(`${CDP_URL_ROOT}account`, 4000);
  const filled = await cdp.eval<boolean>(`(function(){
    var email = document.querySelector('main input[type="email"]');
    var pw = document.querySelector('main input[type="password"]');
    if (!email || !pw) return false;
    var setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
    setter.call(email, 'qa@maho.local');
    email.dispatchEvent(new Event('input', {bubbles: true}));
    setter.call(pw, 'test12345678');
    pw.dispatchEvent(new Event('input', {bubbles: true}));
    var btns = document.querySelectorAll('main button');
    for (var b of btns) if (b.textContent.trim() === 'Sign in') { b.click(); return true; }
    return false;
  })()`);
  assert(filled === true, "Sign-in form filled and submitted");

  await new Promise(r => setTimeout(r, 5500));

  const afterSignin = await cdp.eval<{signedIn: boolean; formVisible: boolean; hasSignOut: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var formVisible = !!main.querySelector('input[type="email"]');
    var btns = Array.from(main.querySelectorAll('button')).map(function(b){return b.textContent.trim();});
    var hasSignOut = btns.some(function(t){return t.toLowerCase().indexOf('sign out') >= 0;});
    return {signedIn: !formVisible, formVisible: formVisible, hasSignOut: hasSignOut, text: text.slice(0, 200)};
  })()`);
  assert(afterSignin!.signedIn === true, `signed_in state reflected (Bug #1): ${afterSignin!.text.slice(0, 80)}`);
  assert(afterSignin!.formVisible === false, "Sign-in form is NOT visible after login");
  assert(afterSignin!.hasSignOut === true, "Sign out button present");
  assert(/qa@maho\.local|QA/i.test(afterSignin!.text), "User identity rendered (email or display_name)");

  step("Step 2 — Billing data (Bug #5, #6, #7)");
  await cdp.navigate(`${CDP_URL_ROOT}billing`, 5500);
  const billing = await cdp.eval<{gated: boolean; hasProgress: boolean; hasTier: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var gated = /Sign in to manage/i.test(text);
    var hasProgress = !!main.querySelector('[role="progressbar"]') || !!main.querySelector('[aria-valuenow]');
    var hasTier = /free|pro|max/i.test(text);
    return {gated: gated, hasProgress: hasProgress, hasTier: hasTier, text: text.slice(0, 300)};
  })()`);
  assert(billing!.gated === false, "Billing pane NOT gated (Sign-in state persists cross-navigation)");
  assert(billing!.hasTier === true, `Tier text visible in billing pane (Bug #6): ${billing!.text.slice(0, 100)}`);
  assert(billing!.hasProgress === true, "Progress bar rendered (Bug #15 shadcn Progress)");
  assert(/No payment method|Payment method|Manage/i.test(billing!.text), "Payment method section rendered (Bug #5)");

  step("Step 3 — Invoices + Portal (Bug #8, #9)");
  const invoicesPortal = await cdp.eval<{hasInvoicesSection: boolean; portalBtnLabels: string[]; noStripeCom: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasInvoicesSection = /Invoices|No invoices|Receipts/i.test(text);
    var btns = Array.from(main.querySelectorAll('button')).map(function(b){return b.textContent.trim();});
    var portalBtnLabels = btns.filter(function(l){return /Manage|Adjust plan/i.test(l);});
    // Note: cannot inspect onclick handler from DOM; we validate at the network layer separately.
    return {hasInvoicesSection: hasInvoicesSection, portalBtnLabels: portalBtnLabels, noStripeCom: true, text: text.slice(0, 400)};
  })()`);
  assert(invoicesPortal!.hasInvoicesSection === true, "Invoices section rendered (Bug #8)");
  assert(invoicesPortal!.portalBtnLabels.length >= 1, `At least one portal button (Bug #9): ${invoicesPortal!.portalBtnLabels.join(", ")}`);

  step("Step 4 — Sidebar width (Bug #10)");
  await cdp.navigate(`${CDP_URL_ROOT}appearance`, 3500);
  const sliderBefore = await cdp.eval<{val: string; min: string; max: string}>(`(function(){
    var s = document.querySelector('main input[type="range"]');
    if (!s) return null;
    return {val: s.value, min: s.min, max: s.max};
  })()`);
  assert(sliderBefore != null, `Sidebar slider present (min=${sliderBefore?.min} max=${sliderBefore?.max} val=${sliderBefore?.val})`);

  const targetWidth = 350;
  await cdp.eval(`(function(){
    var s = document.querySelector('main input[type="range"]');
    if (!s) return;
    var setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
    setter.call(s, '${targetWidth}');
    s.dispatchEvent(new Event('input', {bubbles: true}));
    s.dispatchEvent(new Event('change', {bubbles: true}));
  })()`);
  await new Promise(r => setTimeout(r, 2000));

  const sliderAfter = await cdp.eval<{val: string}>(`(function(){
    var s = document.querySelector('main input[type="range"]');
    return {val: s.value};
  })()`);
  assert(sliderAfter!.val === String(targetWidth), `Slider set to ${targetWidth}`);

  const viewport = await cdp.eval<{innerW: number; outerW: number; sidebarWidthPx: number}>(`(function(){
    return {innerW: window.innerWidth, outerW: window.outerWidth, sidebarWidthPx: window.outerWidth - window.innerWidth};
  })()`);
  const actualSidebar = viewport!.sidebarWidthPx;
  assert(
    actualSidebar >= targetWidth - 20 && actualSidebar <= targetWidth + 30,
    `Actual sidebar width ≈ ${targetWidth} (measured chrome band = ${actualSidebar}px, Bug #10 fix verified)`,
  );

  step("Step 5 — AccountStatus persistence (Bug #1 regression)");
  await cdp.navigate("chrome://newtab", 2000);
  await cdp.navigate(`${CDP_URL_ROOT}account`, 4500);
  const persist = await cdp.eval<{signedIn: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var formVisible = !!main.querySelector('input[type="email"]');
    return {signedIn: !formVisible, text: text.slice(0, 200)};
  })()`);
  assert(persist!.signedIn === true, `Signed-in persists after chrome://newtab round-trip (Bug #1): ${persist!.text.slice(0, 80)}`);

  step("Step 6 — Sign out");
  await cdp.eval(`(function(){
    var btns = document.querySelectorAll('main button');
    for (var b of btns) if (/sign out/i.test(b.textContent)) { b.click(); return; }
  })()`);
  await new Promise(r => setTimeout(r, 2500));
  const afterSignout = await cdp.eval<{formVisible: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    return {formVisible: !!main.querySelector('input[type="email"]'), text: text.slice(0, 200)};
  })()`);
  assert(afterSignout!.formVisible === true, `Sign-in form visible after Sign out: ${afterSignout!.text.slice(0, 80)}`);

  step("Step 7 — Theme store UX (Bug #12)");
  await cdp.navigate(`${CDP_URL_ROOT}theme-store`, 4500);
  const themeStore = await cdp.eval<{comingSoon: boolean; hasRetry: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var comingSoon = /coming soon/i.test(text);
    var btns = Array.from(main.querySelectorAll('button')).map(function(b){return b.textContent.trim();});
    var hasRetry = btns.some(function(t){return t.toLowerCase() === 'retry';});
    return {comingSoon: comingSoon, hasRetry: hasRetry, text: text.slice(0, 300)};
  })()`);
  assert(themeStore!.comingSoon === true, `Theme store 'coming soon' text visible (Bug #12): ${themeStore!.text.slice(0, 120)}`);
  assert(themeStore!.hasRetry === false, "Retry button hidden on theme store failure (Bug #12)");

  ws.close();

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

await main();
