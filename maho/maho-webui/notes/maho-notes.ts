import {CrLitElement, html, css, customElement, state} from '../shared/lib/cr-lit-element.js';
import {getMojoHandler} from '../shared/lib/mojo-mock.js';
import type {MahoNotesPageHandler} from '../shared/lib/mojo-mock.js';
import type {Note} from '../shared/lib/types.js';

@customElement('maho-notes')
export class MahoNotes extends CrLitElement {
  static override styles = css`
    :host {
      display: flex;
      height: 100vh;
      background: var(--maho-bg, #1a1a1a);
      color: var(--maho-text, #e0e0e0);
    }

    .note-list {
      width: 280px;
      border-right: 1px solid var(--maho-border, #333);
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }

    .list-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 16px;
      border-bottom: 1px solid var(--maho-border, #333);
    }

    .list-header h2 {
      font-size: 16px;
      font-weight: 600;
    }

    .new-btn {
      background: var(--maho-accent, #5B9CF6);
      color: white;
      border: none;
      padding: 4px 12px;
      border-radius: 6px;
      font-size: 12px;
      cursor: pointer;
    }

    .new-btn:hover {
      background: var(--maho-accent-hover, #7bb3f7);
    }

    .items {
      flex: 1;
      overflow-y: auto;
    }

    .note-item {
      padding: 12px 16px;
      border-bottom: 1px solid var(--maho-border, #333);
      cursor: pointer;
      transition: background 0.1s;
    }

    .note-item:hover {
      background: var(--maho-hover, #333);
    }

    .note-item.active {
      background: color-mix(in srgb, var(--maho-accent, #5B9CF6) 15%, transparent);
    }

    .note-title {
      font-size: 14px;
      font-weight: 500;
      margin-bottom: 4px;
    }

    .note-preview {
      font-size: 12px;
      color: var(--maho-text-secondary, #999);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .note-date {
      font-size: 11px;
      color: var(--maho-text-secondary, #666);
      margin-top: 4px;
    }

    .editor {
      flex: 1;
      display: flex;
      flex-direction: column;
      padding: 24px;
      gap: 12px;
    }

    .editor-title {
      font-size: 20px;
      font-weight: 600;
      background: none;
      border: none;
      color: var(--maho-text, #e0e0e0);
      outline: none;
      font-family: inherit;
    }

    .editor-url {
      font-size: 12px;
      color: var(--maho-accent, #5B9CF6);
    }

    .editor-content {
      flex: 1;
      background: none;
      border: 1px solid var(--maho-border, #333);
      border-radius: 8px;
      padding: 12px;
      color: var(--maho-text, #e0e0e0);
      font-size: 14px;
      font-family: inherit;
      resize: none;
      outline: none;
      line-height: 1.6;
    }

    .editor-content:focus {
      border-color: var(--maho-accent, #5B9CF6);
    }

    .editor-tags {
      display: flex;
      gap: 6px;
      flex-wrap: wrap;
    }

    .tag {
      font-size: 11px;
      background: var(--maho-surface, #2a2a2a);
      color: var(--maho-text-secondary, #999);
      padding: 2px 8px;
      border-radius: 4px;
    }

    .empty-state {
      flex: 1;
      display: flex;
      align-items: center;
      justify-content: center;
      color: var(--maho-text-secondary, #666);
    }
  `;

  @state() notes: Note[] = [];
  @state() activeNoteId: string = '';

  private handler = getMojoHandler<MahoNotesPageHandler>('MahoNotesPageHandler');

  override async connectedCallback() {
    super.connectedCallback();
    this.notes = await this.handler.getNotes();
    if (this.notes.length > 0) {
      this.activeNoteId = this.notes[0].id;
    }
  }

  private get activeNote(): Note | undefined {
    return this.notes.find(n => n.id === this.activeNoteId);
  }

  private formatDate(ts: number): string {
    return new Date(ts).toLocaleDateString(undefined, {month: 'short', day: 'numeric'});
  }

  private async onNewNote() {
    const note = await this.handler.createNote({title: 'Untitled Note', content: ''});
    this.notes = [note, ...this.notes];
    this.activeNoteId = note.id;
  }

  private async onTitleInput(noteId: string, e: Event) {
    const title = (e.target as HTMLInputElement).value;
    const updated = await this.handler.updateNote(noteId, {title});
    this.notes = this.notes.map(n => n.id === noteId ? updated : n);
  }

  private async onContentInput(noteId: string, e: Event) {
    const content = (e.target as HTMLTextAreaElement).value;
    const updated = await this.handler.updateNote(noteId, {content});
    this.notes = this.notes.map(n => n.id === noteId ? updated : n);
  }

  override render() {
    const active = this.activeNote;

    return html`
      <div class="note-list">
        <div class="list-header">
          <h2>Notes</h2>
          <button class="new-btn" @click=${this.onNewNote}>+ New</button>
        </div>
        <div class="items">
          ${this.notes.map(note => html`
            <div
              class="note-item ${note.id === this.activeNoteId ? 'active' : ''}"
              @click=${() => { this.activeNoteId = note.id; }}
            >
              <div class="note-title">${note.title}</div>
              <div class="note-preview">${note.content.slice(0, 60)}</div>
              <div class="note-date">${this.formatDate(note.updatedAt)}</div>
            </div>
          `)}
        </div>
      </div>

      ${active ? html`
        <div class="editor">
          <input class="editor-title" .value=${active.title}
            @input=${(e: Event) => this.onTitleInput(active.id, e)} />
          ${active.url ? html`<div class="editor-url">${active.url}</div>` : ''}
          <textarea class="editor-content" .value=${active.content}
            @input=${(e: Event) => this.onContentInput(active.id, e)}></textarea>
          <div class="editor-tags">
            ${active.tags.map(tag => html`<span class="tag">${tag}</span>`)}
          </div>
        </div>
      ` : html`
        <div class="empty-state">Select or create a note</div>
      `}
    `;
  }
}
