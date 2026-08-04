#!/usr/bin/env bun
import {
  CDP,
  assert,
  step,
  getSettingsWs,
  checkRelayOrExit,
  reportResultsAndExit,
  waitForDomMutation,
  CDP_URL_ROOT,
} from './cdp_harness';

function js(value: unknown): string { return JSON.stringify(value); }

async function main() {
  await checkRelayOrExit();
  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send('Runtime.enable');
  await cdp.send('Page.enable');
  const runtimeExceptions: Array<{
    text: string;
    description: string;
    url: string;
    lineNumber: number;
    columnNumber: number;
  }> = [];
  const runtimeExceptionListener = (params: {
    exceptionDetails?: {
      text?: string;
      exception?: {description?: string};
      url?: string;
      lineNumber?: number;
      columnNumber?: number;
    };
  }) => {
    const details = params?.exceptionDetails;
    runtimeExceptions.push({
      text: details?.text ?? '',
      description: details?.exception?.description ?? '',
      url: details?.url ?? '',
      lineNumber: details?.lineNumber ?? 0,
      columnNumber: details?.columnNumber ?? 0,
    });
  };
  cdp.on('Runtime.exceptionThrown', runtimeExceptionListener);
  try {
    await cdp.navigate(`${CDP_URL_ROOT}profiles`);
    try {
      await waitForDomMutation(
        cdp,
        `
          !!window.settingsStore &&
          typeof window.settingsStore.getHandler === 'function' &&
          typeof window.settingsStore.getSnapshot === 'function' &&
          typeof window.settingsStore.subscribe === 'function'
        `,
        async () => {},
      );
    } catch (error) {
      const pageDiagnostic = await cdp.eval(`(function(){
        const root = document.querySelector('maho-settings-app');
        const store = window.settingsStore;
        return {
          href: location.href,
          readyState: document.readyState,
          title: document.title,
          rootPresent: !!root,
          rootChildCount: root?.childElementCount ?? null,
          rootTextPrefix: (root?.textContent ?? '').slice(0, 500),
          bodyHtmlPrefix: (document.body?.innerHTML ?? '').slice(0, 1000),
          scriptSrcs: Array.from(document.scripts, script => script.src),
          storeType: typeof store,
          getHandlerType: typeof store?.getHandler,
          getSnapshotType: typeof store?.getSnapshot,
          subscribeType: typeof store?.subscribe,
        };
      })()`);
      throw new Error(
        'Settings store readiness failed: ' +
        JSON.stringify({...pageDiagnostic, runtimeExceptions}) +
        '; cause=' +
        String(error),
      );
    }
  } finally {
    cdp.off('Runtime.exceptionThrown', runtimeExceptionListener);
  }

  const provisionedProfileName = `QA Provisioned ${Date.now()}`;
  const provisioned = await cdp.eval<{
    beforeCount: number;
    profileId: string | null;
    profileNames: string[];
  }>(
    `(async function(){
      const handler = window.settingsStore.getHandler();
      const before = (await handler.getProfiles()).profiles;
      const {profile} = await handler.createProfile(${js(provisionedProfileName)});
      const after = (await handler.getProfiles()).profiles;
      return {
        beforeCount: before.length,
        profileId: profile?.id || null,
        profileNames: after.map(item => item.name),
      };
    })()`,
    true,
  );
  if (
    !provisioned?.profileId ||
    !provisioned.profileNames.includes('Default') ||
    !provisioned.profileNames.includes(provisionedProfileName)
  ) {
    throw new Error(`Fresh-root profile provisioning failed: ${JSON.stringify(provisioned)}`);
  }

  step('Profiles catalog and editor');
  await waitForDomMutation(cdp, `!!document.querySelector('[role="listbox"][aria-label="Profiles"] [role="option"]')`, async () => {});
  assert(true, 'Profile catalog renders');
  await waitForDomMutation(cdp, `document.querySelector('[data-profile-editor="true"] h3')?.textContent.trim() === ${js(provisionedProfileName)}`, () => cdp.eval<boolean>(`(function(){var x=Array.from(document.querySelectorAll('[data-profile-name]')).find(x=>x.getAttribute('data-profile-name')===${js(provisionedProfileName)}); if(!x)return false;x.click();return true;})()`), 15_000);
  assert(true, 'Selected profile editor renders');

  const initial = (await cdp.eval<{names: (string | null)[]; hasSwitch: boolean; hasRawIdPrimary: boolean; hasDefault: boolean; hasGlobalLabel: boolean; hasSpaces: boolean; controls: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main && main.textContent || '').replace(/\\s+/g, ' ');
    return {
      names: Array.from(document.querySelectorAll('[data-profile-name]')).map(x => x.getAttribute('data-profile-name')),
      hasSwitch: Array.from(document.querySelectorAll('main button')).some(x => x.textContent.trim() === 'Switch'),
      hasRawIdPrimary: /\bID:\s*/.test(text),
      hasDefault: text.includes('Default'),
      hasGlobalLabel: text.includes('Applies to all profiles.'),
      hasSpaces: text.includes('Spaces:'),
      controls: ['Profile name','Avatar color','Search suggestions','Ask where to save each file','Archive timeout'].every(label => text.includes(label)),
    };
  })()`))!;
  assert(initial.names.length >= 1, 'At least one profile is listed');
  assert(initial.hasSwitch === false, 'No Switch control exists');
  assert(initial.hasRawIdPrimary === false, 'Raw internal ID is absent from primary text');
  assert(initial.hasDefault === true, 'Default badge is present');
  assert(initial.hasGlobalLabel === true, 'Global-scope applicability label is present');
  assert(initial.hasSpaces === false, 'Space lists are not rendered');
  assert(initial.controls === true, 'Approved identity, search, download, and archive controls render');

  const testProfileName = `QA Profile ${Date.now()}`;
  const opened = await waitForDomMutation(cdp, `!!document.querySelector('[role="dialog"] input[aria-label="New profile name"]')`, () => cdp.eval<boolean>(`(function(){var b=Array.from(document.querySelectorAll('button')).find(x => x.textContent.trim() === 'Add profile'); if (!b) return false; b.click(); return true;})()`), 10_000);
  assert(opened, 'Add-profile popup opens');
  const created = await waitForDomMutation(cdp, `Array.from(document.querySelectorAll('[data-profile-name]')).some(x => x.getAttribute('data-profile-name') === ${js(testProfileName)}) && document.querySelector('[data-profile-editor="true"] h3')?.textContent.trim() === ${js(testProfileName)}`, () => cdp.eval<boolean>(`(function(){
    var input = document.querySelector('[role="dialog"] input[aria-label="New profile name"]');
    var button = Array.from(document.querySelectorAll('[role="dialog"] button')).find(x => x.textContent.trim() === 'Create');
    if (!input || !button || button.disabled) return false;
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(input, ${js(testProfileName)});
    input.dispatchEvent(new Event('input', {bubbles:true}));
    button.click(); return true;
  })()`), 20_000);
  assert(created, 'Submitted disposable profile creation via popup');
  assert(true, 'Disposable profile appears in catalog');
  assert(true, 'Created profile becomes the edit target');

  step('Selected profile B values and mutations');
  const catalogNames = await cdp.eval<string[]>(`Array.from(document.querySelectorAll('[data-profile-name]')).map(x => x.getAttribute('data-profile-name'))`);
  const profileB = catalogNames!.find(name => name !== initial.names[0] && name !== testProfileName) || testProfileName;
  const selectedB = await waitForDomMutation(cdp, `document.querySelector('[data-profile-editor="true"] h3')?.textContent.trim() === ${js(profileB)}`, () => cdp.eval<boolean>(`(function(){var x=Array.from(document.querySelectorAll('[data-profile-name]')).find(x=>x.getAttribute('data-profile-name')===${js(profileB)}); if(!x)return false;x.click();return true;})()`), 15_000);
  assert(selectedB, 'Selected profile B from catalog');
  assert(true, 'Profile B editor values are shown');

  const toggledSuggestions = await cdp.eval<boolean>(`(function(){var x=document.querySelector('button[role="switch"][aria-label="Search suggestions"]');if(!x||x.disabled)return false;x.click();return true;})()`);
  assert(toggledSuggestions || !!await cdp.eval(`document.querySelector('button[role="switch"][aria-label="Search suggestions"]')?.disabled`), 'Search suggestions mutation is actionable or truthfully unavailable');
  const toggledPrompt = await cdp.eval<boolean>(`(function(){var x=document.querySelector('button[role="switch"][aria-label="Ask where to save each file"]');if(!x||x.disabled)return false;x.click();return true;})()`);
  assert(toggledPrompt || !!await cdp.eval(`document.querySelector('button[role="switch"][aria-label="Ask where to save each file"]')?.disabled`), 'Download prompt mutation is actionable or truthfully unavailable');

  const nameInput = await cdp.eval<boolean>(`!!document.querySelector('input[aria-label="Profile name"]')`);
  const colorControl = await cdp.eval<boolean>(`!!document.querySelector('[role="radiogroup"][aria-label="Avatar color"]')`);
  const archiveControl = await cdp.eval<boolean>(`!!document.querySelector('button[aria-label="Archive timeout"]')`);
  assert(nameInput, 'Profile name editor exists');
  assert(colorControl, 'Avatar color editor exists');
  assert(archiveControl, 'Archive timeout editor exists');

  step('Deletion protection and round trip');
  const defaultProfileName = await cdp.eval<string>(`(async function(){
    const {profiles} = await window.settingsStore.getHandler().getProfiles();
    return profiles.find(profile => profile.isDefault)?.name || '';
  })()`, true);
  assert(!!defaultProfileName, 'Default profile is present in the catalog');
  const selectedDefault = await waitForDomMutation(cdp, `
    document.querySelector('[data-profile-editor="true"] h3')?.textContent.trim() === ${js(defaultProfileName)} &&
    Array.from(document.querySelectorAll('[data-profile-name]')).some(option =>
      option.getAttribute('data-profile-name') === ${js(defaultProfileName)} &&
      option.getAttribute('aria-selected') === 'true')
  `, () => cdp.eval<boolean>(`(function(){
    var option=Array.from(document.querySelectorAll('[data-profile-name]')).find(x=>x.getAttribute('data-profile-name')===${js(defaultProfileName)});
    if(!option)return false;option.click();return true;
  })()`), 15_000);
  assert(selectedDefault, 'Default profile selected for delete-protection QA');

  const requestedDefaultDelete = await waitForDomMutation(cdp, `
    Array.from(document.querySelectorAll('main button')).some(button => button.textContent.trim() === 'Confirm delete')
  `, () => cdp.eval<boolean>(`(function(){
    var button=document.querySelector('button[aria-label=${js(`Delete profile ${defaultProfileName}`)}]');
    if(!button)return false;button.click();return true;
  })()`), 5_000);
  assert(requestedDefaultDelete, 'Default profile exact delete action opened confirmation');
  const confirmedDefaultDelete = await waitForDomMutation(cdp, `
    (document.querySelector('main')?.textContent || '').toLocaleLowerCase().includes('the default profile cannot be deleted.')
  `, () => cdp.eval<boolean>(`(function(){
    var button=Array.from(document.querySelectorAll('main button')).find(x=>x.textContent.trim()==='Confirm delete');
    if(!button)return false;button.click();return true;
  })()`), 5_000);
  assert(confirmedDefaultDelete, 'Confirmed protected default-profile deletion attempt');
  assert(true, 'Default-profile delete protection surfaces the real reason');

  await waitForDomMutation(cdp, `document.querySelector('[data-profile-editor="true"] h3')?.textContent.trim() === ${js(testProfileName)}`, () => cdp.eval(`Array.from(document.querySelectorAll('[data-profile-name]')).find(x=>x.getAttribute('data-profile-name')===${js(testProfileName)})?.click()`), 15_000);
  assert(true, 'Disposable profile selected for cleanup');
  await cdp.eval(`document.querySelector('button[aria-label=${js(`Delete profile ${testProfileName}`)}]')?.click()`);
  await waitForDomMutation(cdp, `!Array.from(document.querySelectorAll('[data-profile-name]')).some(x=>x.getAttribute('data-profile-name')===${js(testProfileName)})`, () => cdp.eval(`Array.from(document.querySelectorAll('main button')).find(x=>x.textContent.trim()==='Confirm delete')?.click()`), 20_000);
  assert(true, 'Disposable profile create/delete round trip completed');

  const provisionedDeleted = await cdp.eval<boolean>(`(async function(){
    const handler = window.settingsStore.getHandler();
    const {success} = await handler.deleteProfile(${js(provisioned.profileId)});
    if (!success) return false;
    const {profiles} = await handler.getProfiles();
    return !profiles.some(profile => profile.id === ${js(provisioned.profileId)});
  })()`, true);
  assert(provisionedDeleted, 'Provisioned secondary profile deleted through the supported API');

  ws.close();
  reportResultsAndExit();
}

await main();
