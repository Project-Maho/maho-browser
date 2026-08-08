// C1 runtime check: with the "maho" API permission registered, chrome.maho.*
// must be exposed to this privileged extension context WITHOUT a CHECK crash.
// Load unpacked, then inspect the service worker console (chrome://extensions
// -> this extension -> "service worker").

console.log('[maho-api-test] chrome.maho present:', !!chrome.maho);

if (chrome.maho) {
  console.log(
      '[maho-api-test] splitView:', !!chrome.maho.splitView,
      'sidePanel:', !!chrome.maho.sidePanel);

  if (chrome.maho.splitView) {
    chrome.maho.splitView.query((info) => {
      console.log(
          '[maho-api-test] splitView.query ->', JSON.stringify(info),
          chrome.runtime.lastError ? chrome.runtime.lastError.message : 'ok');
    });
  }

  if (chrome.maho.sidePanel) {
    chrome.maho.sidePanel.setLayout('left', () => {
      console.log(
          '[maho-api-test] sidePanel.setLayout ->',
          chrome.runtime.lastError ? chrome.runtime.lastError.message : 'ok');
    });
  }
} else {
  console.error('[maho-api-test] chrome.maho is UNDEFINED — maho permission not exposed');
}
