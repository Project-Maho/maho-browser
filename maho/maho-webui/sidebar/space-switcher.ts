import {CrLitElement, html, css, customElement, property} from '../shared/lib/cr-lit-element.js';
import type {Space} from '../shared/lib/types.js';

const COLOR_MAP: Record<string, string> = {
  red: '#e5484d',
  blue: '#3b82f6',
  green: '#30a46c',
  purple: '#8b5cf6',
  orange: '#f97316',
  pink: '#ec4899',
  cyan: '#06b6d4',
  yellow: '#eab308',
};

@customElement('space-switcher')
export class SpaceSwitcher extends CrLitElement {
  static override styles = css`
    :host {
      display: block;
      padding: 12px 12px 4px;
    }

    .spaces {
      display: flex;
      gap: 6px;
      align-items: center;
    }

    .space-dot {
      position: relative;
      width: 32px;
      height: 32px;
      border-radius: 8px;
      border: 2px solid transparent;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 16px;
      transition: border-color 0.15s, transform 0.1s;
    }

    .space-dot:hover {
      transform: scale(1.1);
    }

    .space-dot.active {
      border-color: var(--maho-text, #e0e0e0);
    }

    .all-btn {
      width: 32px;
      height: 32px;
      border-radius: 8px;
      border: 1px solid var(--maho-border, #333);
      background: transparent;
      color: var(--maho-text-secondary, #999);
      font-size: 12px;
      font-weight: 600;
      cursor: pointer;
      transition: border-color 0.15s;
    }

    .all-btn:hover {
      border-color: var(--maho-text-secondary, #999);
    }

    .all-btn.active {
      border-color: var(--maho-text, #e0e0e0);
      color: var(--maho-text, #e0e0e0);
    }

    .badge {
      position: absolute;
      top: -4px;
      right: -4px;
      font-size: 9px;
      background: var(--maho-surface, #2a2a2a);
      color: var(--maho-text-secondary, #999);
      min-width: 14px;
      height: 14px;
      border-radius: 7px;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 0 3px;
    }
  `;

  @property({attribute: false}) spaces: Space[] = [];
  @property() activeSpaceId: string = '';

  private onSpaceClick(spaceId: string) {
    this.fire('space-changed', spaceId);
  }

  private onAllClick() {
    this.fire('space-changed', '');
  }

  override render() {
    return html`
      <div class="spaces">
        <button
          class="all-btn ${this.activeSpaceId === '' ? 'active' : ''}"
          @click=${this.onAllClick}
        >All</button>
        ${this.spaces.map(space => {
          const color = COLOR_MAP[space.color] ?? '#666';
          return html`
            <div
              class="space-dot ${space.id === this.activeSpaceId ? 'active' : ''}"
              style="background: ${color}"
              @click=${() => this.onSpaceClick(space.id)}
              title=${space.name}
            >
              <span>${space.icon || '📁'}</span>
              <span class="badge">${space.tabIds.length}</span>
            </div>
          `;
        })}
      </div>
    `;
  }
}
