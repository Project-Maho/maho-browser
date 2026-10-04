import {CrLitElement, html, css, customElement, property} from '../shared/lib/cr-lit-element.js';

@customElement('settings-section')
export class SettingsSection extends CrLitElement {
  static override styles = css`
    :host {
      display: block;
      margin-bottom: 32px;
    }

    .section-title {
      font-size: 12px;
      font-weight: 600;
      color: var(--maho-text-secondary, #999);
      text-transform: uppercase;
      letter-spacing: 0.5px;
      margin-bottom: 8px;
    }

    .section-content {
      background: var(--maho-surface, #2a2a2a);
      border-radius: 10px;
      padding: 4px 16px;
    }
  `;

  @property() title = '';

  override render() {
    return html`
      <div class="section-title">${this.title}</div>
      <div class="section-content">
        <slot></slot>
      </div>
    `;
  }
}
