import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../maho_easel.mojom-webui.js';
import type {CanvasItem} from '../maho_easel.mojom-webui.js';
import {asErrorMessage, notifyListeners} from '//resources/maho_common/react/store_utils.js';
import type {Listener} from '//resources/maho_common/react/store_utils.js';
import {cloneState, createInitialState} from './types.js';
import type {EaselState} from './types.js';

interface MutableEaselState {
  items: CanvasItem[];
  isAdding: boolean;
  errorMessage: string | null;
}

function makeMutableState(state: EaselState): MutableEaselState {
  return {
    items: state.items.map(item => ({...item})),
    isAdding: state.isAdding,
    errorMessage: state.errorMessage,
  };
}

function freezeMutableState(state: MutableEaselState): EaselState {
  return {
    items: state.items.map(item => ({...item})),
    isAdding: state.isAdding,
    errorMessage: state.errorMessage,
  };
}

export class MahoEaselStore {
  private readonly callbackRouter = new PageCallbackRouter();
  private readonly listeners = new Set<Listener>();
  private readonly pageHandler = new PageHandlerRemote();
  private state: EaselState = createInitialState();

  constructor() {
    const factory = PageHandlerFactory.getRemote();
    factory.createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        this.pageHandler.$.bindNewPipeAndPassReceiver());

    this.callbackRouter.canvasChanged.addListener((newItems: CanvasItem[]) => {
      this.patch(draft => {
        draft.items = newItems.map(item => ({...item}));
      });
    });
  }

  getSnapshot(): EaselState {
    return cloneState(this.state);
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  async bootstrap(): Promise<void> {
    try {
      const {items} = await this.pageHandler.getCanvas();
      this.patch(draft => {
        draft.items = items.map(item => ({...item}));
        draft.errorMessage = null;
      });
    } catch (error: unknown) {
      this.patch(draft => {
        draft.errorMessage = asErrorMessage(error, 'Failed to load canvas.');
      });
    }
  }

  async addTextItem(): Promise<void> {
    this.patch(draft => {
      draft.isAdding = true;
      draft.errorMessage = null;
    });

    try {
      const x = 50 + Math.random() * 400;
      const y = 50 + Math.random() * 300;
      const {item} = await this.pageHandler.addItem('text', x, y, 'New text item');
      this.patch(draft => {
        draft.items.push({...item});
      });
    } catch (error: unknown) {
      this.patch(draft => {
        draft.errorMessage = asErrorMessage(error, 'Failed to add item.');
      });
    } finally {
      this.patch(draft => {
        draft.isAdding = false;
      });
    }
  }

  moveItem(id: string, x: number, y: number): void {
    this.patch(draft => {
      const item = draft.items.find(i => i.id === id);
      if (item) {
        item.x = x;
        item.y = y;
      }
    });
    void this.pageHandler.moveItem(id, x, y);
  }

  private notify(): void {
    notifyListeners(this.listeners);
  }

  private patch(mutator: (draft: MutableEaselState) => void): void {
    const draft = makeMutableState(this.state);
    mutator(draft);
    this.state = freezeMutableState(draft);
    this.notify();
  }
}

