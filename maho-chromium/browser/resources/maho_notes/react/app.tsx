import React, {useEffect, useMemo, useSyncExternalStore} from 'react';
import {createRoot} from 'react-dom/client';

import type {Note} from '../maho_notes.mojom-webui.js';
import {hsbToCss} from '//resources/maho_common/maho_theme_utils.js';
import type {SpaceColor} from '//resources/maho_common/maho_theme_utils.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {Badge, Button, Field, TextArea, TextInput, TooltipButton, TooltipProvider} from './primitives.js';
import {MahoNotesStore} from './store.js';

// G1 gate: value-level import proves esbuild externalizes //resources/maho_common/*.
export {hsbToCss};
export type {SpaceColor};

// G2 gate: value-level import proves esbuild externalizes chrome://resources/js/*.
// loadTimeData is used only for its .data property type (harmless read that never
// affects UI; the notes surface does not have any loadTimeData keys registered).
export {loadTimeData};

function NotesApp({store}: {store: MahoNotesStore}) {
  const snapshot = useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());

  const selectedNote = useMemo(() => {
    return snapshot.notes.find(note => note.id === snapshot.selectedNoteId) ?? null;
  }, [snapshot.notes, snapshot.selectedNoteId]);

  useEffect(() => {
    void store.bootstrap();
  }, [store]);

  return (
    <div className="notes-shell">
      <aside className="notes-sidebar">
        <header className="notes-sidebar__header">
          <div className="notes-sidebar__copy">
            <span className="notes-sidebar__eyebrow">First React tranche</span>
            <h1 className="notes-sidebar__title">Notes</h1>
            <p className="notes-sidebar__description">
              Chromium WebUI, Mojo-backed, and styled with shadcn-style primitives
              that leave a clean path for fuller component adoption later.
            </p>
          </div>
          <div className="notes-toolbar">
            <Button prefix="＋" variant="primary" onClick={() => void store.createNote()} disabled={snapshot.isCreating}>
              New note
            </Button>
          </div>
        </header>
        <div className="notes-scroll">
          <div className="notes-list" role="list" aria-label="Notes list">
            {snapshot.notes.map(note => (
              <NoteCard
                key={note.id}
                isSelected={note.id === snapshot.selectedNoteId}
                note={note}
                onSelect={() => store.selectNote(note.id)}
              />
            ))}
          </div>
        </div>
      </aside>
      <main className="notes-editor">
        {selectedNote ? (
          <section className="notes-editor__panel">
            <div className="notes-editor__hero">
              <span className="notes-editor__eyebrow">Editor</span>
              <h2 className="notes-editor__title">{selectedNote.title || 'Untitled note'}</h2>
              <p className="notes-editor__subtitle">
                Draft edits locally, then sync them through the existing Mojo page handler.
              </p>
            </div>
            <div className="notes-editor__meta">
              <Badge accent>{formatDate(selectedNote.updated_at)}</Badge>
              <p className="notes-editor__url">{selectedNote.url || 'No source URL for this note yet.'}</p>
            </div>
            <div className="notes-form">
              <Field label="Title">
                <TextInput
                  value={snapshot.draftTitle}
                  onChange={event => store.updateDraftTitle(event.currentTarget.value)}
                  placeholder="Name this note"
                />
              </Field>
              <Field label="Content">
                <TextArea
                  value={snapshot.draftContent}
                  onChange={event => store.updateDraftContent(event.currentTarget.value)}
                  placeholder="Capture the useful part before it disappears."
                />
              </Field>
              <div className="notes-form__actions">
                <span className="notes-status">{snapshot.errorMessage ?? snapshot.statusMessage ?? 'Ready.'}</span>
                <div className="notes-toolbar">
                  <TooltipButton
                    tooltip="Delete selected note"
                    variant="danger"
                    onClick={() => void store.deleteSelectedNote()}
                    disabled={snapshot.isSaving}>
                    Delete
                  </TooltipButton>
                  <TooltipButton
                    tooltip="Save selected note"
                    variant="primary"
                    onClick={() => void store.saveSelectedNote()}
                    disabled={snapshot.isSaving}>
                    {snapshot.isSaving ? 'Saving…' : 'Save changes'}
                  </TooltipButton>
                </div>
              </div>
            </div>
          </section>
        ) : (
          <section className="notes-empty">
            <div className="notes-empty__panel">
              <div className="notes-empty__orb">✦</div>
              <span className="notes-empty__eyebrow">Ready for capture</span>
              <h2 className="notes-empty__title">Start your first note</h2>
              <p className="notes-empty__description">
                This first tranche keeps the existing backend contract intact while proving a
                React-based WebUI surface and a reusable component seam for later shadcn work.
              </p>
              <div className="notes-empty__actions">
                <Button prefix="＋" variant="primary" onClick={() => void store.createNote()} disabled={snapshot.isCreating}>
                  Create note
                </Button>
              </div>
            </div>
          </section>
        )}
      </main>
    </div>
  );
}

function NoteCard(
    {isSelected, note, onSelect}: {isSelected: boolean; note: Note; onSelect: () => void}) {
  return (
    <button
      type="button"
      className={isSelected ? 'notes-card is-selected' : 'notes-card'}
      onClick={onSelect}>
      <div className="notes-card__header">
        <Badge accent={isSelected}>{formatDate(note.updated_at)}</Badge>
        {note.url ? <span className="notes-meta">Linked</span> : <span className="notes-meta">Scratchpad</span>}
      </div>
      <div className="notes-card__body">
        <h3 className="notes-card__title">{note.title || 'Untitled note'}</h3>
        <p className="notes-card__excerpt">{note.content || 'No content yet — open it and start writing.'}</p>
      </div>
    </button>
  );
}

function formatDate(timestamp: number | null | undefined): string {
  if (!timestamp) {
    return 'Just now';
  }

  const milliseconds = timestamp > 1_000_000_000_000 ? timestamp : timestamp * 1000;
  const date = new Date(milliseconds);
  return new Intl.DateTimeFormat(undefined, {
    day: 'numeric',
    month: 'short',
  }).format(date);
}

const rootElement = document.getElementById('app');

if (!rootElement) {
  throw new Error('Missing #app root for maho_notes.');
}

const store = new MahoNotesStore();
createRoot(rootElement).render(
    <React.StrictMode>
      <TooltipProvider delayDuration={200}>
        <NotesApp store={store} />
      </TooltipProvider>
    </React.StrictMode>);
