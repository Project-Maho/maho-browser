import { SHELL_DATA } from "./shell-data.js";

export const BROWSER_SHELL_GEOMETRY = Object.freeze({
  canvasWidth: 1920,
  canvasHeight: 1080,
  width: 1440,
  height: 1033,
  left: 240,
  top: 23.5,
  sidebarWidth: 304,
});

function controlMarkup(control = { key: "", label: "", icon: "" }) {
  return `<span class="shell-icon-button" data-toolbar-control="${control.key}" aria-label="${control.label}">${control.icon}</span>`;
}

function shortcutMarkup(shortcut = { key: "", label: "", tone: "", icon: "" }) {
  return `<div class="shell-shortcut" data-shortcut="${shortcut.key}" data-shortcut-tone="${shortcut.tone}" aria-label="${shortcut.label}"><span class="shell-shortcut__icon">${shortcut.icon}</span></div>`;
}

function tabMarkup(tab = { key: "", label: "", icon: "", active: false }) {
  const active = tab.active ? ' data-active-tab="true"' : "";
  return `<div class="shell-tab" data-tab-row="${tab.key}"${active}><span class="shell-tab__icon">${tab.icon}</span><span class="shell-tab__label">${tab.label}</span></div>`;
}

function actionMarkup(action = { key: "", label: "", icon: "" }) {
  return `<span class="shell-tab-action" data-shell-action="${action.key}"><span class="shell-tab-action__icon">${action.icon}</span><span>${action.label}</span></span>`;
}

function dotMarkup(dot = "", activeDot = "") {
  const active = dot === activeDot ? ' data-active="true"' : "";
  return `<span class="shell-space-dot" data-space-dot="${dot}"${active}></span>`;
}

export function browserShellMarkup(data = SHELL_DATA) {
  const sidebarToggle = data.toolbarControls[0];
  const navigationControls = data.toolbarControls.slice(1);

  return `
    <section
      data-shell="browser-shell"
      aria-label="Maho browser shell"
      style="--shell-width:${BROWSER_SHELL_GEOMETRY.width}px;--shell-height:${BROWSER_SHELL_GEOMETRY.height}px;--shell-sidebar-width:${BROWSER_SHELL_GEOMETRY.sidebarWidth}px"
    >
      <div data-shell="browser-window">
        <aside data-shell="browser-sidebar">
          <div data-shell="sidebar-translucent-tint" aria-hidden="true"></div>
          <div class="shell-sidebar__content">
            <header data-shell="window-controls">
              <div class="shell-traffic-lights" aria-hidden="true">
                <span data-traffic-light="close"></span>
                <span data-traffic-light="minimize"></span>
                <span data-traffic-light="zoom"></span>
              </div>
              ${controlMarkup(sidebarToggle)}
              <div class="shell-toolbar-nav">${navigationControls.map(controlMarkup).join("")}</div>
            </header>

            <div data-shell="address-pill">
              <span class="shell-address__icon">${iconsForAddress(data)}</span>
              <span class="shell-address__text">${data.address}</span>
            </div>

            <div data-shell="shortcut-grid">${data.shortcuts.map(shortcutMarkup).join("")}</div>

            <div class="shell-workspace" data-shell="tab-groups">
              <div class="shell-workspace__label">${data.workspaceLabel}</div>
              <div data-tab-group="primary">${data.primaryTabs.map(tabMarkup).join("")}</div>
              <div data-shell="separator"></div>
              <div data-shell="tab-actions">${data.tabActions.map(actionMarkup).join("")}</div>
              <div data-tab-group="secondary">${data.secondaryTabs.map(tabMarkup).join("")}</div>
            </div>

            <footer data-shell="space-controls">
              <span class="shell-footer-button" data-footer-control="${data.footerControls.leading.key}" aria-label="${data.footerControls.leading.label}">${data.footerControls.leading.icon}</span>
              <div class="shell-space-dots">${data.footerControls.dots.map((dot) => dotMarkup(dot, data.footerControls.activeDot)).join("")}</div>
              <span class="shell-footer-button" data-footer-control="${data.footerControls.trailing.key}" aria-label="${data.footerControls.trailing.label}">${data.footerControls.trailing.icon}</span>
            </footer>
          </div>
        </aside>
        <main data-shell="browser-content" aria-label="Browser content slot"></main>
      </div>
    </section>
  `;
}

function iconsForAddress(data = SHELL_DATA) {
  return data.shortcuts.find((shortcut) => shortcut.key === "search")?.icon ?? "";
}

export function mountBrowserShell(target = document.createElement("div"), data = SHELL_DATA) {
  target.innerHTML = browserShellMarkup(data);
  return target.querySelector('[data-shell="browser-shell"]');
}
