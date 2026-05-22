export interface CanvasItem {
  id: string;
  kind: string;
  x: number;
  y: number;
  width: number;
  height: number;
  content: string;
}

export class PageCallbackRouter {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
  canvasChanged: {
    addListener(listener: (items: CanvasItem[]) => void): void;
  };
}

export class PageHandlerRemote {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  getCanvas(): Promise<{items: CanvasItem[]}>;
  addItem(kind: string, x: number, y: number, content: string): Promise<{item: CanvasItem}>;
  moveItem(id: string, x: number, y: number): Promise<void>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
