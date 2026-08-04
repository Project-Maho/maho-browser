import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../maho_space_create.mojom-webui.js';
import type {Page, PageHandler} from '../maho_space_create.mojom-webui.js';
import {
  DEFAULT_CANVAS_GEOMETRY,
  HARMONIES,
  HARMONY_MAX_DOTS,
  PRESETS,
  ZEN_PRESETS,
  clampBrightness,
  clampOpacity,
  clampToCircle,
  clampUnit,
  cloneState,
  createInitialState,
  getColorFromPosition,
  normalizeHue,
  normalizePresetIndex,
  parseThemeSelection,
  positionForHueAndDistance,
  recomputeSecondaryPositions,
  rgbToHsb,
  serializeThemeSelection,
  snapTexture,
  stopsForPreset,
} from './types.js';
import type {
  CanvasGeometry,
  ColorHarmony,
  ThemeMode,
  ThemePickerState,
  ThemeSelection,
  ThemeSelectionWire,
  ZenStop,
} from './types.js';

type Listener = () => void;

const PREVIEW_DEBOUNCE_MS = 33;

function notifyListeners(listeners: Set<Listener>): void {
  for (const listener of listeners) {
    listener();
  }
}

export interface ThemePickerStoreDeps {
  router: PageCallbackRouter | unknown;
  handler: PageHandler;
  scheduleTimeout?: (callback: () => void, ms: number) => number;
  clearTimeout?: (handle: number) => void;
}

export class ThemePickerStore {
  private readonly callbackRouter: PageCallbackRouter | unknown;
  private readonly listeners = new Set<Listener>();
  private readonly handler: PageHandler;
  private readonly scheduleTimeout: (callback: () => void, ms: number) => number;
  private readonly clearTimeoutFn: (handle: number) => void;
  private state: ThemePickerState = createInitialState();
  private previewTimer: number | null = null;

  constructor(deps?: ThemePickerStoreDeps) {
    if (deps) {
      this.callbackRouter = deps.router;
      this.handler = deps.handler;
      this.scheduleTimeout = deps.scheduleTimeout ??
          ((cb, ms) => window.setTimeout(cb, ms) as unknown as number);
      this.clearTimeoutFn = deps.clearTimeout ??
          ((id) => window.clearTimeout(id));
    } else {
      const router = new PageCallbackRouter();
      const handler = new PageHandlerRemote();
      const factory = PageHandlerFactory.getRemote();
      factory.createPageHandler(
          router.$.bindNewPipeAndPassRemote(),
          handler.$.bindNewPipeAndPassReceiver());
      this.callbackRouter = router;
      this.handler = handler as unknown as PageHandler;
      this.scheduleTimeout =
          (cb, ms) => window.setTimeout(cb, ms) as unknown as number;
      this.clearTimeoutFn = (id) => window.clearTimeout(id);
    }

    const router = this.callbackRouter as PageCallbackRouter;
    if (router && router.initialize && router.initialize.addListener) {
      router.initialize.addListener((initial: ThemeSelectionWire) => {
        this.loadInitial(initial.themeJson);
      });
    }
  }

  getSnapshot(): ThemePickerState {
    return this.state;
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  setMode(mode: ThemeMode): void {
    this.patch(draft => {
      draft.selection.mode = mode;
    });
  }

  setPreset(index: number): void {
    const normalizedIndex = normalizePresetIndex(index);
    this.patch(draft => {
      draft.selection.preset_index = normalizedIndex;
      draft.selection.stops = stopsForPreset(normalizedIndex);
    });
  }

  setOpacity(opacity: number): void {
    this.patch(draft => {
      draft.selection.opacity = clampOpacity(opacity);
    });
  }

  setGrain(grain: number): void {
    const texture = grain / 100;
    const snapped = snapTexture(texture);
    const grainSnapped = clampBrightness(snapped * 100);
    this.patch(draft => {
      draft.selection.grain = grainSnapped;
    });
  }

  setStopColor(index: number, hue: number, saturation: number): void {
    this.patch(draft => {
      const target = draft.selection.stops[index];
      if (!target) return;
      target.hue = normalizeHue(hue);
      target.saturation = clampUnit(saturation);
    });
  }

  setStopPosition(
      index: number, x: number, y: number, geom: CanvasGeometry): void {
    this.patch(draft => {
      const stops = draft.selection.stops;
      const target = stops[index];
      if (!target) return;

      const clamped = clampToCircle(x, y, geom);
      const color = getColorFromPosition(clamped.x, clamped.y, geom);

      target.position = clamped;
      target.hue = color.hue;
      target.saturation = color.saturation;
      target.lightness = color.lightness;

      if (target.isPrimary && draft.selection.harmony !== 'floating') {
        const secondaries = recomputeSecondaryPositions(
            target, draft.selection.harmony, geom);
        let si = 0;
        for (let i = 0; i < stops.length; i++) {
          if (!stops[i].isPrimary && si < secondaries.length) {
            const sec = secondaries[si];
            stops[i].position = sec.position;
            stops[i].hue = sec.hue;
            stops[i].saturation = sec.saturation;
            stops[i].lightness = sec.lightness;
            si++;
          }
        }
      }
    });
  }

  setHarmony(harmony: ColorHarmony): void {
    this.patch(draft => {
      draft.selection.harmony = harmony;
      const stops = draft.selection.stops;
      const primary = stops.find(s => s.isPrimary) ?? stops[0]!;

      if (harmony !== 'floating') {
        const geom = DEFAULT_CANVAS_GEOMETRY;
        const secondaries = recomputeSecondaryPositions(
            primary, harmony, geom);
        const maxDots = HARMONY_MAX_DOTS[harmony];

        const newStops: ZenStop[] = [
          {...primary, position: {...primary.position}},
        ];
        for (let i = 0; i < secondaries.length && newStops.length < maxDots; i++) {
          newStops.push({
            hue: secondaries[i].hue,
            saturation: secondaries[i].saturation,
            lightness: secondaries[i].lightness,
            position: secondaries[i].position,
            isPrimary: false,
          });
        }
        draft.selection.stops = newStops;
      }
    });
  }

  addStop(position?: {x: number; y: number}): void {
    this.patch(draft => {
      const sel = draft.selection;
      const maxDots = HARMONY_MAX_DOTS[sel.harmony];
      if (sel.stops.length >= maxDots) return;

      const geom = DEFAULT_CANVAS_GEOMETRY;
      let pos: {x: number; y: number};
      if (position) {
        pos = position;
      } else {
        const angleFraction = (sel.stops.length * 60) / 360;
        pos = positionForHueAndDistance(angleFraction, 0.7, geom);
      }

      const color = getColorFromPosition(pos.x, pos.y, geom);
      sel.stops.push({
        hue: color.hue,
        saturation: color.saturation,
        lightness: color.lightness,
        position: pos,
        isPrimary: sel.stops.length === 0,
      });
    });
  }

  removeStop(index: number): void {
    this.patch(draft => {
      const stops = draft.selection.stops;
      if (stops.length === 0) return;
      if (stops.length > 1 && stops[index]?.isPrimary) return;
      stops.splice(index, 1);
    });
  }

  applyPreset(presetIndex: number): void {
    const idx = ((presetIndex % ZEN_PRESETS.length) + ZEN_PRESETS.length) %
        ZEN_PRESETS.length;
    const preset = ZEN_PRESETS[idx];
    const geom = DEFAULT_CANVAS_GEOMETRY;

    const hsb = rgbToHsb(
        preset.colors[0].r, preset.colors[0].g, preset.colors[0].b);

    const primary: ZenStop = {
      hue: hsb.h,
      saturation: hsb.s,
      lightness: preset.lightness,
      position: {...preset.primaryPosition},
      isPrimary: true,
    };

    const secondaries = recomputeSecondaryPositions(
        primary, preset.algorithm, geom);
    const stops: ZenStop[] = [primary];
    for (let i = 0; i < preset.numDots - 1 && i < secondaries.length; i++) {
      stops.push({
        hue: secondaries[i].hue,
        saturation: secondaries[i].saturation,
        lightness: secondaries[i].lightness,
        position: secondaries[i].position,
        isPrimary: false,
      });
    }

    this.patch(draft => {
      draft.selection.harmony = preset.algorithm;
      draft.selection.preset_index = idx;
      draft.selection.stops = stops;
    });
  }

  loadInitial(json: string): void {
    const initialSelection = parseThemeSelection(json);
    this.patch(draft => {
      draft.selection = initialSelection;
      draft.loading = false;
    }, {emitPreview: false});
  }

  previewNextPreset(): void {
    this.setPreset(this.state.selection.preset_index + 1);
  }

  async commitTheme(): Promise<void> {
    this.clearPreviewTimer();
    await this.handler.commitTheme({themeJson: this.serializeSelection()});
  }

  async cancelTheme(): Promise<void> {
    this.clearPreviewTimer();
    await this.handler.cancelTheme();
  }

  private serializeSelection(): string {
    return serializeThemeSelection(this.state.selection);
  }

  private patch(
      mutator: (draft: ThemePickerState) => void,
      options: {emitPreview?: boolean} = {}): void {
    const draft = cloneState(this.state);
    mutator(draft);
    this.state = draft;
    notifyListeners(this.listeners);
    if (options.emitPreview !== false) {
      this.schedulePreview();
    }
  }

  private schedulePreview(): void {
    this.clearPreviewTimer();
    this.previewTimer = this.scheduleTimeout(() => {
      const payload = this.serializeSelection();
      void this.handler.previewTheme({themeJson: payload});
      this.previewTimer = null;
    }, PREVIEW_DEBOUNCE_MS);
  }

  private clearPreviewTimer(): void {
    if (this.previewTimer !== null) {
      this.clearTimeoutFn(this.previewTimer);
      this.previewTimer = null;
    }
  }
}

export function getDialLevel(selection: ThemeSelection): number {
  return Math.max(0, Math.min(8, Math.round(selection.grain / 12.5)));
}
