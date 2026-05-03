import {CrLitElement, html, css, customElement, state} from '../shared/lib/cr-lit-element.js';
import {getMojoHandler} from '../shared/lib/mojo-mock.js';
import type {MahoEaselPageHandler} from '../shared/lib/mojo-mock.js';

interface Canvas {
  id: string;
  name: string;
  createdAt: string;
  thumbnail: string;
}

type ToolId = 'select' | 'pen' | 'text' | 'shape';

@customElement('maho-easel')
export class MahoEasel extends CrLitElement {
  static override styles = css`
    :host {
      display: flex;
      flex-direction: column;
      height: 100vh;
      background: var(--maho-bg, #1a1a1a);
      color: var(--maho-text, #e0e0e0);
    }

    .toolbar {
      display: flex;
      align-items: center;
      gap: 4px;
      padding: 8px 16px;
      border-bottom: 1px solid var(--maho-border, #333);
    }

    .tool-btn {
      width: 32px;
      height: 32px;
      border: 1px solid var(--maho-border, #333);
      background: none;
      color: var(--maho-text-secondary, #999);
      border-radius: 6px;
      cursor: pointer;
      font-size: 14px;
      transition: background 0.1s;
    }

    .tool-btn:hover {
      background: var(--maho-hover, #333);
    }

    .tool-btn.active {
      background: color-mix(in srgb, var(--maho-accent, #5B9CF6) 20%, transparent);
      border-color: var(--maho-accent, #5B9CF6);
      color: var(--maho-accent, #5B9CF6);
    }

    .spacer { flex: 1; }

    .capture-btn {
      background: var(--maho-accent, #5B9CF6);
      color: white;
      border: none;
      padding: 6px 14px;
      border-radius: 6px;
      font-size: 13px;
      cursor: pointer;
    }

    .capture-btn:hover {
      background: var(--maho-accent-hover, #7bb3f7);
    }

    .canvas-area {
      flex: 1;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    .empty-state {
      text-align: center;
      color: var(--maho-text-secondary, #666);
    }

    .empty-state h2 {
      font-size: 18px;
      margin-bottom: 8px;
      color: var(--maho-text, #e0e0e0);
    }

    .empty-state p {
      font-size: 13px;
      margin-bottom: 16px;
    }

    .create-btn {
      background: var(--maho-accent, #5B9CF6);
      color: white;
      border: none;
      padding: 8px 20px;
      border-radius: 8px;
      font-size: 14px;
      cursor: pointer;
    }

    .create-btn:hover {
      background: var(--maho-accent-hover, #7bb3f7);
    }
  `;

  @state() canvases: Canvas[] = [];
  @state() activeTool: ToolId = 'select';

  private handler = getMojoHandler<MahoEaselPageHandler>('MahoEaselPageHandler');

  override async connectedCallback() {
    super.connectedCallback();
    this.canvases = await this.handler.getCanvases();
  }

  private async onCreateCanvas() {
    const result = await this.handler.createCanvas('Untitled Canvas');
    this.canvases = [...this.canvases, {id: result.id, name: 'Untitled Canvas', createdAt: new Date().toISOString(), thumbnail: ''}];
  }

  override render() {
    const tools: Array<{id: ToolId; icon: string; label: string}> = [
      {id: 'select', icon: '⬚', label: 'Select'},
      {id: 'pen', icon: '✏️', label: 'Pen'},
      {id: 'text', icon: 'T', label: 'Text'},
      {id: 'shape', icon: '◻', label: 'Shape'},
    ];

    return html`
      <div class="toolbar">
        ${tools.map(tool => html`
          <button
            class="tool-btn ${this.activeTool === tool.id ? 'active' : ''}"
            title=${tool.label}
            @click=${() => { this.activeTool = tool.id; }}
          >${tool.icon}</button>
        `)}
        <div class="spacer"></div>
        <button class="capture-btn">📸 Capture Page</button>
      </div>

      <div class="canvas-area">
        <div class="empty-state">
          <h2>Create your first canvas</h2>
          <p>Capture screenshots, annotate pages, and create visual bookmarks.</p>
          <button class="create-btn" @click=${this.onCreateCanvas}>New Canvas</button>
        </div>
      </div>
    `;
  }
}
