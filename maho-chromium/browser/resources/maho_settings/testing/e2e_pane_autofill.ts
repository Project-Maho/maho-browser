#!/usr/bin/env bun
import {
  CDP,
  assert,
  step,
  getSettingsWs,
  checkRelayOrExit,
  reportResultsAndExit,
  CDP_URL_ROOT,
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

const ADDR_NAME = "E2E Autofill Tester";
const ADDR_LINE1 = "100 E2E Street";
const ADDR_LINE2 = "Apt 4B";
const ADDR_CITY = "Testville";
const CARD_NETWORK = "visa";
const CARD_LAST_FOUR = "4242";
const CARD_HOLDER = "E2E Autofill Tester";

async function readCounts(cdp: CDP) {
  return await cdp.eval<{addresses: number; payments: number}>(`(function(){
    var main = document.querySelector('main');
    var btns = Array.from(main.querySelectorAll('button'));
    var addresses = btns.filter(function(b){ return /^Delete address/.test(b.getAttribute('aria-label')||''); }).length;
    var payments = btns.filter(function(b){ return /^Delete payment method/.test(b.getAttribute('aria-label')||''); }).length;
    return {addresses: addresses, payments: payments};
  })()`);
}

async function main() {
  await checkRelayOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise((r) => setTimeout(r, 500));

  step("Autofill Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}autofill`, 4000);

  const inputs = await cdp.eval<{hasAddressFields: boolean; hasPaymentFields: boolean; addAddressBtn: boolean; addPaymentBtn: boolean}>(`(function(){
    var main = document.querySelector('main');
    var labels = Array.from(main.querySelectorAll('input')).map(function(i){return i.getAttribute('aria-label') || i.placeholder || i.name || '';}).filter(Boolean);
    var joined = labels.join(' | ');
    var hasAddressFields = /Address full name/i.test(joined) && /Address line 1/i.test(joined) && /Address line 2/i.test(joined) && /Address city/i.test(joined);
    var hasPaymentFields = /Payment card network/i.test(joined) && /Payment card last four/i.test(joined) && /Payment cardholder name/i.test(joined);
    var btnTexts = Array.from(main.querySelectorAll('button')).map(function(b){return b.textContent.trim();});
    return {hasAddressFields: hasAddressFields, hasPaymentFields: hasPaymentFields, addAddressBtn: btnTexts.indexOf('Add address') !== -1, addPaymentBtn: btnTexts.indexOf('Add payment') !== -1};
  })()`);
  assert(inputs!.hasAddressFields === true, "Address form has full name / line 1 / line 2 / city fields");
  assert(inputs!.hasPaymentFields === true, "Payment form has network / last four / cardholder fields");
  assert(inputs!.addAddressBtn === true, "'Add address' button present");
  assert(inputs!.addPaymentBtn === true, "'Add payment' button present");

  step("Empty required fields are handled (failure/boundary)");
  const before = await readCounts(cdp);
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var btn = Array.from(main.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Add address'; });
    if (btn) btn.click();
  })()`);
  await new Promise((r) => setTimeout(r, 1000));
  const afterEmpty = await readCounts(cdp);
  assert(afterEmpty!.addresses === before!.addresses, `Submitting an empty address form adds nothing (still ${before!.addresses})`);

  step("Add address with line 2 and payment with network (happy)");
  await cdp.eval(`(function(){
    ${SET_INPUT_HELPER}
    var main = document.querySelector('main');
    function byLabel(l){ return Array.from(main.querySelectorAll('input')).find(function(i){ return (i.getAttribute('aria-label')||'') === l; }); }
    var name = byLabel('Address full name');
    var line1 = byLabel('Address line 1');
    var line2 = byLabel('Address line 2');
    var city = byLabel('Address city');
    if (name) __setInput(name, ${JSON.stringify(ADDR_NAME)});
    if (line1) __setInput(line1, ${JSON.stringify(ADDR_LINE1)});
    if (line2) __setInput(line2, ${JSON.stringify(ADDR_LINE2)});
    if (city) __setInput(city, ${JSON.stringify(ADDR_CITY)});
  })()`);
  await new Promise((r) => setTimeout(r, 400));
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var btn = Array.from(main.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Add address'; });
    if (btn) btn.click();
  })()`);
  await new Promise((r) => setTimeout(r, 1200));

  await cdp.eval(`(function(){
    ${SET_INPUT_HELPER}
    var main = document.querySelector('main');
    function byLabel(l){ return Array.from(main.querySelectorAll('input')).find(function(i){ return (i.getAttribute('aria-label')||'') === l; }); }
    var net = byLabel('Payment card network');
    var last = byLabel('Payment card last four digits');
    var holder = byLabel('Payment cardholder name');
    if (net) __setInput(net, ${JSON.stringify(CARD_NETWORK)});
    if (last) __setInput(last, ${JSON.stringify(CARD_LAST_FOUR)});
    if (holder) __setInput(holder, ${JSON.stringify(CARD_HOLDER)});
  })()`);
  await new Promise((r) => setTimeout(r, 400));
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var btn = Array.from(main.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Add payment'; });
    if (btn) btn.click();
  })()`);
  await new Promise((r) => setTimeout(r, 1500));

  step("Reload and assert both persist");
  await cdp.send("Page.reload");
  await new Promise((r) => setTimeout(r, 4000));

  const persisted = await cdp.eval<{addressPersisted: boolean; paymentPersisted: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '');
    var addressPersisted = text.indexOf(${JSON.stringify(ADDR_LINE2)}) !== -1;
    var paymentPersisted = /visa/i.test(text) && text.indexOf(${JSON.stringify(CARD_LAST_FOUR)}) !== -1;
    return {addressPersisted: addressPersisted, paymentPersisted: paymentPersisted};
  })()`);
  assert(persisted!.addressPersisted === true, "Saved address with 'Apt 4B' line 2 persists after reload");
  assert(persisted!.paymentPersisted === true, "Saved payment with 'visa' network + last four persists after reload");

  step("Cleanup — delete the added address and payment");
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var sections = Array.from(main.querySelectorAll('section'));
    var addrRow = sections.find(function(s){ return (s.textContent||'').indexOf(${JSON.stringify(ADDR_LINE2)}) !== -1; });
    if (addrRow) {
      var del = Array.from(addrRow.querySelectorAll('button')).find(function(b){ return /^Delete address/.test(b.getAttribute('aria-label')||''); });
      if (del) del.click();
    }
  })()`);
  await new Promise((r) => setTimeout(r, 1000));
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var sections = Array.from(main.querySelectorAll('section'));
    var payRow = sections.find(function(s){ return (s.textContent||'').indexOf(${JSON.stringify(CARD_LAST_FOUR)}) !== -1 && /visa/i.test(s.textContent||''); });
    if (payRow) {
      var del = Array.from(payRow.querySelectorAll('button')).find(function(b){ return /^Delete payment method/.test(b.getAttribute('aria-label')||''); });
      if (del) del.click();
    }
  })()`);
  await new Promise((r) => setTimeout(r, 1000));

  ws.close();
  reportResultsAndExit();
}

await main();
