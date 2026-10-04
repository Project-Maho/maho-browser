import {CrLitElement, html, css, customElement, state} from '../shared/lib/cr-lit-element.js';
import {getMojoHandler} from '../shared/lib/mojo-mock.js';
import type {MahoBoostPageHandler} from '../shared/lib/mojo-mock.js';

interface BoostStats {
  adsBlocked: number;
  trackersBlocked: number;
  httpsUpgrades: number;
  bandwidthSaved: string;
}

interface BoostSettings {
  adBlocking: boolean;
  trackerBlocking: boolean;
  httpsUpgrade: boolean;
  fingerprintProtection: boolean;
}

@customElement('maho-boost')
export class MahoBoost extends CrLitElement {
  static override styles = css`
    :host {
      display: block;
      height: 100vh;
      background: var(--maho-bg, #1a1a1a);
      color: var(--maho-text, #e0e0e0);
      padding: 32px 40px;
      overflow-y: auto;
    }

    h1 {
      font-size: 22px;
      font-weight: 700;
      margin-bottom: 24px;
    }

    .stats-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
      gap: 12px;
      margin-bottom: 32px;
    }

    .stat-card {
      background: var(--maho-surface, #2a2a2a);
      border-radius: 10px;
      padding: 16px;
    }

    .stat-value {
      font-size: 28px;
      font-weight: 700;
      color: var(--maho-accent, #5B9CF6);
    }

    .stat-label {
      font-size: 12px;
      color: var(--maho-text-secondary, #999);
      margin-top: 4px;
    }

    h2 {
      font-size: 16px;
      font-weight: 600;
      margin-bottom: 12px;
    }

    .toggle-list {
      display: flex;
      flex-direction: column;
      gap: 2px;
      background: var(--maho-surface, #2a2a2a);
      border-radius: 10px;
      padding: 4px 16px;
      max-width: 500px;
    }

    .toggle-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 12px 0;
      border-bottom: 1px solid var(--maho-border, #333);
    }

    .toggle-row:last-child {
      border-bottom: none;
    }

    .toggle-label {
      font-size: 14px;
      font-weight: 500;
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
  `;

  @state() stats: BoostStats = {adsBlocked: 0, trackersBlocked: 0, httpsUpgrades: 0, bandwidthSaved: '0 MB'};
  @state() boostSettings: BoostSettings = {adBlocking: true, trackerBlocking: true, httpsUpgrade: true, fingerprintProtection: false};

  private handler = getMojoHandler<MahoBoostPageHandler>('MahoBoostPageHandler');

  override async connectedCallback() {
    super.connectedCallback();
    this.stats = await this.handler.getStats();
    this.boostSettings = await this.handler.getSettings();
  }

  private async onToggle(key: keyof BoostSettings) {
    const newValue = !this.boostSettings[key];
    await this.handler.toggleSetting(key, newValue);
    this.boostSettings = {...this.boostSettings, [key]: newValue};
  }

  override render() {
    return html`
      <h1>Boost</h1>

      <div class="stats-grid">
        <div class="stat-card">
          <div class="stat-value">${this.stats.adsBlocked.toLocaleString()}</div>
          <div class="stat-label">Ads Blocked</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">${this.stats.trackersBlocked.toLocaleString()}</div>
          <div class="stat-label">Trackers Blocked</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">${this.stats.httpsUpgrades.toLocaleString()}</div>
          <div class="stat-label">HTTPS Upgrades</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">${this.stats.bandwidthSaved}</div>
          <div class="stat-label">Bandwidth Saved</div>
        </div>
      </div>

      <h2>Protection Settings</h2>
      <div class="toggle-list">
        <div class="toggle-row">
          <span class="toggle-label">Ad Blocking</span>
          <button class="toggle ${this.boostSettings.adBlocking ? 'on' : ''}" @click=${() => this.onToggle('adBlocking')}></button>
        </div>
        <div class="toggle-row">
          <span class="toggle-label">Tracker Blocking</span>
          <button class="toggle ${this.boostSettings.trackerBlocking ? 'on' : ''}" @click=${() => this.onToggle('trackerBlocking')}></button>
        </div>
        <div class="toggle-row">
          <span class="toggle-label">HTTPS Upgrade</span>
          <button class="toggle ${this.boostSettings.httpsUpgrade ? 'on' : ''}" @click=${() => this.onToggle('httpsUpgrade')}></button>
        </div>
        <div class="toggle-row">
          <span class="toggle-label">Fingerprint Protection</span>
          <button class="toggle ${this.boostSettings.fingerprintProtection ? 'on' : ''}" @click=${() => this.onToggle('fingerprintProtection')}></button>
        </div>
      </div>
    `;
  }
}
