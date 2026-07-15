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

// Native React <input> value setter so dispatched events fire the controlled
// component's onChange.
const SET_INPUT_HELPER = `
  function __setInput(el, val){
    var setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
    setter.call(el, val);
    el.dispatchEvent(new Event('input', {bubbles: true}));
    el.dispatchEvent(new Event('change', {bubbles: true}));
  }
`;

// A known native entry we create then remove. The password value is only ever
// used to prove reveal works via derived booleans — it is NEVER read back into
// the test process or logged.
const TEST_DOMAIN = "e2e-reveal.example";
const TEST_USERNAME = "e2e-user";
const TEST_SECRET = "e2e-secret-value-do-not-log";

async function main() {
  await checkRelayOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise(r => setTimeout(r, 500));

  step("Passwords Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}passwords`, 4000);

  const base = await cdp.eval<{hasTitle: boolean; hasProviderText: boolean; providerCombobox: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    return {
      hasTitle: /Saved passwords/i.test(text),
      hasProviderText: /Passwords provider/i.test(text),
      providerCombobox: !!main.querySelector('button[role="combobox"][aria-label="Password provider"]')
    };
  })()`);
  assert(base!.hasTitle === true, "Passwords pane title 'Saved passwords' rendered");
  assert(base!.hasProviderText === true, "Password provider description is visible");
  assert(base!.providerCombobox === true, "Password provider selector combobox present");

  step("Provider selector lists Maho Native + Bitwarden + 1Password");
  await cdp.eval(`(function(){
    var trigger = document.querySelector('main button[role="combobox"][aria-label="Password provider"]');
    if (trigger) trigger.click();
  })()`);
  await new Promise(r => setTimeout(r, 700));

  const providerOptions = await cdp.eval<Array<{label: string; disabled: boolean}>>(`(function(){
    var options = Array.from(document.querySelectorAll('[role="option"]'));
    return options.map(function(o){
      return {
        label: (o.textContent || '').trim(),
        disabled: o.getAttribute('aria-disabled') === 'true' || o.hasAttribute('data-disabled')
      };
    });
  })()`);

  // Close the dropdown without selecting anything (Escape).
  await cdp.send("Input.dispatchKeyEvent", {type: "keyDown", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
  await cdp.send("Input.dispatchKeyEvent", {type: "keyUp", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
  await new Promise(r => setTimeout(r, 400));

  const opts = providerOptions ?? [];
  const maho = opts.find(o => /maho/i.test(o.label));
  const bitwarden = opts.find(o => /bitwarden/i.test(o.label));
  const onePassword = opts.find(o => /1 ?password/i.test(o.label));

  assert(!!maho, `Maho Native provider option present: ${JSON.stringify(opts.map(o => o.label))}`);
  assert(!!bitwarden, "Bitwarden provider option present");
  assert(!!onePassword, "1Password provider option present");
  assert(maho ? maho.disabled === false : false, "Maho Native provider option is enabled (available)");
  assert(bitwarden ? bitwarden.disabled === true : false, "Unavailable Bitwarden provider option is disabled");
  assert(onePassword ? onePassword.disabled === true : false, "Unavailable 1Password provider option is disabled");

  // Ensure the native provider is active and passwords management is enabled so
  // the credentials list / reveal controls render.
  const providerAndEnable = await cdp.eval<{isNative: boolean; toggledOn: boolean; alreadyOn: boolean}>(`(function(){
    var trigger = document.querySelector('main button[role="combobox"][aria-label="Password provider"]');
    var isNative = !!trigger && /maho/i.test(trigger.textContent || '');
    var sw = document.querySelector('main button[role="switch"][aria-label="Passwords"]');
    var on = sw ? (sw.getAttribute('aria-checked') === 'true' || sw.getAttribute('data-state') === 'checked') : true;
    var toggledOn = false;
    if (sw && !on) { sw.click(); toggledOn = true; }
    return {isNative: isNative, toggledOn: toggledOn, alreadyOn: on};
  })()`);
  assert(providerAndEnable!.isNative === true, "Maho Native is the active password provider");
  await new Promise(r => setTimeout(r, 1200));

  step("Add a known native entry, then reveal it (happy)");
  await cdp.eval(`(function(){
    ${SET_INPUT_HELPER}
    var main = document.querySelector('main');
    function byLabel(l){ return Array.from(main.querySelectorAll('input')).find(function(i){ return (i.getAttribute('aria-label')||'') === l; }); }
    var d = byLabel('Password domain');
    var u = byLabel('Password username');
    var p = byLabel('Password value');
    if (d) __setInput(d, ${JSON.stringify(TEST_DOMAIN)});
    if (u) __setInput(u, ${JSON.stringify(TEST_USERNAME)});
    if (p) __setInput(p, ${JSON.stringify(TEST_SECRET)});
  })()`);
  await new Promise(r => setTimeout(r, 400));

  const addClicked = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    var btn = Array.from(main.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Add password'; });
    if (btn && !btn.disabled) { btn.click(); return true; }
    return false;
  })()`);
  assert(addClicked === true, "Clicked 'Add password' for the known native entry");
  await new Promise(r => setTimeout(r, 1500));

  // Isolate the entry via search so exactly one reveal target exists.
  await cdp.eval(`(function(){
    ${SET_INPUT_HELPER}
    var el = document.querySelector('main input[aria-label="Search passwords"]');
    if (el) __setInput(el, ${JSON.stringify(TEST_DOMAIN)});
  })()`);
  await new Promise(r => setTimeout(r, 900));

  const revealClicked = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    var btn = Array.from(main.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Reveal'; });
    if (btn) { btn.click(); return true; }
    return false;
  })()`);
  assert(revealClicked === true, "Clicked 'Reveal' on the known native entry");

  // Poll until reveal settles; return ONLY derived booleans, never the secret.
  let revealState = {revealed: false, masked: true, revealing: false, failed: false};
  for (let i = 0; i < 10; i++) {
    await new Promise(r => setTimeout(r, 400));
    revealState = await cdp.eval<{revealed: boolean; masked: boolean; revealing: boolean; failed: boolean}>(`(function(){
      var main = document.querySelector('main');
      var ps = Array.from(main.querySelectorAll('p')).filter(function(p){ return /^Password:/.test((p.textContent||'').trim()); });
      var txt = ps.length ? (ps[0].textContent || '').trim() : '';
      var masked = /\\u2022/.test(txt);
      var revealing = /\\(revealing/i.test(txt);
      var failed = /Couldn't reveal/i.test(txt);
      var revealed = /^Password: /.test(txt) && !masked && !revealing && !failed && txt.length > 'Password: '.length;
      return {revealed: revealed, masked: masked, revealing: revealing, failed: failed};
    })()`) ?? revealState;
    if (revealState.revealed || revealState.failed) break;
  }
  assert(revealState.revealed === true, "Reveal shows a non-empty secret for the known native entry (value not printed)");

  step("Hide the revealed secret again");
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var btn = Array.from(main.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Hide'; });
    if (btn) btn.click();
  })()`);
  await new Promise(r => setTimeout(r, 700));
  const afterHide = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    var ps = Array.from(main.querySelectorAll('p')).filter(function(p){ return /^Password:/.test((p.textContent||'').trim()); });
    var txt = ps.length ? (ps[0].textContent || '').trim() : '';
    return /\\u2022/.test(txt);
  })()`);
  assert(afterHide === true, "Secret is masked again after clicking Hide");

  step("No matching native entry shows no secret (failure/boundary)");
  await cdp.eval(`(function(){
    ${SET_INPUT_HELPER}
    var el = document.querySelector('main input[aria-label="Search passwords"]');
    if (el) __setInput(el, 'zzz-no-such-entry-zzz');
  })()`);
  await new Promise(r => setTimeout(r, 900));
  const noMatch = await cdp.eval<{hasRevealBtn: boolean; anyRevealedSecret: boolean}>(`(function(){
    var main = document.querySelector('main');
    var hasRevealBtn = Array.from(main.querySelectorAll('button')).some(function(b){ return b.textContent.trim() === 'Reveal' || b.textContent.trim() === 'Hide'; });
    var ps = Array.from(main.querySelectorAll('p')).filter(function(p){ return /^Password:/.test((p.textContent||'').trim()); });
    var anyRevealedSecret = ps.some(function(p){
      var txt = (p.textContent || '').trim();
      return /^Password: /.test(txt) && !/\\u2022/.test(txt) && !/\\(revealing/i.test(txt) && !/Couldn't reveal/i.test(txt) && txt.length > 'Password: '.length;
    });
    return {hasRevealBtn: hasRevealBtn, anyRevealedSecret: anyRevealedSecret};
  })()`);
  assert(noMatch!.hasRevealBtn === false, "No reveal control renders when no entry matches the search");
  assert(noMatch!.anyRevealedSecret === false, "No plaintext secret is shown when no entry matches");

  step("Cleanup — clear search and delete the test entry");
  await cdp.eval(`(function(){
    ${SET_INPUT_HELPER}
    var el = document.querySelector('main input[aria-label="Search passwords"]');
    if (el) __setInput(el, ${JSON.stringify(TEST_DOMAIN)});
  })()`);
  await new Promise(r => setTimeout(r, 900));
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var sections = Array.from(main.querySelectorAll('section'));
    var row = sections.find(function(s){ return (s.textContent||'').indexOf(${JSON.stringify(TEST_DOMAIN)}) !== -1; });
    if (row) {
      var del = Array.from(row.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Delete'; });
      if (del) del.click();
    }
  })()`);
  await new Promise(r => setTimeout(r, 1000));

  ws.close();
  reportResultsAndExit();
}

await main();
