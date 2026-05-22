export interface Note {
  id: string;
  title: string;
  content: string;
  url: string;
  created_at: number;
  updated_at: number;
}

export class PageCallbackRouter {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
  notesChanged: {
    addListener(listener: (notes: Note[]) => void): void;
  };
}

export class PageHandlerRemote {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  getNotes(): Promise<{notes: Note[]}>;
  createNote(title: string, content: string, url: string): Promise<{note: Note}>;
  updateNote(id: string, title: string, content: string): Promise<{success: boolean}>;
  deleteNote(id: string): Promise<{success: boolean}>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
