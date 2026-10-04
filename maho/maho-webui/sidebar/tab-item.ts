import {CrLitElement, html, css, customElement, property} from '../shared/lib/cr-lit-element.js';
import type {Tab} from '../shared/lib/types.js';
import {extractDomain} from '../shared/utils/url.js';
import {getFaviconUrl} from '../shared/utils/favicon.js';

@customElement('tab-item')
export class TabItem extends CrLitElement {
  static override styles = css`
    :host {
      display: block;
    }

    .tab {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 6px 8px;
      border-radius: 6px;
      cursor: pointer;
      transition: background 0.1s;
      position: relative;
    }

    .tab:hover {
      background: var(--maho-hover, #333);
    }

    .tab.active {
      background: color-mix(in srgb, var(--maho-accent, #5B9CF6) 15%, transparent);
    }

    .favicon {
      width: 16px;
      height: 16px;
      border-radius: 2px;
      flex-shrink: 0;
    }

    .info {
      flex: 1;
      min-width: 0;
    }

    .title {
      font-size: 13px;
      color: var(--maho-text, #e0e0e0);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .domain {
      font-size: 11px;
      color: var(--maho-text-secondary, #999);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .indicators {
      display: flex;
      align-items: center;
      gap: 4px;
      flex-shrink: 0;
    }

    .audio-icon {
      font-size: 10px;
      color: var(--maho-accent, #5B9CF6);
    }

    .pin-icon {
      font-size: 10px;
      color: var(--maho-text-secondary, #666);
    }

    .close-btn {
      display: none;
      width: 18px;
      height: 18px;
      border: none;
      background: var(--maho-surface, #2a2a2a);
      color: var(--maho-text-secondary, #999);
      border-radius: 4px;
      cursor: pointer;
      font-size: 12px;
      line-height: 18px;
      text-align: center;
      flex-shrink: 0;
    }

    .tab:hover .close-btn {
      display: block;
    }

    .close-btn:hover {
      background: var(--maho-danger, #e5484d);
      color: white;
    }
  `;

  @property({attribute: false}) tab!: Tab;

  private onClick() {
    this.fire('tab-click', this.tab.id);
  }

  private onClose(e: Event) {
    e.stopPropagation();
    this.fire('tab-close', this.tab.id);
  }

  override render() {
    const tab = this.tab;
    return html`
      <div class="tab ${tab.isActive ? 'active' : ''}" @click=${this.onClick}>
        <img class="favicon" src=${getFaviconUrl(tab.url)} alt="" />
        <div class="info">
          <div class="title">${tab.title}</div>
          <div class="domain">${extractDomain(tab.url)}</div>
        </div>
        <div class="indicators">
          ${tab.isAudible ? html`<span class="audio-icon">🔊</span>` : ''}
          ${tab.isPinned ? html`<span class="pin-icon">📌</span>` : ''}
        </div>
        <button class="close-btn" @click=${this.onClose}>✕</button>
      </div>
    `;
  }
}
