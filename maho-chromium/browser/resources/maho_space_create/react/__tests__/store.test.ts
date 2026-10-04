import {beforeEach, afterEach, describe, expect, it, vi} from 'vitest';
import {ThemePickerStore, getDialLevel} from '../store.js';
import {
  DEFAULT_CANVAS_GEOMETRY,
  HARMONY_MAX_DOTS,
  createDefaultSelection,
} from '../types.js';
import type {ThemeSelectionWire, CanvasGeometry} from '../types.js';

interface FakeRouter {
  initialize: {
    addListener: (cb: (w: ThemeSelectionWire) => void) => void;
  };
  fire: (wire: ThemeSelectionWire) => void;
}

function createFakeHandler() {
  return {
    previewTheme: vi.fn().mockResolvedValue(undefined),
    commitTheme: vi.fn().mockResolvedValue(undefined),
    cancelTheme: vi.fn().mockResolvedValue(undefined),
  };
}

function createFakeRouter(): FakeRouter {
  const listeners: Array<(w: ThemeSelectionWire) => void> = [];
  return {
    initialize: {
      addListener: (cb) => {
        listeners.push(cb);
      },
    },
    fire: (wire: ThemeSelectionWire) => {
      for (const listener of listeners) {
        listener(wire);
      }
    },
  };
}

function createStore(opts?: {fireInitial?: ThemeSelectionWire}) {
  const handler = createFakeHandler();
  const router = createFakeRouter();
  const store = new ThemePickerStore({
    handler: handler as unknown as Parameters<typeof ThemePickerStore>[0] extends
        infer T ? T extends {handler: infer H} ? H : never : never,
    router,
    scheduleTimeout: (cb, ms) => setTimeout(cb, ms) as unknown as number,
    clearTimeout: (id) => clearTimeout(id),
  });
  if (opts?.fireInitial) {
    router.fire(opts.fireInitial);
  }
  return {store, handler, router};
}

const GEOM: CanvasGeometry = DEFAULT_CANVAS_GEOMETRY;

describe('ThemePickerStore — debounced preview', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it('coalesces multiple mutations within debounce window into a single preview call', () => {
    const {store, handler} = createStore();
    store.setPreset(3);
    store.setGrain(60);
    store.setMode('moon');
    expect(handler.previewTheme).not.toHaveBeenCalled();
    vi.advanceTimersByTime(50);
    expect(handler.previewTheme).toHaveBeenCalledTimes(1);
  });

  it('preview payload uses gradient wire shape (default sparkle mode)', () => {
    const {store, handler} = createStore();
    store.setPreset(7);
    store.setGrain(50);
    vi.advanceTimersByTime(50);
    const arg = handler.previewTheme.mock.calls.at(-1)![0] as ThemeSelectionWire;
    const parsed = JSON.parse(arg.themeJson);
    expect(parsed.type).toBe('gradient');
    expect(Array.isArray(parsed.gradientColors)).toBe(true);
    expect(parsed.gradientColors.length).toBeGreaterThanOrEqual(1);
    expect(parsed.gradientColors[0].isPrimary).toBe(true);
  });
});

describe('ThemePickerStore — cancel race', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it('clears pending preview timer before resolving cancel', async () => {
    const {store, handler} = createStore();
    store.setGrain(60);
    expect(handler.previewTheme).not.toHaveBeenCalled();
    await store.cancelTheme();
    vi.advanceTimersByTime(200);
    expect(handler.previewTheme).not.toHaveBeenCalled();
    expect(handler.cancelTheme).toHaveBeenCalledTimes(1);
  });
});

describe('ThemePickerStore — initialization', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it('starts in loading=true with default selection', () => {
    const {store} = createStore();
    expect(store.getSnapshot().loading).toBe(true);
    expect(store.getSnapshot().selection).toEqual(createDefaultSelection());
  });

  it('Initialize callback transitions to loading=false without preview emit', () => {
    const wire: ThemeSelectionWire = {
      themeJson: JSON.stringify({
        type: 'solid',
        color: {hue: 0.58, saturation: 0.80, brightness: 0.50, grain: 0},
      }),
    };
    const {store, handler} = createStore({fireInitial: wire});
    expect(store.getSnapshot().loading).toBe(false);
    expect(store.getSnapshot().selection.preset_index).toBe(7);
    vi.advanceTimersByTime(100);
    expect(handler.previewTheme).not.toHaveBeenCalled();
  });

  it('Initialize with empty themeJson falls back to default state but loading=false', () => {
    const {store} = createStore({fireInitial: {themeJson: ''}});
    expect(store.getSnapshot().loading).toBe(false);
    expect(store.getSnapshot().selection).toEqual(createDefaultSelection());
  });
});

describe('ThemePickerStore — commit', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it('commitTheme sends current selection in gradient wire shape', async () => {
    const {store, handler} = createStore();
    store.setPreset(5);
    store.setGrain(70);
    await store.commitTheme();
    expect(handler.commitTheme).toHaveBeenCalledTimes(1);
    const arg = handler.commitTheme.mock.calls[0]![0] as ThemeSelectionWire;
    const parsed = JSON.parse(arg.themeJson);
    expect(parsed.type).toBe('gradient');
    expect(parsed.gradientColors[0].isPrimary).toBe(true);
  });

  it('clears pending preview timer before committing', async () => {
    const {store, handler} = createStore();
    store.setGrain(70);

    await store.commitTheme();
    vi.advanceTimersByTime(200);

    expect(handler.commitTheme).toHaveBeenCalledTimes(1);
    expect(handler.previewTheme).not.toHaveBeenCalled();
  });

  it('commitTheme adds scheme=light when mode is sun', async () => {
    const {store, handler} = createStore();
    store.setPreset(5);
    store.setMode('sun');
    await store.commitTheme();
    const arg = handler.commitTheme.mock.calls[0]![0] as ThemeSelectionWire;
    const parsed = JSON.parse(arg.themeJson);
    expect(parsed.type).toBe('gradient');
    expect(parsed.scheme).toBe('light');
  });
});

describe('ThemePickerStore — subscribers', () => {
  it('notifies subscribers on every state mutation', () => {
    const {store} = createStore();
    const spy = vi.fn();
    store.subscribe(spy);
    store.setPreset(2);
    store.setGrain(20);
    expect(spy).toHaveBeenCalledTimes(2);
  });

  it('unsubscribe stops notifications', () => {
    const {store} = createStore();
    const spy = vi.fn();
    const unsubscribe = store.subscribe(spy);
    unsubscribe();
    store.setPreset(2);
    expect(spy).not.toHaveBeenCalled();
  });

  it('getSnapshot returns the same reference between mutations', () => {
    const {store} = createStore();
    const a = store.getSnapshot();
    const b = store.getSnapshot();
    expect(a).toBe(b);
  });

  it('getSnapshot returns a new reference after a mutation', () => {
    const {store} = createStore();
    const before = store.getSnapshot();
    store.setGrain(40);
    const after = store.getSnapshot();
    expect(after).not.toBe(before);
  });
});

describe('getDialLevel', () => {
  it('maps grain 0 to level 0', () => {
    const sel = createDefaultSelection();
    sel.grain = 0;
    expect(getDialLevel(sel)).toBe(0);
  });
  it('maps grain 50 to level 4', () => {
    const sel = createDefaultSelection();
    sel.grain = 50;
    expect(getDialLevel(sel)).toBe(4);
  });
  it('maps grain 100 to level 8', () => {
    const sel = createDefaultSelection();
    sel.grain = 100;
    expect(getDialLevel(sel)).toBe(8);
  });
  it('clamps level to [0,8]', () => {
    const high = createDefaultSelection();
    high.grain = 200;
    expect(getDialLevel(high)).toBe(8);
    const low = createDefaultSelection();
    low.grain = -50;
    expect(getDialLevel(low)).toBe(0);
  });
});

describe('ThemePickerStore — setStopPosition', () => {
  beforeEach(() => { vi.useFakeTimers(); });
  afterEach(() => { vi.useRealTimers(); });

  it('updates only the target stop', () => {
    const {store} = createStore();
    const before = store.getSnapshot().selection.stops.map(s => ({...s, position: {...s.position}}));
    store.setStopPosition(0, GEOM.width / 2 + 50, GEOM.height / 2, GEOM);
    const after = store.getSnapshot().selection.stops;
    expect(after[0].position.x).not.toBe(before[0].position.x);
  });

  it('primary drag in analogous mode triggers secondary recomputation', () => {
    const {store} = createStore();
    const beforeSecondary = {...store.getSnapshot().selection.stops[1].position};
    store.setStopPosition(0, GEOM.width / 2 + 30, GEOM.height / 2 + 30, GEOM);
    const afterSecondary = store.getSnapshot().selection.stops[1].position;
    expect(afterSecondary.x).not.toBeCloseTo(beforeSecondary.x, 0);
  });

  it('primary drag in floating mode does NOT recompute secondaries', () => {
    const {store} = createStore();
    store.setHarmony('floating');
    const beforeSecondary = {...store.getSnapshot().selection.stops[1].position};
    store.setStopPosition(0, GEOM.width / 2 + 30, GEOM.height / 2 + 30, GEOM);
    const afterSecondary = store.getSnapshot().selection.stops[1].position;
    expect(afterSecondary.x).toBeCloseTo(beforeSecondary.x, 5);
    expect(afterSecondary.y).toBeCloseTo(beforeSecondary.y, 5);
  });

  it('clamps to circle for out-of-bounds coordinates', () => {
    const {store} = createStore();
    store.setStopPosition(0, 9999, 9999, GEOM);
    const pos = store.getSnapshot().selection.stops[0].position;
    const cx = GEOM.width / 2;
    const cy = GEOM.height / 2;
    const radius = Math.min(cx, cy) - GEOM.padding;
    const dist = Math.sqrt((pos.x - cx) ** 2 + (pos.y - cy) ** 2);
    expect(dist).toBeCloseTo(radius, 5);
  });
});

describe('ThemePickerStore — addStop / removeStop', () => {
  beforeEach(() => { vi.useFakeTimers(); });
  afterEach(() => { vi.useRealTimers(); });

  it('addStop enforces harmony max (analogous=3)', () => {
    const {store} = createStore();
    expect(store.getSnapshot().selection.stops).toHaveLength(3);
    store.addStop();
    expect(store.getSnapshot().selection.stops).toHaveLength(3);
  });

  it('addStop works in floating mode up to 8 stops', () => {
    const {store} = createStore();
    store.setHarmony('floating');
    const initial = store.getSnapshot().selection.stops.length;
    for (let i = 0; i < 10; i++) {
      store.addStop();
    }
    expect(store.getSnapshot().selection.stops.length).toBeLessThanOrEqual(8);
    expect(store.getSnapshot().selection.stops.length).toBeGreaterThan(initial);
  });

  it('removeStop never removes primary', () => {
    const {store} = createStore();
    const primaryIdx = store.getSnapshot().selection.stops.findIndex(s => s.isPrimary);
    const before = store.getSnapshot().selection.stops.length;
    store.removeStop(primaryIdx);
    expect(store.getSnapshot().selection.stops.length).toBe(before);
    expect(store.getSnapshot().selection.stops.some(s => s.isPrimary)).toBe(true);
  });

  it('removeStop minimum 1 stop', () => {
    const {store} = createStore();
    store.setHarmony('floating');
    while (store.getSnapshot().selection.stops.length > 1) {
      const stops = store.getSnapshot().selection.stops;
      const nonPrimary = stops.findIndex(s => !s.isPrimary);
      if (nonPrimary === -1) break;
      store.removeStop(nonPrimary);
    }
    const remaining = store.getSnapshot().selection.stops.length;
    store.removeStop(0);
    expect(store.getSnapshot().selection.stops.length).toBe(remaining);
  });
});

describe('ThemePickerStore — setHarmony', () => {
  beforeEach(() => { vi.useFakeTimers(); });
  afterEach(() => { vi.useRealTimers(); });

  it('triggers position recompute when switching harmonies', () => {
    const {store} = createStore();
    const beforePos = {...store.getSnapshot().selection.stops[1].position};
    store.setHarmony('triadic');
    const afterPos = store.getSnapshot().selection.stops[1].position;
    expect(afterPos.x).not.toBeCloseTo(beforePos.x, 0);
  });

  it('floating preserves stop positions', () => {
    const {store} = createStore();
    const beforeStops = store.getSnapshot().selection.stops.map(
        s => ({...s, position: {...s.position}}));
    store.setHarmony('floating');
    const afterStops = store.getSnapshot().selection.stops;
    for (let i = 0; i < beforeStops.length; i++) {
      expect(afterStops[i].position.x).toBeCloseTo(beforeStops[i].position.x, 5);
      expect(afterStops[i].position.y).toBeCloseTo(beforeStops[i].position.y, 5);
    }
  });
});

describe('ThemePickerStore — setOpacity', () => {
  it('clamps to [0.25, 0.9]', () => {
    const {store} = createStore();
    store.setOpacity(0.1);
    expect(store.getSnapshot().selection.opacity).toBe(0.25);
    store.setOpacity(0.99);
    expect(store.getSnapshot().selection.opacity).toBe(0.9);
    store.setOpacity(0.5);
    expect(store.getSnapshot().selection.opacity).toBe(0.5);
  });
});

describe('ThemePickerStore — setGrain snapping', () => {
  it('snaps 47 to 50 via 16-step quantization', () => {
    const {store} = createStore();
    store.setGrain(47);
    expect(store.getSnapshot().selection.grain).toBe(50);
  });
});

describe('ThemePickerStore — applyPreset', () => {
  beforeEach(() => { vi.useFakeTimers(); });
  afterEach(() => { vi.useRealTimers(); });

  it('sets harmony + stops + lightness atomically', () => {
    const {store} = createStore();
    store.applyPreset(0);
    const sel = store.getSnapshot().selection;
    expect(sel.harmony).toBe('analogous');
    expect(sel.stops.length).toBeGreaterThanOrEqual(1);
    expect(sel.stops[0].isPrimary).toBe(true);
    expect(sel.stops[0].lightness).toBe(85);
    expect(sel.preset_index).toBe(0);
  });
});

describe('ThemePickerStore — preview payload', () => {
  beforeEach(() => { vi.useFakeTimers(); });
  afterEach(() => { vi.useRealTimers(); });

  it('has type gradient after mutation', () => {
    const {store, handler} = createStore();
    store.setGrain(25);
    vi.advanceTimersByTime(50);
    const arg = handler.previewTheme.mock.calls.at(-1)![0] as ThemeSelectionWire;
    const parsed = JSON.parse(arg.themeJson);
    expect(parsed.type).toBe('gradient');
  });

  it('has c arrays present per stop', async () => {
    const {store, handler} = createStore();
    await store.commitTheme();
    const arg = handler.commitTheme.mock.calls[0]![0] as ThemeSelectionWire;
    const parsed = JSON.parse(arg.themeJson);
    for (const stop of parsed.gradientColors) {
      expect(Array.isArray(stop.c)).toBe(true);
      expect(stop.c).toHaveLength(3);
    }
  });
});
