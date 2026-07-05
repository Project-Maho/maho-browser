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

async function main() {
  await checkRelayOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise((r) => setTimeout(r, 500));

  step("Autofill Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}autofill`, 4000);

  const inputs = await cdp.eval<{
    labels: string[];
    total: number;
    hasAddressFields: boolean;
    hasPaymentFields: boolean;
  }>(`(function(){
    var main = document.querySelector('main');
    var inputs = Array.from(main.querySelectorAll('input'));
    var labels = inputs.map(function(i){return i.getAttribute('aria-label') || i.placeholder || i.name || '';}).filter(Boolean);
    var joined = labels.join(' | ');
    var hasAddressFields = /Address full name/i.test(joined) && /Address line 1/i.test(joined) && /Address city/i.test(joined);
    var hasPaymentFields = /Payment card network/i.test(joined) && /Payment card last four/i.test(joined) && /Payment cardholder name/i.test(joined);
    return {labels: labels.slice(0, 15), total: inputs.length, hasAddressFields: hasAddressFields, hasPaymentFields: hasPaymentFields};
  })()`);

  assert(inputs!.total >= 9, `Autofill inputs count ≥ 9 (found ${inputs!.total})`);
  assert(inputs!.hasAddressFields === true, `Address form has full name / line 1 / city fields: ${JSON.stringify(inputs!.labels.slice(0, 6))}`);
  assert(inputs!.hasPaymentFields === true, `Payment form has network / last four / cardholder fields: ${JSON.stringify(inputs!.labels.slice(-4))}`);

  const btns = await cdp.eval<{addAddressBtn: boolean; addPaymentBtn: boolean}>(`(function(){
    var main = document.querySelector('main');
    var btnTexts = Array.from(main.querySelectorAll('button')).map(function(b){return b.textContent.trim();});
    var addAddressBtn = btnTexts.some(function(t){return /Add address/i.test(t);});
    var addPaymentBtn = btnTexts.some(function(t){return /Add payment/i.test(t);});
    return {addAddressBtn: addAddressBtn, addPaymentBtn: addPaymentBtn};
  })()`);
  assert(btns!.addAddressBtn === true, "'Add address' button present");
  assert(btns!.addPaymentBtn === true, "'Add payment' button present");

  ws.close();
  reportResultsAndExit();
}

await main();
