// Seed colours: hue=0.58 (blue), s=0.80, b=0.90, computed via hsbToRgb.
// Canvas 380×380, padding 20, radius=170, position offset at 0.7×radius (119px).
// primary  h=0.58  angle=208.8°  → RGB [46,141,230]  pos (86,133)
// second1  h≈0.441 angle=158.8°  → RGB [46,230,165]  pos (79,233)
// second2  h≈0.719 angle=258.8°  → RGB [103,46,230]  pos (167,73)
const SPARKLE_DEFAULT = JSON.stringify({
  type: 'gradient',
  gradientColors: [
    {
      c: [46, 141, 230],
      position: {x: 86, y: 133},
      hue: 0.58, saturation: 0.80, brightness: 0.90,
      isCustom: false, isPrimary: true,
      algorithm: 'analogous', lightness: 85,
    },
    {
      c: [46, 230, 165],
      position: {x: 79, y: 233},
      hue: 0.441, saturation: 0.80, brightness: 0.90,
      isCustom: false, isPrimary: false,
    },
    {
      c: [103, 46, 230],
      position: {x: 167, y: 73},
      hue: 0.719, saturation: 0.80, brightness: 0.90,
      isCustom: false, isPrimary: false,
    },
  ],
  harmony: 'analogous',
  opacity: 0.5,
  texture: 0,
});

class FakeRouter {
  constructor() {
    this._listeners = {initialize: []};
    this.$ = {
      bindNewPipeAndPassRemote: () => ({}),
    };
    this.initialize = {
      addListener: (listener) => {
        this._listeners.initialize.push(listener);
        queueMicrotask(() => listener({themeJson: SPARKLE_DEFAULT}));
      },
    };
  }
}

class FakeRemote {
  constructor() {
    this.$ = {
      bindNewPipeAndPassReceiver: () => ({}),
    };
  }
  previewTheme(selection) {
    console.info('[standalone stub] previewTheme', selection);
    return Promise.resolve();
  }
  commitTheme(selection) {
    console.info('[standalone stub] commitTheme', selection);
    window.alert('commitTheme: ' + selection.themeJson);
    return Promise.resolve();
  }
  cancelTheme() {
    console.info('[standalone stub] cancelTheme');
    window.alert('cancelTheme');
    return Promise.resolve();
  }
}

class FakeFactory {
  static getRemote() { return new FakeFactory(); }
  createPageHandler(page, handler) {
    console.info('[standalone stub] createPageHandler');
  }
}

export const PageCallbackRouter = FakeRouter;
export const PageHandlerRemote = FakeRemote;
export const PageHandlerFactory = FakeFactory;
