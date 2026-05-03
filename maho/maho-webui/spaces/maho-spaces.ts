import {CrLitElement, html, css, customElement, state} from '../shared/lib/cr-lit-element.js';
import {getMojoHandler} from '../shared/lib/mojo-mock.js';
import type {MahoSpacesPageHandler} from '../shared/lib/mojo-mock.js';
import type {Space} from '../shared/lib/types.js';

const COLOR_HEX: Record<string, string> = {
  red: '#e5484d',
  blue: '#3b82f6',
  green: '#30a46c',
  purple: '#8b5cf6',
  orange: '#f97316',
  pink: '#ec4899',
  cyan: '#06b6d4',
  yellow: '#eab308',
};

@customElement('maho-spaces')
export class MahoSpaces extends CrLitElement {
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

    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
      gap: 16px;
    }

    .space-card {
      background: var(--maho-surface, #2a2a2a);
      border: 1px solid var(--maho-border, #333);
      border-radius: 12px;
      padding: 20px;
      cursor: pointer;
      transition: border-color 0.15s;
    }

    .space-card:hover {
      border-color: var(--maho-text-secondary, #666);
    }

    .space-header {
      display: flex;
      align-items: center;
      gap: 10px;
      margin-bottom: 12px;
    }

    .space-color {
      width: 16px;
      height: 16px;
      border-radius: 4px;
    }

    .space-icon {
      font-size: 18px;
    }

    .space-name {
      font-size: 16px;
      font-weight: 600;
    }

    .space-tabs {
      font-size: 13px;
      color: var(--maho-text-secondary, #999);
    }

    .create-card {
      background: transparent;
      border: 2px dashed var(--maho-border, #333);
      border-radius: 12px;
      padding: 20px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: 8px;
      cursor: pointer;
      transition: border-color 0.15s;
      min-height: 110px;
    }

    .create-card:hover {
      border-color: var(--maho-accent, #5B9CF6);
    }

    .create-icon {
      font-size: 24px;
      color: var(--maho-text-secondary, #666);
    }

    .create-label {
      font-size: 13px;
      color: var(--maho-text-secondary, #999);
    }
  `;

  @state() spaces: Space[] = [];

  private handler = getMojoHandler<MahoSpacesPageHandler>('MahoSpacesPageHandler');

  override async connectedCallback() {
    super.connectedCallback();
    this.spaces = await this.handler.getSpaces();
  }

  private async onCreateSpace() {
    const space = await this.handler.createSpace('New Space', 'blue', '📁');
    this.spaces = [...this.spaces, space];
  }

  override render() {
    return html`
      <h1>Spaces</h1>
      <div class="grid">
        ${this.spaces.map(space => html`
          <div class="space-card">
            <div class="space-header">
              <div class="space-color" style="background: ${COLOR_HEX[space.color] ?? '#666'}"></div>
              <span class="space-icon">${space.icon}</span>
              <span class="space-name">${space.name}</span>
            </div>
            <div class="space-tabs">${space.tabIds.length} tabs</div>
          </div>
        `)}
        <div class="create-card" @click=${this.onCreateSpace}>
          <span class="create-icon">+</span>
          <span class="create-label">Create Space</span>
        </div>
      </div>
    `;
  }
}
