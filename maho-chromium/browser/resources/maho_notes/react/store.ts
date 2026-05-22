import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../maho_notes.mojom-webui.js';
import type {Note} from '../maho_notes.mojom-webui.js';
import {asErrorMessage, notifyListeners} from '//resources/maho_common/react/store_utils.js';
import type {Listener} from '//resources/maho_common/react/store_utils.js';
import {cloneState, createInitialState} from './types.js';
import type {NotesState} from './types.js';

export class MahoNotesStore {
  private readonly callbackRouter = new PageCallbackRouter();
  private readonly listeners = new Set<Listener>();
  private readonly pageHandler = new PageHandlerRemote();
  private state: NotesState = createInitialState();

  constructor() {
    const factory = PageHandlerFactory.getRemote();
    factory.createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        this.pageHandler.$.bindNewPipeAndPassReceiver());

    this.callbackRouter.notesChanged.addListener((notes: Note[]) => {
      this.patch(draft => {
        draft.notes = notes.map(note => ({...note}));
        draft.selectedNoteId = resolveSelection(draft.notes, draft.selectedNoteId);
        syncDraftFromSelection(draft);
        draft.statusMessage = `Updated ${draft.notes.length} note${draft.notes.length === 1 ? '' : 's'}.`;
      });
    });
  }

  getSnapshot(): NotesState {
    return cloneState(this.state);
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  async bootstrap(): Promise<void> {
    this.patch(draft => {
      draft.errorMessage = null;
      draft.statusMessage = 'Loading notes…';
    });

    try {
      const {notes} = await this.pageHandler.getNotes();
      this.patch(draft => {
        draft.notes = notes.map(note => ({...note}));
        draft.selectedNoteId = resolveSelection(draft.notes, draft.selectedNoteId);
        syncDraftFromSelection(draft);
        draft.statusMessage = draft.notes.length > 0 ?
            `Loaded ${draft.notes.length} note${draft.notes.length === 1 ? '' : 's'}.` :
            'No notes yet.';
      });
    } catch (error: unknown) {
      this.patch(draft => {
        draft.errorMessage = asErrorMessage(error, 'Failed to load notes.');
        draft.statusMessage = null;
      });
    }
  }

  selectNote(noteId: string): void {
    this.patch(draft => {
      if (draft.selectedNoteId === noteId) {
        return;
      }
      draft.selectedNoteId = noteId;
      syncDraftFromSelection(draft);
      draft.errorMessage = null;
      draft.statusMessage = 'Note ready to edit.';
    });
  }

  updateDraftTitle(title: string): void {
    this.patch(draft => {
      draft.draftTitle = title;
      draft.statusMessage = null;
    });
  }

  updateDraftContent(content: string): void {
    this.patch(draft => {
      draft.draftContent = content;
      draft.statusMessage = null;
    });
  }

  async createNote(): Promise<void> {
    this.patch(draft => {
      draft.errorMessage = null;
      draft.isCreating = true;
      draft.statusMessage = 'Creating note…';
    });

    try {
      const {note} = await this.pageHandler.createNote('New Note', '', '');
      this.patch(draft => {
        const existing = draft.notes.filter(existingNote => existingNote.id !== note.id);
        draft.notes = [
          {...note},
          ...existing,
        ];
        draft.selectedNoteId = note.id;
        syncDraftFromSelection(draft);
        draft.statusMessage = 'Created a new note.';
      });
    } catch (error: unknown) {
      this.patch(draft => {
        draft.errorMessage = asErrorMessage(error, 'Failed to create a note.');
        draft.statusMessage = null;
      });
    } finally {
      this.patch(draft => {
        draft.isCreating = false;
      });
    }
  }

  async saveSelectedNote(): Promise<void> {
    const selected = this.getSelectedNote(this.state);
    if (!selected) {
      return;
    }

    this.patch(draft => {
      draft.errorMessage = null;
      draft.isSaving = true;
      draft.statusMessage = 'Saving note…';
    });

    try {
      const success = await this.pageHandler.updateNote(
          selected.id, this.state.draftTitle, this.state.draftContent);
      this.patch(draft => {
        if (success.success) {
          draft.notes = draft.notes.map(note => {
            if (note.id !== selected.id) {
              return note;
            }
            return {
              ...note,
              content: draft.draftContent,
              title: draft.draftTitle,
              updated_at: Date.now(),
            };
          });
          draft.statusMessage = 'Saved changes.';
        } else {
          draft.errorMessage = 'The browser rejected the note update.';
          draft.statusMessage = null;
        }
      });
    } catch (error: unknown) {
      this.patch(draft => {
        draft.errorMessage = asErrorMessage(error, 'Failed to save the selected note.');
        draft.statusMessage = null;
      });
    } finally {
      this.patch(draft => {
        draft.isSaving = false;
      });
    }
  }

  async deleteSelectedNote(): Promise<void> {
    const selected = this.getSelectedNote(this.state);
    if (!selected) {
      return;
    }

    this.patch(draft => {
      draft.errorMessage = null;
      draft.isSaving = true;
      draft.statusMessage = 'Deleting note…';
    });

    try {
      const success = await this.pageHandler.deleteNote(selected.id);
      this.patch(draft => {
        if (success.success) {
          draft.notes = draft.notes.filter(note => note.id !== selected.id);
          draft.selectedNoteId = resolveSelection(draft.notes, null);
          syncDraftFromSelection(draft);
          draft.statusMessage = draft.notes.length > 0 ? 'Deleted note.' : 'Deleted the last note.';
        } else {
          draft.errorMessage = 'The browser rejected the delete request.';
          draft.statusMessage = null;
        }
      });
    } catch (error: unknown) {
      this.patch(draft => {
        draft.errorMessage = asErrorMessage(error, 'Failed to delete the selected note.');
        draft.statusMessage = null;
      });
    } finally {
      this.patch(draft => {
        draft.isSaving = false;
      });
    }
  }

  private getSelectedNote(state: NotesState): Note | null {
    return state.notes.find(note => note.id === state.selectedNoteId) ?? null;
  }

  private notify(): void {
    notifyListeners(this.listeners);
  }

  private patch(mutator: (draft: MutableNotesState) => void): void {
    const draft = makeMutableState(this.state);
    mutator(draft);
    this.state = freezeMutableState(draft);
    this.notify();
  }
}

interface MutableNotesState {
  draftContent: string;
  draftTitle: string;
  errorMessage: string | null;
  isCreating: boolean;
  isSaving: boolean;
  notes: Note[];
  selectedNoteId: string | null;
  statusMessage: string | null;
}

function makeMutableState(state: NotesState): MutableNotesState {
  return {
    draftContent: state.draftContent,
    draftTitle: state.draftTitle,
    errorMessage: state.errorMessage,
    isCreating: state.isCreating,
    isSaving: state.isSaving,
    notes: state.notes.map(note => ({...note})),
    selectedNoteId: state.selectedNoteId,
    statusMessage: state.statusMessage,
  };
}

function freezeMutableState(state: MutableNotesState): NotesState {
  return {
    draftContent: state.draftContent,
    draftTitle: state.draftTitle,
    errorMessage: state.errorMessage,
    isCreating: state.isCreating,
    isSaving: state.isSaving,
    notes: state.notes.map(note => ({...note})),
    selectedNoteId: state.selectedNoteId,
    statusMessage: state.statusMessage,
  };
}

function resolveSelection(notes: readonly Note[], preferredId: string | null): string | null {
  if (preferredId && notes.some(note => note.id === preferredId)) {
    return preferredId;
  }
  return notes[0]?.id ?? null;
}

function syncDraftFromSelection(state: MutableNotesState): void {
  const selected = state.notes.find(note => note.id === state.selectedNoteId) ?? null;
  state.draftTitle = selected?.title ?? '';
  state.draftContent = selected?.content ?? '';
}


