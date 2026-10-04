import {CrLitElement, html, css, customElement, state} from '../shared/lib/cr-lit-element.js';
import {getMojoHandler} from '../shared/lib/mojo-mock.js';
import type {MahoSettingsPageHandler} from '../shared/lib/mojo-mock.js';
import type {Settings, SettingValue} from '../shared/lib/types.js';
import './settings-section.js';

type SectionId = 'general' | 'appearance' | 'search' | 'privacy' | 'ai' | 'sync' | 'about';

interface NavItem {
  id: SectionId;
  label: string;
  icon: string;
}

interface CapabilityBullet {
  title: string;
  detail: string;
}

interface CapabilityStub {
  title: string;
  summary: string;
  missing: string;
  bullets?: CapabilityBullet[];
}

const NAV_ITEMS: NavItem[] = [
  {id: 'general', label: 'General', icon: '⚙️'},
  {id: 'appearance', label: 'Appearance', icon: '🎨'},
  {id: 'search', label: 'Search', icon: '🔍'},
  {id: 'privacy', label: 'Privacy', icon: '🔒'},
  {id: 'ai', label: 'AI', icon: '🤖'},
  {id: 'sync', label: 'Sync', icon: '🔄'},
  {id: 'about', label: 'About', icon: 'ℹ️'},
];

@customElement('maho-settings')
export class MahoSettings extends CrLitElement {
  static override styles = css`
    :host {
      display: flex;
      height: 100vh;
      background: var(--maho-bg, #1a1a1a);
      color: var(--maho-text, #e0e0e0);
    }

    .nav {
      width: 220px;
      border-right: 1px solid var(--maho-border, #333);
      padding: 20px 12px;
      display: flex;
      flex-direction: column;
      gap: 2px;
    }

    .nav h1 {
      font-size: 18px;
      font-weight: 700;
      padding: 0 12px 16px;
    }

    .nav-item {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 8px 12px;
      border-radius: 8px;
      font-size: 14px;
      cursor: pointer;
      transition: background 0.1s;
      border: none;
      background: none;
      color: var(--maho-text, #e0e0e0);
      width: 100%;
      text-align: left;
      font-family: inherit;
    }

    .nav-item:hover {
      background: var(--maho-hover, #333);
    }

    .nav-item.active {
      background: color-mix(in srgb, var(--maho-accent, #5B9CF6) 15%, transparent);
      color: var(--maho-accent, #5B9CF6);
    }

    .content {
      flex: 1;
      overflow-y: auto;
      padding: 32px 40px;
      max-width: 700px;
    }

    .content h2 {
      font-size: 22px;
      font-weight: 700;
      margin-bottom: 24px;
    }

    .field {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 12px 0;
      border-bottom: 1px solid var(--maho-border, #333);
    }

    .field-info {
      flex: 1;
    }

    .field-label {
      font-size: 14px;
      font-weight: 500;
    }

    .field-desc {
      font-size: 12px;
      color: var(--maho-text-secondary, #999);
      margin-top: 2px;
    }

    .field-control {
      flex-shrink: 0;
      margin-left: 16px;
    }

    select, input[type="text"] {
      background: var(--maho-surface, #2a2a2a);
      border: 1px solid var(--maho-border, #333);
      color: var(--maho-text, #e0e0e0);
      padding: 6px 10px;
      border-radius: 6px;
      font-size: 13px;
      font-family: inherit;
      min-width: 180px;
    }

    .toggle {
      position: relative;
      width: 40px;
      height: 22px;
      background: var(--maho-border, #333);
      border-radius: 11px;
      cursor: pointer;
      transition: background 0.2s;
      border: none;
      padding: 0;
    }

    .toggle.on {
      background: var(--maho-accent, #5B9CF6);
    }

    .toggle::after {
      content: '';
      position: absolute;
      top: 2px;
      left: 2px;
      width: 18px;
      height: 18px;
      background: white;
      border-radius: 50%;
      transition: transform 0.2s;
    }

    .toggle.on::after {
      transform: translateX(18px);
    }

    .about-info {
      text-align: center;
      padding: 40px 0;
    }

    .about-info .version {
      font-size: 32px;
      font-weight: 700;
      margin-bottom: 8px;
    }

    .about-info .build {
      font-size: 13px;
      color: var(--maho-text-secondary, #999);
    }

    .capability-stub {
      padding: 16px 0;
      display: flex;
      flex-direction: column;
      gap: 12px;
    }

    .capability-stub__summary {
      display: grid;
      gap: 6px;
    }

    .capability-stub__title {
      font-size: 14px;
      font-weight: 600;
      color: var(--maho-text, #e0e0e0);
    }

    .capability-stub__text {
      font-size: 13px;
      line-height: 1.45;
      color: var(--maho-text-secondary, #999);
    }

    .capability-stub__missing {
      font-size: 12px;
      color: var(--maho-accent, #5B9CF6);
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.4px;
    }

    .capability-stub__bullets {
      margin: 0;
      padding-left: 18px;
      display: grid;
      gap: 8px;
      color: var(--maho-text, #e0e0e0);
      font-size: 13px;
    }

    .capability-stub__bullets li {
      line-height: 1.4;
    }

    .capability-stub__bullet-detail {
      color: var(--maho-text-secondary, #999);
      margin-left: 4px;
    }
  `;

  @state() activeSection: SectionId = 'general';
  @state() settings: Settings | null = null;

  private handler = getMojoHandler<MahoSettingsPageHandler>('MahoSettingsPageHandler');

  override async connectedCallback() {
    super.connectedCallback();
    this.settings = await this.handler.getSettings();
  }

  private async updateSetting(key: string, value: SettingValue) {
    await this.handler.updateSetting(key, value);
    if (this.settings) {
      this.settings = {...this.settings, [key]: value};
    }
  }

  private onSelectChange(key: string, e: Event) {
    const value = (e.target as HTMLSelectElement).value;
    this.updateSetting(key, value);
  }

  private onInputChange(key: string, e: Event) {
    const value = (e.target as HTMLInputElement).value;
    this.updateSetting(key, value);
  }

  private renderToggle(key: string) {
    const val = this.settings?.[key] as boolean ?? false;
    return html`
      <button
        class="toggle ${val ? 'on' : ''}"
        @click=${() => this.updateSetting(key, !val)}
      ></button>
    `;
  }

  private renderCapabilityStub(stub: CapabilityStub) {
    return html`
      <div class="capability-stub">
        <div class="capability-stub__summary">
          <div class="capability-stub__title">${stub.title}</div>
          <div class="capability-stub__text">${stub.summary}</div>
          <div class="capability-stub__missing">Missing capability: ${stub.missing}</div>
        </div>
        ${stub.bullets?.length ? html`
          <ul class="capability-stub__bullets">
            ${stub.bullets.map(bullet => html`
              <li>
                <strong>${bullet.title}</strong>
                <span class="capability-stub__bullet-detail">${bullet.detail}</span>
              </li>
            `)}
          </ul>
        ` : html``}
      </div>
    `;
  }

  private renderSection() {
    if (!this.settings) return html``;

    switch (this.activeSection) {
      case 'general':
        return html`
          <h2>General</h2>
          <settings-section title="Startup">
            <div class="field">
              <div class="field-info">
                <div class="field-label">Default Browser</div>
                <div class="field-desc">Set Maho as your default browser</div>
              </div>
              <div class="field-control">${this.renderToggle('defaultBrowser')}</div>
            </div>
            <div class="field">
              <div class="field-info">
                <div class="field-label">Start Page</div>
                <div class="field-desc">Page shown when opening a new window</div>
              </div>
              <div class="field-control">
                <input type="text" .value=${this.settings.startPage as string} @change=${(e: Event) => this.onInputChange('startPage', e)} />
              </div>
            </div>
            <div class="field">
              <div class="field-info">
                <div class="field-label">Downloads Folder</div>
                <div class="field-desc">Where downloaded files are saved</div>
              </div>
              <div class="field-control">
                <input type="text" .value=${this.settings.downloadsFolder as string} @change=${(e: Event) => this.onInputChange('downloadsFolder', e)} />
              </div>
            </div>
            <div class="field">
              <div class="field-info">
                <div class="field-label">Language</div>
                <div class="field-desc">Browser display language</div>
              </div>
              <div class="field-control">
                <select @change=${(e: Event) => this.onSelectChange('language', e)}>
                  <option selected>English (US)</option>
                  <option>Korean</option>
                  <option>Japanese</option>
                </select>
              </div>
            </div>
          </settings-section>
        `;
      case 'appearance':
        return html`
          <h2>Appearance</h2>
          <settings-section title="Theme">
            <div class="field">
              <div class="field-info">
                <div class="field-label">Theme</div>
                <div class="field-desc">Choose light, dark, or system theme</div>
              </div>
              <div class="field-control">
                <select @change=${(e: Event) => this.onSelectChange('theme', e)}>
                  <option>System</option>
                  <option>Light</option>
                  <option>Dark</option>
                </select>
              </div>
            </div>
            <div class="field">
              <div class="field-info">
                <div class="field-label">Font Size</div>
                <div class="field-desc">Base font size for web content</div>
              </div>
              <div class="field-control">
                <select @change=${(e: Event) => this.onSelectChange('fontSize', e)}>
                  <option>12px</option>
                  <option selected>14px</option>
                  <option>16px</option>
                  <option>18px</option>
                </select>
              </div>
            </div>
          </settings-section>
        `;
      case 'search':
        return html`
          <h2>Search</h2>
          <settings-section title="Search Engine">
            <div class="field">
              <div class="field-info">
                <div class="field-label">Default Search Engine</div>
                <div class="field-desc">Used for address bar searches</div>
              </div>
              <div class="field-control">
                <select @change=${(e: Event) => this.onSelectChange('searchEngine', e)}>
                  <option selected>Google</option>
                  <option>DuckDuckGo</option>
                  <option>Bing</option>
                  <option>Brave Search</option>
                </select>
              </div>
            </div>
          </settings-section>
        `;
      case 'privacy':
        return html`
          <h2>Privacy</h2>
          <settings-section title="Tracking">
            <div class="field">
              <div class="field-info">
                <div class="field-label">Send Analytics</div>
                <div class="field-desc">Help improve Maho by sending anonymous usage data</div>
              </div>
              <div class="field-control">${this.renderToggle('sendAnalytics')}</div>
            </div>
            <div class="field">
              <div class="field-info">
                <div class="field-label">Do Not Track</div>
                <div class="field-desc">Request websites not to track you</div>
              </div>
              <div class="field-control">${this.renderToggle('doNotTrack')}</div>
            </div>
            <div class="field">
              <div class="field-info">
                <div class="field-label">Block Third-Party Cookies</div>
                <div class="field-desc">Prevent cross-site tracking cookies</div>
              </div>
              <div class="field-control">${this.renderToggle('blockThirdPartyCookies')}</div>
            </div>
          </settings-section>
        `;
      case 'ai':
        return html`
          <h2>AI</h2>
          <settings-section title="AI Assistant">
            ${this.renderCapabilityStub({
              title: 'AI Assistant capabilities are staged, not live',
              summary: 'This pane documents the assistant surface, but the browser-side AI service and provider plumbing are not implemented in this build yet.',
              missing: 'browser-side AI service',
              bullets: [
                {title: 'Enable AI', detail: 'future toggle for turning the assistant on or off'},
                {title: 'AI Provider', detail: 'future provider picker for Claude, OpenAI, Gemini, or local models'},
              ],
            })}
          </settings-section>
        `;
      case 'sync':
        return html`
          <h2>Sync</h2>
          <settings-section title="Sync Settings">
            ${this.renderCapabilityStub({
              title: 'Sync is a capability stub for now',
              summary: 'Cross-device sync needs account and transport plumbing that is not present yet, so this pane stays honest instead of pretending to control a real service.',
              missing: 'sync backend and account state',
              bullets: [
                {title: 'Enable Sync', detail: 'future master switch for the sync service'},
                {title: 'Sync Bookmarks', detail: 'future per-dataset sync control'},
                {title: 'Sync History', detail: 'future per-dataset sync control'},
                {title: 'Sync Open Tabs', detail: 'future per-dataset sync control'},
              ],
            })}
          </settings-section>
        `;
      case 'about':
        return html`
          <h2>About</h2>
          <div class="about-info">
            <div class="version">Maho 0.1.0</div>
            <div class="build">Built on Chromium 148</div>
            <div class="build" style="margin-top: 4px">A browser that thinks with you.</div>
          </div>
        `;
    }
  }

  override render() {
    return html`
      <nav class="nav">
        <h1>Settings</h1>
        ${NAV_ITEMS.map(item => html`
          <button
            class="nav-item ${this.activeSection === item.id ? 'active' : ''}"
            @click=${() => { this.activeSection = item.id; }}
          >
            <span>${item.icon}</span>
            <span>${item.label}</span>
          </button>
        `)}
      </nav>
      <main class="content">
        ${this.renderSection()}
      </main>
    `;
  }
}
