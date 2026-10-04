import {CrLitElement, html, css, customElement, state} from '../shared/lib/cr-lit-element.js';
import {getMojoHandler} from '../shared/lib/mojo-mock.js';
import type {MahoSidebarPageHandler} from '../shared/lib/mojo-mock.js';
import type {Tab, Space} from '../shared/lib/types.js';
import './tab-item.js';
import './space-switcher.js';

// TODO(B2.5): Implement sidebar collapse/expand with CSS animation + Side Panel toggle
@customElement('maho-sidebar')
export class MahoSidebar extends CrLitElement {
  static override styles = css`
    :host {
      width: 280px;
      height: 100vh;
      background: var(--maho-bg, #1a1a1a);
      border-right: 1px solid var(--maho-border, #333);
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }

    .header {
      padding: 12px 16px 8px;
      display: flex;
      align-items: center;
      justify-content: space-between;
    }

    .header h2 {
      font-size: 13px;
      font-weight: 600;
      color: var(--maho-text-secondary, #999);
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }

    .tab-count {
      font-size: 11px;
      color: var(--maho-text-secondary, #999);
      background: var(--maho-surface, #2a2a2a);
      padding: 2px 8px;
      border-radius: 10px;
    }

    .search {
      padding: 4px 12px 8px;
    }

    .search input {
      width: 100%;
      background: var(--maho-surface, #2a2a2a);
      border: 1px solid var(--maho-border, #333);
      border-radius: 6px;
      padding: 6px 10px;
      font-size: 13px;
      color: var(--maho-text, #e0e0e0);
      outline: none;
      transition: border-color 0.15s;
    }

    .search input:focus {
      border-color: var(--maho-accent, #5B9CF6);
    }

    .search input::placeholder {
      color: var(--maho-text-secondary, #666);
    }

    .tab-list {
      flex: 1;
      overflow-y: auto;
      padding: 0 8px;
    }

    .section-label {
      font-size: 11px;
      font-weight: 600;
      color: var(--maho-text-secondary, #999);
      padding: 8px 8px 4px;
      text-transform: uppercase;
      letter-spacing: 0.3px;
    }
  `;

  @state() tabs: Tab[] = [];
  @state() spaces: Space[] = [];
  @state() activeSpaceId: string = '';
  @state() searchQuery: string = '';

  private handler = getMojoHandler<MahoSidebarPageHandler>('MahoSidebarPageHandler');

  override async connectedCallback() {
    super.connectedCallback();
    this.tabs = await this.handler.getTabs();
    this.spaces = await this.handler.getSpaces();
    const activeSpace = this.spaces.find(s => s.isActive);
    if (activeSpace) this.activeSpaceId = activeSpace.id;
  }

  private get filteredTabs(): Tab[] {
    let tabs = this.activeSpaceId
      ? this.tabs.filter(t => t.spaceId === this.activeSpaceId)
      : this.tabs;

    if (this.searchQuery) {
      const q = this.searchQuery.toLowerCase();
      tabs = tabs.filter(t => t.title.toLowerCase().includes(q) || t.url.toLowerCase().includes(q));
    }
    return tabs;
  }

  private get pinnedTabs(): Tab[] {
    return this.filteredTabs.filter(t => t.isPinned);
  }

  private get regularTabs(): Tab[] {
    return this.filteredTabs
      .filter(t => !t.isPinned)
      .sort((a, b) => b.lastAccessedAt - a.lastAccessedAt);
  }

  private onSpaceChanged(e: CustomEvent<string>) {
    this.activeSpaceId = e.detail;
  }

  private onSearchInput(e: Event) {
    this.searchQuery = (e.target as HTMLInputElement).value;
  }

  private async onTabClose(e: CustomEvent<string>) {
    await this.handler.closeTab(e.detail);
    this.tabs = this.tabs.filter(t => t.id !== e.detail);
  }

  private async onTabClick(e: CustomEvent<string>) {
    await this.handler.switchTab(e.detail);
    this.tabs = this.tabs.map(t => ({...t, isActive: t.id === e.detail}));
  }

  override render() {
    return html`
      <space-switcher
        .spaces=${this.spaces}
        .activeSpaceId=${this.activeSpaceId}
        @space-changed=${this.onSpaceChanged}
      ></space-switcher>

      <div class="header">
        <h2>Tabs</h2>
        <span class="tab-count">${this.filteredTabs.length}</span>
      </div>

      <div class="search">
        <input
          type="text"
          placeholder="Search tabs…"
          .value=${this.searchQuery}
          @input=${this.onSearchInput}
        />
      </div>

      ${/* TODO(B2.1a): Implement virtual scrolling with Lit repeat directive + IntersectionObserver for 100+ tabs */ ''}
      <div class="tab-list">
        ${this.pinnedTabs.length > 0 ? html`
          <div class="section-label">Pinned</div>
          ${this.pinnedTabs.map(tab => html`
            <tab-item
              .tab=${tab}
              @tab-close=${this.onTabClose}
              @tab-click=${this.onTabClick}
            ></tab-item>
          `)}
        ` : ''}

        ${this.regularTabs.length > 0 ? html`
          <div class="section-label">Open</div>
          ${this.regularTabs.map(tab => html`
            <tab-item
              .tab=${tab}
              @tab-close=${this.onTabClose}
              @tab-click=${this.onTabClick}
            ></tab-item>
          `)}
        ` : ''}
      </div>
    `;
  }
}
