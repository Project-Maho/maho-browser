import type {Note} from '../maho_notes.mojom-webui.js';

export interface NotesState {
  readonly draftContent: string;
  readonly draftTitle: string;
  readonly errorMessage: string | null;
  readonly isCreating: boolean;
  readonly isSaving: boolean;
  readonly notes: readonly Note[];
  readonly selectedNoteId: string | null;
  readonly statusMessage: string | null;
}

export function createInitialState(): NotesState {
  return {
    draftContent: '',
    draftTitle: '',
    errorMessage: null,
    isCreating: false,
    isSaving: false,
    notes: [],
    selectedNoteId: null,
    statusMessage: null,
  };
}

export function cloneState(state: NotesState): NotesState {
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
