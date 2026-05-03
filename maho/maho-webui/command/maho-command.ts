import {CrLitElement, html, css, customElement, state} from '../shared/lib/cr-lit-element.js';
import {getMojoHandler} from '../shared/lib/mojo-mock.js';
import type {MahoCommandPageHandler} from '../shared/lib/mojo-mock.js';
import type {Suggestion} from '../shared/lib/types.js';
import './suggestion-item.js';

@customElement('maho-command')
export class MahoCommand extends CrLitElement {
  static override styles = css`
    :host {
      display: flex;
      align-items: flex-start;
      justify-content: center;
      padding-top: 120px;
      width: 100vw;
      height: 100vh;
      background: rgba(0, 0, 0, 0.5);
    }

    .command-box {
      width: 600px;
      max-height: 480px;
      background: var(--maho-bg, #1a1a1a);
      border: 1px solid var(--maho-border, #333);
      border-radius: 12px;
      box-shadow: var(--maho-shadow-lg, 0 8px 24px rgba(0,0,0,0.4));
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }

    .search-row {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 14px 16px;
      border-bottom: 1px solid var(--maho-border, #333);
    }

    .search-icon {
      font-size: 18px;
      color: var(--maho-text-secondary, #999);
      flex-shrink: 0;
    }

    .search-input {
      flex: 1;
      background: none;
      border: none;
      font-size: 16px;
      color: var(--maho-text, #e0e0e0);
      outline: none;
      font-family: var(--maho-font-family, system-ui);
    }

    .search-input::placeholder {
      color: var(--maho-text-secondary, #666);
    }

    .results {
      flex: 1;
      overflow-y: auto;
      padding: 8px;
    }

    .section-header {
      font-size: 11px;
      font-weight: 600;
      color: var(--maho-text-secondary, #999);
      text-transform: uppercase;
      letter-spacing: 0.3px;
      padding: 8px 8px 4px;
    }

    .empty {
      text-align: center;
      color: var(--maho-text-secondary, #666);
      padding: 32px 16px;
      font-size: 13px;
    }

    .footer {
      padding: 8px 16px;
      border-top: 1px solid var(--maho-border, #333);
      display: flex;
      gap: 16px;
      font-size: 11px;
      color: var(--maho-text-secondary, #666);
    }

    kbd {
      background: var(--maho-surface, #2a2a2a);
      padding: 1px 5px;
      border-radius: 3px;
      font-size: 10px;
      font-family: inherit;
    }
  `;

  @state() query: string = '';
  @state() suggestions: Suggestion[] = [];
  @state() selectedIndex: number = 0;

  private handler = getMojoHandler<MahoCommandPageHandler>('MahoCommandPageHandler');

  override async connectedCallback() {
    super.connectedCallback();
    this.suggestions = await this.handler.search('');
  }

  private async onInput(e: Event) {
    this.query = (e.target as HTMLInputElement).value;
    this.selectedIndex = 0;
    this.suggestions = await this.handler.search(this.query);
  }

  private onKeyDown(e: KeyboardEvent) {
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      this.selectedIndex = Math.min(this.selectedIndex + 1, this.suggestions.length - 1);
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      this.selectedIndex = Math.max(this.selectedIndex - 1, 0);
    } else if (e.key === 'Enter') {
      if (this.selectedIndex >= 0 && this.selectedIndex < this.suggestions.length) {
        const selected = this.suggestions[this.selectedIndex];
        this.fire('command-execute', {suggestion: selected});
        this.close();
      }
    } else if (e.key === 'Escape') {
      this.close();
    }
  }

  private close() {
    this.query = '';
    this.selectedIndex = 0;
    this.fire('command-close');
  }

  private groupedSuggestions() {
    const groups: Record<string, Suggestion[]> = {};
    for (const s of this.suggestions) {
      const label = s.type === 'tab' ? 'Open Tabs' : s.type === 'history' ? 'History' : s.type === 'bookmark' ? 'Bookmarks' : 'Actions';
      if (!groups[label]) groups[label] = [];
      groups[label].push(s);
    }
    return groups;
  }

  override render() {
    const groups = this.groupedSuggestions();
    let globalIndex = 0;

    return html`
      <div class="command-box">
        <div class="search-row">
          <span class="search-icon">🔍</span>
          <input
            class="search-input"
            type="text"
            placeholder="Search tabs, history, bookmarks, actions…"
            .value=${this.query}
            @input=${this.onInput}
            @keydown=${this.onKeyDown}
            autofocus
          />
        </div>
        <div class="results">
          ${this.suggestions.length === 0 ? html`
            <div class="empty">No results for "${this.query}"</div>
          ` : Object.entries(groups).map(([label, items]) => html`
            <div class="section-header">${label}</div>
            ${items.map(item => {
              const idx = globalIndex++;
              return html`
                <suggestion-item
                  .suggestion=${item}
                  .selected=${idx === this.selectedIndex}
                ></suggestion-item>
              `;
            })}
          `)}
        </div>
        <div class="footer">
          <span><kbd>↑↓</kbd> Navigate</span>
          <span><kbd>↵</kbd> Open</span>
          <span><kbd>Esc</kbd> Close</span>
        </div>
      </div>
    `;
  }
}
