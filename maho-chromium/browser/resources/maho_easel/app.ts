import {PageHandlerFactory, PageHandlerRemote, CanvasItem} from './maho_easel.mojom-webui.js';
import {PageCallbackRouter} from './maho_easel.mojom-webui.js';

let pageHandler: PageHandlerRemote;
let callbackRouter: PageCallbackRouter;
let items: CanvasItem[] = [];

function initialize() {
  callbackRouter = new PageCallbackRouter();
  pageHandler = new PageHandlerRemote();

  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      pageHandler.$.bindNewPipeAndPassReceiver());

  callbackRouter.canvasChanged.addListener((newItems: CanvasItem[]) => {
    items = newItems;
    renderCanvas();
  });

  pageHandler.getCanvas().then(({items: i}: {items: CanvasItem[]}) => {
    items = i;
    renderCanvas();
  });
}

function renderCanvas() {
  const container = document.querySelector('maho-easel-app');
  if (!container) return;
  container.innerHTML = '';
  (container as HTMLElement).style.cssText = 'display: flex; flex-direction: column; height: 100vh;';

  const toolbar = document.createElement('div');
  toolbar.style.cssText = 'padding: 12px 24px; border-bottom: 1px solid rgba(255,255,255,0.1); display: flex; align-items: center; gap: 12px;';
  toolbar.innerHTML = '<h2 style="margin:0; color:#e94560; font-size:18px;">Easel</h2>';

  const addTextBtn = document.createElement('button');
  addTextBtn.textContent = '+ Text';
  addTextBtn.style.cssText = 'padding: 6px 14px; background: #e94560; border: none; border-radius: 6px; color: white; cursor: pointer; font-size: 13px;';
  addTextBtn.addEventListener('click', () => {
    const x = 50 + Math.random() * 400;
    const y = 50 + Math.random() * 300;
    pageHandler.addItem('text', x, y, 'New text item').then(({item}: {item: CanvasItem}) => {
      items.push(item);
      renderCanvas();
    });
  });
  toolbar.appendChild(addTextBtn);
  container.appendChild(toolbar);

  const canvas = document.createElement('div');
  canvas.style.cssText = 'flex: 1; position: relative; overflow: hidden;';

  for (const item of items) {
    const el = document.createElement('div');
    el.style.cssText = `position: absolute; left: ${item.x}px; top: ${item.y}px; width: ${item.width}px; min-height: ${item.height}px; background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.15); border-radius: 8px; padding: 8px 12px; cursor: move; font-size: 13px; user-select: none;`;
    el.textContent = item.content;

    let dragging = false;
    let startX = 0;
    let startY = 0;
    let origX = item.x;
    let origY = item.y;

    el.addEventListener('mousedown', (e: MouseEvent) => {
      dragging = true;
      startX = e.clientX;
      startY = e.clientY;
      origX = item.x;
      origY = item.y;
      e.preventDefault();
    });

    const onMouseMove = (e: MouseEvent) => {
      if (!dragging) return;
      item.x = origX + (e.clientX - startX);
      item.y = origY + (e.clientY - startY);
      el.style.left = item.x + 'px';
      el.style.top = item.y + 'px';
    };

    const onMouseUp = () => {
      if (!dragging) return;
      dragging = false;
      pageHandler.moveItem(item.id, item.x, item.y);
    };

    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseup', onMouseUp);

    canvas.appendChild(el);
  }

  container.appendChild(canvas);
}

document.addEventListener('DOMContentLoaded', initialize);
