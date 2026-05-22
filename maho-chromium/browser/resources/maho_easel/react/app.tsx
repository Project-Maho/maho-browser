import React, {useCallback, useEffect, useRef, useSyncExternalStore} from 'react';
import {createRoot} from 'react-dom/client';

import type {CanvasItem} from '../maho_easel.mojom-webui.js';
import {MahoEaselStore} from './store.js';

function EaselApp({store}: {store: MahoEaselStore}) {
  const snapshot = useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());

  useEffect(() => {
    void store.bootstrap();
  }, [store]);

  return (
    <div style={{display: 'flex', flexDirection: 'column', height: '100vh'}}>
      <Toolbar isAdding={snapshot.isAdding} onAddText={() => void store.addTextItem()} />
      <CanvasArea items={snapshot.items} store={store} />
      {snapshot.errorMessage ? (
        <div style={{
          position: 'fixed',
          bottom: '16px',
          right: '16px',
          padding: '8px 14px',
          background: 'rgba(233,69,96,0.9)',
          borderRadius: '6px',
          color: 'white',
          fontSize: '13px',
        }}>
          {snapshot.errorMessage}
        </div>
      ) : null}
    </div>
  );
}

function Toolbar({isAdding, onAddText}: {isAdding: boolean; onAddText: () => void}) {
  return (
    <div style={{
      padding: '12px 24px',
      borderBottom: '1px solid rgba(255,255,255,0.1)',
      display: 'flex',
      alignItems: 'center',
      gap: '12px',
    }}>
      <h2 style={{margin: 0, color: '#e94560', fontSize: '18px'}}>Easel</h2>
      <button
        disabled={isAdding}
        onClick={onAddText}
        style={{
          padding: '6px 14px',
          background: '#e94560',
          border: 'none',
          borderRadius: '6px',
          color: 'white',
          cursor: isAdding ? 'default' : 'pointer',
          fontSize: '13px',
          opacity: isAdding ? 0.6 : 1,
        }}>
        + Text
      </button>
    </div>
  );
}

function CanvasArea({items, store}: {items: readonly CanvasItem[]; store: MahoEaselStore}) {
  return (
    <div style={{flex: 1, position: 'relative', overflow: 'hidden'}}>
      {items.map(item => (
        <DraggableItem key={item.id} item={item} store={store} />
      ))}
    </div>
  );
}

function DraggableItem({item, store}: {item: CanvasItem; store: MahoEaselStore}) {
  const dragging = useRef(false);
  const startPos = useRef({x: 0, y: 0});
  const origPos = useRef({x: item.x, y: item.y});
  const elRef = useRef<HTMLDivElement>(null);

  const onMouseDown = useCallback((e: React.MouseEvent) => {
    dragging.current = true;
    startPos.current = {x: e.clientX, y: e.clientY};
    origPos.current = {x: item.x, y: item.y};
    e.preventDefault();
  }, [item.x, item.y]);

  useEffect(() => {
    const onMouseMove = (e: MouseEvent) => {
      if (!dragging.current || !elRef.current) return;
      const newX = origPos.current.x + (e.clientX - startPos.current.x);
      const newY = origPos.current.y + (e.clientY - startPos.current.y);
      elRef.current.style.left = newX + 'px';
      elRef.current.style.top = newY + 'px';
    };

    const onMouseUp = (e: MouseEvent) => {
      if (!dragging.current) return;
      dragging.current = false;
      const newX = origPos.current.x + (e.clientX - startPos.current.x);
      const newY = origPos.current.y + (e.clientY - startPos.current.y);
      store.moveItem(item.id, newX, newY);
    };

    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseup', onMouseUp);
    return () => {
      document.removeEventListener('mousemove', onMouseMove);
      document.removeEventListener('mouseup', onMouseUp);
    };
  }, [item.id, store]);

  return (
    <div
      ref={elRef}
      onMouseDown={onMouseDown}
      style={{
        position: 'absolute',
        left: item.x + 'px',
        top: item.y + 'px',
        width: item.width + 'px',
        minHeight: item.height + 'px',
        background: 'rgba(255,255,255,0.08)',
        border: '1px solid rgba(255,255,255,0.15)',
        borderRadius: '8px',
        padding: '8px 12px',
        cursor: 'move',
        fontSize: '13px',
        userSelect: 'none',
      }}>
      {item.content}
    </div>
  );
}

const rootElement = document.getElementById('app');

if (!rootElement) {
  throw new Error('Missing #app root for maho_easel.');
}

const store = new MahoEaselStore();
createRoot(rootElement).render(
    <React.StrictMode>
      <EaselApp store={store} />
    </React.StrictMode>);
