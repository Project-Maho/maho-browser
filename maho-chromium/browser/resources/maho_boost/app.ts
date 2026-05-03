import {PageHandlerFactory, PageHandlerRemote, SiteRule} from './maho_boost.mojom-webui.js';
import {PageCallbackRouter} from './maho_boost.mojom-webui.js';

let pageHandler: PageHandlerRemote;
let callbackRouter: PageCallbackRouter;

function initialize() {
  callbackRouter = new PageCallbackRouter();
  pageHandler = new PageHandlerRemote();

  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      pageHandler.$.bindNewPipeAndPassReceiver());

  callbackRouter.rulesChanged.addListener((rule: SiteRule) => {
    renderBoost(rule);
  });

  pageHandler.getRulesForSite('example.com').then(({rule}: {rule: SiteRule}) => {
    renderBoost(rule);
  });
}

function renderBoost(rule: SiteRule) {
  const container = document.querySelector('maho-boost-app');
  if (!container) return;
  container.innerHTML = '';
  (container as HTMLElement).style.cssText = 'display: flex; flex-direction: column; height: 100vh;';

  const header = document.createElement('div');
  header.style.cssText = 'padding: 24px 32px; border-bottom: 1px solid rgba(255,255,255,0.1);';
  header.innerHTML = `<h1 style="margin:0 0 8px 0; color:#e94560; font-size:24px;">Boost</h1><div style="font-size:14px; color:#888;">Site: <strong style="color:#e0e0e0;">${escapeHtml(rule.domain)}</strong></div>`;
  container.appendChild(header);

  const controls = document.createElement('div');
  controls.style.cssText = 'padding: 24px 32px;';

  const makeToggle = (label: string, value: boolean, onChange: (v: boolean) => void) => {
    const row = document.createElement('div');
    row.style.cssText = 'display: flex; justify-content: space-between; align-items: center; padding: 12px 16px; background: rgba(255,255,255,0.05); border-radius: 8px; margin-bottom: 8px;';
    const lbl = document.createElement('span');
    lbl.textContent = label;
    lbl.style.cssText = 'font-size: 14px;';
    const btn = document.createElement('button');
    btn.textContent = value ? 'ON' : 'OFF';
    btn.style.cssText = `padding: 6px 16px; border: none; border-radius: 6px; cursor: pointer; font-size: 13px; background: ${value ? '#e94560' : '#333'}; color: white;`;
    btn.addEventListener('click', () => onChange(!value));
    row.appendChild(lbl);
    row.appendChild(btn);
    return row;
  };

  controls.appendChild(makeToggle('JavaScript Enabled', rule.jsEnabled, (v) => {
    pageHandler.toggleJs(rule.domain, v);
  }));
  controls.appendChild(makeToggle('Block Ads', rule.adsBlocked, (v) => {
    pageHandler.toggleAds(rule.domain, v);
  }));

  const cssLabel = document.createElement('div');
  cssLabel.textContent = 'Custom CSS Rules';
  cssLabel.style.cssText = 'font-size: 14px; font-weight: 500; margin: 16px 0 8px 0;';
  controls.appendChild(cssLabel);

  const cssEditor = document.createElement('textarea');
  cssEditor.value = rule.cssRules;
  cssEditor.placeholder = 'Enter custom CSS for this site...';
  cssEditor.style.cssText = 'width: 100%; min-height: 200px; background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.1); border-radius: 8px; color: #e0e0e0; padding: 12px; font-family: monospace; font-size: 13px; resize: vertical; outline: none; box-sizing: border-box;';

  const saveBtn = document.createElement('button');
  saveBtn.textContent = 'Save CSS';
  saveBtn.style.cssText = 'margin-top: 8px; padding: 8px 20px; background: #e94560; border: none; border-radius: 6px; color: white; cursor: pointer; font-size: 13px;';
  saveBtn.addEventListener('click', () => {
    pageHandler.setCssRules(rule.domain, cssEditor.value);
  });

  controls.appendChild(cssEditor);
  controls.appendChild(saveBtn);
  container.appendChild(controls);
}

function escapeHtml(text: string): string {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

document.addEventListener('DOMContentLoaded', initialize);
