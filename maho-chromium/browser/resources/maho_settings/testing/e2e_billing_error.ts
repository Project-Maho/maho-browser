#!/usr/bin/env bun
import {
  CDP,
  assert,
  step,
  getSettingsWs,
  checkRelayOrExit,
  reportResultsAndExit,
  CDP_URL_ROOT
} from "./cdp_harness";

async function main() {
  await checkRelayOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise(r => setTimeout(r, 500));

  step("Billing Error Retry Path Verification");

  // Navigate to account and sign out if signed in, or make sure we are signed in
  await cdp.navigate(`${CDP_URL_ROOT}account`, 3500);
  const signedIn = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    return text.toLowerCase().indexOf('sign out') >= 0;
  })()`);

  if (!signedIn) {
    // Fill sign in form
    await cdp.eval(`(function(){
      var email = document.querySelector('main input[type="email"]');
      var pw = document.querySelector('main input[type="password"]');
      if (!email || !pw) return;
      var setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
      setter.call(email, 'qa@maho.local');
      email.dispatchEvent(new Event('input', {bubbles: true}));
      setter.call(pw, 'test12345678');
      pw.dispatchEvent(new Event('input', {bubbles: true}));
      var btns = document.querySelectorAll('main button');
      for (var b of btns) if (b.textContent.trim() === 'Sign in') { b.click(); break; }
    })()`);
    await new Promise(r => setTimeout(r, 5000));
  }

  // Navigate to Billing pane
  await cdp.navigate(`${CDP_URL_ROOT}billing`, 4000);

  // Override GetBillingInfo on the store's handler to simulate a billing error
  await cdp.eval(`(function(){
    if (window.store && window.store.pageHandler) {
      window.store.pageHandler.getBillingInfo = async function() {
        return { info: null, error: "Mocked Stripe connectivity failure" };
      };
    }
  })()`);

  // Navigate to general and back to billing to trigger refetch
  await cdp.navigate(`${CDP_URL_ROOT}general`, 1500);
  await cdp.navigate(`${CDP_URL_ROOT}billing`, 3000);

  // Check if billing error and retry button are displayed
  const errorInfo = await cdp.eval<{hasErrorText: boolean; hasRetryBtn: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var buttons = Array.from(main.querySelectorAll('button'));
    var retryBtn = buttons.find(b => b.textContent.trim() === 'Retry');
    return {
      hasErrorText: /Mocked Stripe connectivity failure/i.test(text),
      hasRetryBtn: !!retryBtn
    };
  })()`);

  assert(errorInfo!.hasErrorText === true, "Billing error message is rendered in the pane");
  assert(errorInfo!.hasRetryBtn === true, "Retry button is visible in the pane");

  ws.close();
  reportResultsAndExit();
}

await main();
