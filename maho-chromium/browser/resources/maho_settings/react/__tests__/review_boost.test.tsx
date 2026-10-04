import React, {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {useBoostController, type BoostController} from '../../../maho_boost/react/use-boost-controller';
import {createBoostUpdate, createColorBoostUpdate} from '../../../maho_boost/react/boost-state';

const harness = vi.hoisted(() => ({bridge: null as any, ready: null as any}));
vi.mock('../../../maho_boost/react/use-boost-bridge', async () => {
  const React = await import('react');
  return {useBoostBridge(refs: any, callbacks: any) {
    React.useEffect(() => {
      refs.bridgeRef.current = harness.bridge;
      harness.ready = callbacks.bootstrap(harness.bridge, 'example.com', []);
    }, []);
  }};
});
vi.mock('../../../maho_boost/react/use-close-lifecycle', () => ({useCloseLifecycle() {}}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it.each(['selectBoost', 'renameBoost', 'setSiteBoostEnabled'] as const)(
  '%s persists debounced edits before the transition', async action => {
    const boosts = ['A', 'B'].map(id => ({id, name: id, domain: 'example.com', color: {
      dotPos: {x: 0, y: 0}, secondaryDotPos: {x: 0, y: 0}, brightness: 1,
    }, typography: {}, zapSelectors: [], customCss: '', changeWasMade: false}));
    let active: string | null = 'A';
    const updateBoost = vi.fn(async (id, update) => {
      const boost = boosts.find(b => b.id === id)!;
      if (update.color?.brightness != null) boost.color.brightness = update.color.brightness;
      if (update.name != null) boost.name = update.name;
      return {boost};
    });
    harness.bridge = {pageHandler: {
      getActiveBoost: async () => ({boost: boosts.find(b => b.id === active) ?? null}),
      listBoosts: async () => ({boosts}),
      setHostCloseState: async () => {},
      setActiveBoost: async (id: string | null) => {active = id;},
      updateBoost,
    }};
    const container = document.createElement('div');
    const root = createRoot(container);
    let controller!: BoostController;
    function App() {controller = useBoostController(); return null;}
    try {
      await act(async () => root.render(<App />));
      await act(async () => harness.ready);
      await act(async () => {
        controller.applyUpdate('brightness', createBoostUpdate({color: createColorBoostUpdate({brightness: 1.5})}), 'slider');
        if (action === 'selectBoost') await controller.selectBoost('B');
        if (action === 'renameBoost') await controller.renameBoost('renamed');
        if (action === 'setSiteBoostEnabled') await controller.setSiteBoostEnabled(false);
      });
      expect(boosts[0].color.brightness).toBe(1.5);
    } finally {act(() => root.unmount());}
  });
