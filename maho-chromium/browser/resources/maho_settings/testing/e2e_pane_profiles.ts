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

  step("Profiles Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}profiles`, 4000);

  const initial = await cdp.eval<{hasTitle: boolean; hasAddProfileInput: boolean; profileNames: string[]}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /Browser profiles/i.test(text);
    var input = main.querySelector('input[placeholder="New profile name"]');
    var profiles = Array.from(main.querySelectorAll('h4')).map(h => h.textContent.trim());
    return {
      hasTitle: hasTitle,
      hasAddProfileInput: !!input,
      profileNames: profiles
    };
  })()`);

  assert(initial!.hasTitle === true, "Profiles pane title 'Browser profiles' rendered");
  assert(initial!.hasAddProfileInput === true, "New profile input textfield present");
  assert(initial!.profileNames.length >= 1, "At least one browser profile is configured");

  // Create new profile
  const testProfileName = "QA Test Profile";
  const created = await cdp.eval<boolean>(`(function(){
    var input = document.querySelector('main input[placeholder="New profile name"]');
    if (!input) return false;
    var setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
    setter.call(input, '${testProfileName}');
    input.dispatchEvent(new Event('input', {bubbles: true}));
    
    var btns = Array.from(document.querySelectorAll('main button'));
    var createBtn = btns.find(b => b.textContent.trim() === 'Create');
    if (!createBtn) return false;
    createBtn.click();
    return true;
  })()`);

  assert(created === true, "Submitted new profile form");
  await new Promise(r => setTimeout(r, 4500));

  const afterCreate = await cdp.eval<{profileNames: string[]}>(`(function(){
    var main = document.querySelector('main');
    var profiles = Array.from(main.querySelectorAll('h4')).map(h => h.textContent.trim());
    return { profileNames: profiles };
  })()`);

  assert(afterCreate!.profileNames.includes(testProfileName), `New profile '${testProfileName}' successfully added to UI`);

  // Delete the profile
  const deleted = await cdp.eval<boolean>(`(function(){
    var rows = Array.from(document.querySelectorAll('main section'));
    for (var row of rows) {
      var h4 = row.querySelector('h4');
      if (h4 && h4.textContent.trim() === '${testProfileName}') {
        var delBtn = row.querySelector('button[aria-label="Delete profile"]');
        if (delBtn) {
          delBtn.click();
          return true;
        }
      }
    }
    return false;
  })()`);

  assert(deleted === true, "Clicked delete profile button");
  await new Promise(r => setTimeout(r, 3500));

  const afterDelete = await cdp.eval<{profileNames: string[]}>(`(function(){
    var main = document.querySelector('main');
    var profiles = Array.from(main.querySelectorAll('h4')).map(h => h.textContent.trim());
    return { profileNames: profiles };
  })()`);

  assert(!afterDelete!.profileNames.includes(testProfileName), `New profile '${testProfileName}' successfully removed from UI`);

  ws.close();
  reportResultsAndExit();
}

await main();
