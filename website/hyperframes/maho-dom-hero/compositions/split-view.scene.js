// @ts-check

const [{ mountBrowserShell }, { mountSplitView }, { SPLIT_VIEW_PRESETS }] = await Promise.all([
  import(new URL("/shared/browser-shell.js", document.baseURI).href),
  import(new URL("/shared/demo-pages.js", document.baseURI).href),
  import(new URL("/shared/split-data.js", document.baseURI).href),
]);

/**
 * @param {Element} root
 * @param {string} selector
 * @returns {Element}
 */
function requiredElement(root, selector) {
  const element = root.querySelector(selector);
  if (!element) throw new Error(`Split View composition is missing ${selector}`);
  return element;
}

function createDragOriginAffordance() {
  const affordance = document.createElement("div");
  affordance.className = "split-view-scene__drag-affordance";
  affordance.setAttribute("data-drag-origin", "sidebar-active-tab");
  affordance.setAttribute("data-drag-target", "split-divider");
  affordance.setAttribute("aria-hidden", "true");
  return affordance;
}

/** @param {Element} splitView */
function assertDomOnlySplitView(splitView) {
  if (splitView.querySelector("img, video, iframe, canvas, picture, source")) {
    throw new Error("Split View panes must remain local DOM-only surfaces");
  }
  if (splitView.querySelectorAll("[data-split-pane]").length !== 2) {
    throw new Error("Split View composition requires exactly two panes");
  }
  requiredElement(splitView, '[data-split-divider="continuous"]');
}

/** @param {Element} scene */
export function mountSplitViewComposition(scene) {
  const camera = requiredElement(scene, '[data-camera-wrapper="split-view"]');
  const shellHost = requiredElement(scene, "[data-scene-shell-host]");
  const shell = mountBrowserShell(shellHost);
  const content = requiredElement(shell, '[data-shell="browser-content"]');
  const splitView = mountSplitView(content, SPLIT_VIEW_PRESETS.settled);
  assertDomOnlySplitView(splitView);

  const activeTab = requiredElement(shell, '[data-active-tab="true"]');
  activeTab.setAttribute("data-drag-source", "split-view");
  const dragOrigin = createDragOriginAffordance();
  requiredElement(shell, '[data-shell="browser-window"]').append(dragOrigin);

  const signup = requiredElement(splitView, '[data-page-overlay="signup"]');
  const cookie = requiredElement(splitView, '[data-page-overlay="cookie"]');
  signup.setAttribute("data-overlay-scene-state", "action-final");
  cookie.setAttribute("data-overlay-scene-state", "final");

  return Object.freeze({ scene, camera, dragOrigin, signup, cookie });
}
