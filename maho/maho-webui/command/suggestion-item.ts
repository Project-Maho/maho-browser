import {CrLitElement, html, css, customElement, property} from '../shared/lib/cr-lit-element.js';
import type {Suggestion} from '../shared/lib/types.js';

const TYPE_LABELS: Record<string, string> = {
  tab: 'Tab',
  history: 'History',
  bookmark: 'Bookmark',
  action: 'Action',
};

const SHORTCUT_MAP: Record<string, string> = {
  'New Tab': '⌘T',
  'Clear Browsing Data': '⌘⇧⌫',
  'Toggle Dark Mode': '⌘⇧D',
};

@customElement('suggestion-item')
export class SuggestionItem extends CrLitElement {
  static override styles = css`
    :host {
      display: block;
    }

    .row {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 8px 10px;
      border-radius: 8px;
      cursor: pointer;
      transition: background 0.1s;
    }

    .row:hover,
    .row.selected {
      background: var(--maho-hover, #333);
    }

    .icon {
      width: 20px;
      text-align: center;
      font-size: 14px;
      color: var(--maho-text-secondary, #999);
      flex-shrink: 0;
    }

    .content {
      flex: 1;
      min-width: 0;
    }

    .title {
      font-size: 14px;
      color: var(--maho-text, #e0e0e0);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .subtitle {
      font-size: 12px;
      color: var(--maho-text-secondary, #999);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .meta {
      display: flex;
      align-items: center;
      gap: 8px;
      flex-shrink: 0;
    }

    .type-badge {
      font-size: 10px;
      padding: 2px 6px;
      border-radius: 4px;
      background: var(--maho-surface, #2a2a2a);
      color: var(--maho-text-secondary, #999);
    }

    .shortcut {
      font-size: 11px;
      color: var(--maho-text-secondary, #666);
    }
  `;

  @property({attribute: false}) suggestion!: Suggestion;
  @property({type: Boolean}) selected = false;

  private get iconChar(): string {
    switch (this.suggestion.type) {
      case 'tab': return '⬜';
      case 'history': return '🕐';
      case 'bookmark': return '⭐';
      case 'action': return '⚡';
      default: return '•';
    }
  }

  override render() {
    const s = this.suggestion;
    const shortcut = SHORTCUT_MAP[s.title];
    return html`
      <div class="row ${this.selected ? 'selected' : ''}">
        <span class="icon">${this.iconChar}</span>
        <div class="content">
          <div class="title">${s.title}</div>
          ${s.subtitle ? html`<div class="subtitle">${s.subtitle}</div>` : ''}
        </div>
        <div class="meta">
          ${shortcut ? html`<span class="shortcut">${shortcut}</span>` : ''}
          <span class="type-badge">${TYPE_LABELS[s.type] ?? s.type}</span>
        </div>
      </div>
    `;
  }
}
