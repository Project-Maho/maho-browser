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

  step("Login Error Path Verification");
  await cdp.navigate(`${CDP_URL_ROOT}account`, 4000);

  // Sign out if already signed in
  await cdp.eval(`(function(){
    var btns = Array.from(document.querySelectorAll('main button'));
    var signoutBtn = btns.find(b => /sign out/i.test(b.textContent));
    if (signoutBtn) signoutBtn.click();
  })()`);
  await new Promise(r => setTimeout(r, 2000));

  // Enter invalid credentials
  const filled = await cdp.eval<boolean>(`(function(){
    var email = document.querySelector('main input[type="email"]');
    var pw = document.querySelector('main input[type="password"]');
    if (!email || !pw) return false;
    var setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
    setter.call(email, 'wrong_user@maho.local');
    email.dispatchEvent(new Event('input', {bubbles: true}));
    setter.call(pw, 'badpassword123456');
    pw.dispatchEvent(new Event('input', {bubbles: true}));
    
    var btns = Array.from(document.querySelectorAll('main button'));
    var signinBtn = btns.find(b => b.textContent.trim() === 'Sign in');
    if (!signinBtn) return false;
    signinBtn.click();
    return true;
  })()`);

  assert(filled === true, "Invalid credentials filled and sign-in submitted");
  await new Promise(r => setTimeout(r, 4500));

  const afterFailedLogin = await cdp.eval<{formVisible: boolean; hasError: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var formVisible = !!main.querySelector('input[type="email"]');
    // Look for error alerts or messages
    var hasError = /invalid|error|failed|could not|incorrect/i.test(text);
    return {
      formVisible: formVisible,
      hasError: hasError,
      text: text.slice(0, 300)
    };
  })()`);

  assert(afterFailedLogin!.formVisible === true, "Sign-in form remains visible after failed login");
  assert(afterFailedLogin!.hasError === true, `Error message/alert displayed: ${afterFailedLogin!.text.slice(0, 150)}`);

  ws.close();
  reportResultsAndExit();
}

await main();
