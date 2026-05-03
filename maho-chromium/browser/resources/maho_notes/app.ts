import {PageHandlerFactory, PageHandlerRemote, Note} from './maho_notes.mojom-webui.js';
import {PageCallbackRouter} from './maho_notes.mojom-webui.js';

let pageHandler: PageHandlerRemote;
let callbackRouter: PageCallbackRouter;
let selectedNoteId: string | null = null;

function initialize() {
  callbackRouter = new PageCallbackRouter();
  pageHandler = new PageHandlerRemote();

  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      pageHandler.$.bindNewPipeAndPassReceiver());

  callbackRouter.notesChanged.addListener((notes: Note[]) => {
    renderNotes(notes);
  });

  pageHandler.getNotes().then(({notes}: {notes: Note[]}) => {
    renderNotes(notes);
  });
}

function renderNotes(notes: Note[]) {
  const container = document.querySelector('maho-notes-app');
  if (!container) return;
  container.innerHTML = '';
  (container as HTMLElement).style.cssText = 'display: flex; height: 100vh;';

  const list = document.createElement('div');
  list.style.cssText = 'width: 280px; border-right: 1px solid rgba(255,255,255,0.1); overflow-y: auto; flex-shrink: 0;';

  const listHeader = document.createElement('div');
  listHeader.style.cssText = 'padding: 16px; display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid rgba(255,255,255,0.1);';
  listHeader.innerHTML = '<h2 style="margin:0; font-size:16px; color:#e94560;">Notes</h2>';

  const addBtn = document.createElement('button');
  addBtn.textContent = '+';
  addBtn.style.cssText = 'background: #e94560; border: none; color: white; width: 28px; height: 28px; border-radius: 6px; cursor: pointer; font-size: 16px;';
  addBtn.addEventListener('click', () => {
    pageHandler.createNote('New Note', '', '').then(() => {
      pageHandler.getNotes().then(({notes: n}: {notes: Note[]}) => renderNotes(n));
    });
  });
  listHeader.appendChild(addBtn);
  list.appendChild(listHeader);

  for (const note of notes) {
    const item = document.createElement('div');
    item.style.cssText = `padding: 12px 16px; cursor: pointer; border-bottom: 1px solid rgba(255,255,255,0.05); background: ${note.id === selectedNoteId ? 'rgba(233,69,96,0.1)' : 'transparent'};`;
    item.innerHTML = `<div style="font-size:13px; font-weight:500;">${escapeHtml(note.title)}</div><div style="font-size:11px; color:#888; margin-top:4px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;">${escapeHtml(note.content)}</div>`;
    item.addEventListener('click', () => {
      selectedNoteId = note.id;
      renderNotes(notes);
    });
    list.appendChild(item);
  }

  const editor = document.createElement('div');
  editor.style.cssText = 'flex: 1; display: flex; flex-direction: column; padding: 16px;';

  const selected = notes.find(n => n.id === selectedNoteId) || notes[0] || null;
  if (selected) {
    if (!selectedNoteId) selectedNoteId = selected.id;

    const titleInput = document.createElement('input');
    titleInput.value = selected.title;
    titleInput.style.cssText = 'background: transparent; border: none; color: #e0e0e0; font-size: 20px; font-weight: 600; margin-bottom: 8px; outline: none;';
    titleInput.addEventListener('change', () => {
      pageHandler.updateNote(selected.id, titleInput.value, contentArea.value);
    });

    const urlLabel = document.createElement('div');
    urlLabel.textContent = selected.url;
    urlLabel.style.cssText = 'font-size: 11px; color: #888; margin-bottom: 12px;';

    const contentArea = document.createElement('textarea');
    contentArea.value = selected.content;
    contentArea.style.cssText = 'flex: 1; background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.1); border-radius: 8px; color: #e0e0e0; padding: 12px; font-size: 14px; font-family: inherit; resize: none; outline: none;';
    contentArea.addEventListener('change', () => {
      pageHandler.updateNote(selected.id, titleInput.value, contentArea.value);
    });

    editor.appendChild(titleInput);
    editor.appendChild(urlLabel);
    editor.appendChild(contentArea);
  } else {
    editor.innerHTML = '<div style="display:flex; align-items:center; justify-content:center; height:100%; color:#888;">Select a note or create a new one</div>';
  }

  container.appendChild(list);
  container.appendChild(editor);
}

function escapeHtml(text: string): string {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

document.addEventListener('DOMContentLoaded', initialize);
