#!/usr/bin/env bun
import {
  CDP,
  CDP_URL_ROOT,
  assert,
  checkBrowserOrExit,
  getSettingsWs,
  reportResultsAndExit,
  step,
  waitForCondition,
} from "./cdp_harness";

type PaneSnapshot = {
  readonly activePane: string;
  readonly buttonLabels: readonly string[];
  readonly contentPane: string;
  readonly currentUrl: string;
  readonly hasCredentialManagementText: boolean;
  readonly hasSavedPasswordsManagementHeading: boolean;
  readonly isSettingsSurface: boolean;
  readonly mainText: string;
  readonly managementControlLabels: readonly string[];
  readonly navPaneKeys: readonly string[];
};

async function capturePaneSnapshot(cdp: CDP): Promise<PaneSnapshot | undefined> {
  return cdp.eval<PaneSnapshot>(`(function(){
    function normalizedText(element) {
      return ((element && element.textContent) || '').replace(/\\s+/g, ' ').trim();
    }

    var main = document.querySelector('main');
    var nav = document.querySelector('nav[aria-label="Settings panes"]');
    var active = document.querySelector('nav[aria-label="Settings panes"] [aria-current="page"]');
    var contentSection = main ? main.querySelector('[data-pane]') : null;
    var buttonLabels = Array.from(main ? main.querySelectorAll('button') : []).map(function(button){
      return normalizedText(button);
    }).filter(Boolean);
    var headings = Array.from(main ? main.querySelectorAll('h1,h2,h3,[role="heading"]') : []).map(function(heading){
      return normalizedText(heading);
    }).filter(Boolean);
    var mainText = normalizedText(main);

    return {
      activePane: active ? active.getAttribute('data-pane') || '' : '',
      buttonLabels: buttonLabels,
      contentPane: contentSection ? contentSection.getAttribute('data-pane') || '' : '',
      currentUrl: window.location.href,
      hasCredentialManagementText: /\\b(Credentials|Search saved passwords|Add password|Reveal|Hide|Copy|Fill|Edit|Delete|Password:)\\b/i.test(mainText),
      hasSavedPasswordsManagementHeading: headings.some(function(text){
        return /Saved Passwords/i.test(text) && /Manage|Saved Passwords/i.test(mainText);
      }),
      isSettingsSurface: !!main && !!nav,
      mainText: mainText.slice(0, 320),
      managementControlLabels: buttonLabels.filter(function(label){
        return /^(Add password|Edit|Delete|Reveal|Hide|Copy|Fill|Save Username)$/i.test(label);
      }),
      navPaneKeys: Array.from(document.querySelectorAll('nav[aria-label="Settings panes"] [data-pane]')).map(function(node){
        return node.getAttribute('data-pane') || '';
      }).filter(Boolean)
    };
  })()`);
}

function hasPaneKey(snapshot: PaneSnapshot | undefined, paneKey: string): boolean {
  return snapshot?.navPaneKeys.includes(paneKey) === true;
}

async function main(): Promise<void> {
  await checkBrowserOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await Bun.sleep(500);

  step("Given the existing Passwords settings route");
  await cdp.navigate(`${CDP_URL_ROOT}passwords`, 2500);
  const passwordsRouteReady = await waitForCondition(cdp, `(function(){
    var active = document.querySelector('nav[aria-label="Settings panes"] [aria-current="page"]');
    return active && active.getAttribute('data-pane') === 'passwords';
  })()`, 4000);
  const passwordsSnapshot = await capturePaneSnapshot(cdp);

  assert(passwordsRouteReady, "When chrome://maho-settings/passwords loads, the Passwords pane becomes active");
  assert(passwordsSnapshot?.isSettingsSurface === true, "Then passwords renders inside the Maho Settings surface");
  assert(passwordsSnapshot?.activePane === "passwords", "Then passwords remains the active settings pane");
  assert(
      passwordsSnapshot?.hasCredentialManagementText === false,
      `Then passwords contains no saved-credential management copy or landmark (excerpt: ${passwordsSnapshot?.mainText ?? "<missing>"})`,
  );
  assert(
      (passwordsSnapshot?.managementControlLabels.length ?? 0) === 0,
      `Then passwords contains no credential CRUD/secret-use buttons (found: ${(passwordsSnapshot?.managementControlLabels ?? []).join(", ") || "none"})`,
  );

  step("When the Saved Passwords management sibling route is opened");
  await cdp.navigate(`${CDP_URL_ROOT}saved-passwords`, 2500);
  const savedPasswordsRouteReady = await waitForCondition(cdp, `(function(){
    var active = document.querySelector('nav[aria-label="Settings panes"] [aria-current="page"]');
    var section = document.querySelector('main [data-pane="saved-passwords"]');
    return active && active.getAttribute('data-pane') === 'saved-passwords' && !!section;
  })()`, 3000);
  const savedPasswordsSnapshot = await capturePaneSnapshot(cdp);

  assert(savedPasswordsRouteReady, "Then chrome://maho-settings/saved-passwords activates its own pane");
  assert(
      savedPasswordsSnapshot?.currentUrl.includes("pane=saved-passwords") === true,
      `Then the URL preserves pane=saved-passwords (got: ${savedPasswordsSnapshot?.currentUrl ?? "<missing>"})`,
  );
  assert(savedPasswordsSnapshot?.activePane === "saved-passwords", "Then Saved Passwords is the active nav item");
  assert(savedPasswordsSnapshot?.contentPane === "saved-passwords", "Then Saved Passwords owns the rendered content landmark");
  assert(
      hasPaneKey(savedPasswordsSnapshot, "passwords") && hasPaneKey(savedPasswordsSnapshot, "saved-passwords"),
      `Then Identity navigation exposes Passwords and Saved Passwords as sibling panes (keys: ${(savedPasswordsSnapshot?.navPaneKeys ?? []).join(", ")})`,
  );
  assert(
      savedPasswordsSnapshot?.hasSavedPasswordsManagementHeading === true,
      `Then Saved Passwords renders a management heading/landmark (excerpt: ${savedPasswordsSnapshot?.mainText ?? "<missing>"})`,
  );

  ws.close();
  reportResultsAndExit();
}

await main();
