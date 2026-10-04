// Copyright 2026 Maho Browser. All rights reserved.

import {
  SizeMode,
  WindowMode,
} from '../maho_boost.mojom-webui.js';
import type {
  BoostInfo,
  BoostUpdate,
  ColorBoost,
  ColorBoostUpdate,
  Point,
  TypographyBoost,
  TypographyBoostUpdate,
} from '../maho_boost.mojom-webui.js';

export interface BoostState {
  activeBoostId: string | null;
  boost: BoostInfo | null;
  boosts: BoostInfo[];
  domain: string | null;
  error: string | null;
  isLoading: boolean;
  mode: WindowMode;
  pickerModeEnabled: boolean;
  selectedBoostId: string | null;
  systemFonts: string[];
  zapMode: {anyZapped: boolean; isOn: boolean};
}

export interface BoostSelectionRow {
  readonly id: string;
  readonly name: string;
}

export interface BoostSelectionProps {
  readonly activeBoostId: string | null;
  readonly boosts: readonly BoostSelectionRow[];
  readonly onSelectBoost: (boostId: string) => void;
  readonly onSiteBoostEnabledChange: (enabled: boolean) => void;
  readonly selectedBoostId: string | null;
}

export const initialBoostState: BoostState = {
  activeBoostId: null,
  boost: null,
  boosts: [],
  domain: null,
  error: null,
  isLoading: true,
  mode: WindowMode.kBoost,
  pickerModeEnabled: false,
  selectedBoostId: null,
  systemFonts: [],
  zapMode: {anyZapped: false, isOn: false},
};

export function clonePoint(point: Point): Point {
  return {...point};
}

export function cloneColorBoost(color: ColorBoost): ColorBoost {
  return {
    ...color,
    dotPos: clonePoint(color.dotPos),
    secondaryDotPos: clonePoint(color.secondaryDotPos),
  };
}

export function cloneTypographyBoost(typography: TypographyBoost): TypographyBoost {
  return {...typography};
}

export function cloneBoostInfo(boost: BoostInfo): BoostInfo {
  return {
    ...boost,
    color: cloneColorBoost(boost.color),
    typography: cloneTypographyBoost(boost.typography),
    zapSelectors: [...boost.zapSelectors],
  };
}

export function cloneBoostInfos(boosts: readonly BoostInfo[]): BoostInfo[] {
  return boosts.map(cloneBoostInfo);
}

function asciiCaseFold(value: string): string {
  let folded = '';
  for (const character of value) {
    const code = character.charCodeAt(0);
    folded += code >= 65 && code <= 90 ? String.fromCharCode(code + 32) : character;
  }
  return folded;
}

export function sortBoostInfos(
  boosts: readonly BoostInfo[],
  selectedBoostId: string | null,
): BoostInfo[] {
  const uniqueBoosts = new Map<string, BoostInfo>();
  for (const boost of boosts) {
    if (!uniqueBoosts.has(boost.id)) {
      uniqueBoosts.set(boost.id, cloneBoostInfo(boost));
    }
  }
  return [...uniqueBoosts.values()].sort((left, right) => {
    const leftSelected = left.id === selectedBoostId;
    const rightSelected = right.id === selectedBoostId;
    if (leftSelected !== rightSelected) {
      return leftSelected ? -1 : 1;
    }
    const leftName = asciiCaseFold(left.name);
    const rightName = asciiCaseFold(right.name);
    if (leftName !== rightName) {
      return leftName < rightName ? -1 : 1;
    }
    if (left.id === right.id) {
      return 0;
    }
    return left.id < right.id ? -1 : 1;
  });
}

export function createColorBoostUpdate(
  update: Partial<ColorBoostUpdate>,
): ColorBoostUpdate {
  return {
    colorBoostEnabled: null,
    dotAngleDeg: null,
    secondaryDotAngleDegDelta: null,
    brightness: null,
    saturation: null,
    contrast: null,
    magicTheme: null,
    smartInvert: null,
    dotPos: null,
    dotDistance: null,
    secondaryDotPos: null,
    ...update,
  };
}

export function createTypographyBoostUpdate(
  update: Partial<TypographyBoostUpdate>,
): TypographyBoostUpdate {
  return {
    caseMode: null,
    fontFamily: '',
    setFontFamily: false,
    setSizeMode: false,
    sizeMode: SizeMode.k100,
    ...update,
  };
}

export function createBoostUpdate(update: Partial<BoostUpdate>): BoostUpdate {
  return {
    name: null,
    color: null,
    typography: null,
    zapSelectors: null,
    customCss: null,
    ...update,
  };
}

export function patchBoostInfo(boost: BoostInfo, update: BoostUpdate): BoostInfo {
  const color = patchColorBoost(boost.color, update.color);
  const typography = patchTypographyBoost(boost.typography, update.typography);

  return {
    ...cloneBoostInfo(boost),
    changeWasMade: true,
    name: update.name === null || update.name === undefined ? boost.name : update.name,
    color,
    typography,
    zapSelectors: update.zapSelectors === null || update.zapSelectors === undefined ?
      [...boost.zapSelectors] : [...update.zapSelectors],
    customCss: update.customCss === null || update.customCss === undefined ?
      boost.customCss : update.customCss,
  };
}

function patchColorBoost(color: ColorBoost, update: ColorBoostUpdate | null | undefined): ColorBoost {
  if (update === null || update === undefined) {
    return cloneColorBoost(color);
  }

  return {
    colorBoostEnabled: update.colorBoostEnabled ?? color.colorBoostEnabled,
    dotAngleDeg: update.dotAngleDeg ?? color.dotAngleDeg,
    secondaryDotAngleDegDelta:
      update.secondaryDotAngleDegDelta ?? color.secondaryDotAngleDegDelta,
    brightness: update.brightness ?? color.brightness,
    saturation: update.saturation ?? color.saturation,
    contrast: update.contrast ?? color.contrast,
    magicTheme: update.magicTheme ?? color.magicTheme,
    smartInvert: update.smartInvert ?? color.smartInvert,
    dotPos: update.dotPos === null || update.dotPos === undefined ?
      clonePoint(color.dotPos) : clonePoint(update.dotPos),
    dotDistance: update.dotDistance ?? color.dotDistance,
    secondaryDotPos: update.secondaryDotPos === null || update.secondaryDotPos === undefined ?
      clonePoint(color.secondaryDotPos) : clonePoint(update.secondaryDotPos),
  };
}

function patchTypographyBoost(
  typography: TypographyBoost,
  update: TypographyBoostUpdate | null | undefined,
): TypographyBoost {
  if (update === null || update === undefined) {
    return cloneTypographyBoost(typography);
  }

  return {
    fontFamily: update.setFontFamily ? update.fontFamily : typography.fontFamily,
    caseMode: update.caseMode ?? typography.caseMode,
    sizeMode: update.setSizeMode ? update.sizeMode : typography.sizeMode,
  };
}

export type BoostAction =
  | {type: 'bootstrap'; activeBoostId: string | null; domain: string; boost: BoostInfo | null; boosts: BoostInfo[]; selectedBoostId: string | null; systemFonts: string[]}
  | {type: 'replace-domain-state'; activeBoostId: string | null; boost: BoostInfo | null; boosts: BoostInfo[]; selectedBoostId: string | null}
  | {type: 'set-boost'; boost: BoostInfo | null; preserveDirty: boolean}
  | {type: 'set-boosts'; boosts: BoostInfo[]}
  | {type: 'apply-update'; update: BoostUpdate}
  | {type: 'set-active-boost-id'; boostId: string | null}
  | {type: 'set-loading'; isLoading: boolean}
  | {type: 'set-error'; error: string | null}
  | {type: 'set-mode'; mode: WindowMode}
  | {type: 'set-zap-mode'; isOn: boolean; anyZapped: boolean}
  | {type: 'set-picker-mode'; isOn: boolean};

export function boostReducer(state: BoostState, action: BoostAction): BoostState {
  switch (action.type) {
    case 'bootstrap':
      return {
        ...state,
        activeBoostId: action.activeBoostId,
        boost: action.boost === null ? null : cloneBoostInfo(action.boost),
        boosts: sortBoostInfos(action.boosts, action.selectedBoostId),
        domain: action.domain,
        error: null,
        isLoading: false,
        selectedBoostId: action.selectedBoostId,
        systemFonts: [...action.systemFonts],
      };
    case 'replace-domain-state':
      return {
        ...state,
        activeBoostId: action.activeBoostId,
        boost: action.boost === null ? null : cloneBoostInfo(action.boost),
        boosts: sortBoostInfos(action.boosts, action.selectedBoostId),
        selectedBoostId: action.selectedBoostId,
      };
    case 'set-boost': {
      const boost = action.boost === null ? null : cloneBoostInfo(action.boost);
      const preserveDirty = action.preserveDirty && state.boost?.changeWasMade === true;
      const selectedBoostId = boost?.id ?? null;
      const boosts = boost === null ? state.boosts : state.boosts.some(item => item.id === boost.id) ?
        state.boosts.map(item => item.id === boost.id ? boost : item) : [...state.boosts, boost];
      return {
        ...state,
        boost: boost === null ? null : {...boost, changeWasMade: preserveDirty || boost.changeWasMade},
        boosts: sortBoostInfos(boosts, selectedBoostId),
        selectedBoostId,
      };
    }
    case 'set-boosts':
      return {...state, boosts: sortBoostInfos(action.boosts, state.selectedBoostId)};
    case 'apply-update': {
      if (state.boost === null) {
        return state;
      }
      const boost = patchBoostInfo(state.boost, action.update);
      return {
        ...state,
        boost,
        boosts: sortBoostInfos(
            state.boosts.map(item => item.id === boost.id ? boost : item),
            state.selectedBoostId),
      };
    }
    case 'set-active-boost-id':
      return {...state, activeBoostId: action.boostId};
    case 'set-loading':
      return {...state, isLoading: action.isLoading};
    case 'set-error':
      return {...state, error: action.error};
    case 'set-mode':
      return {...state, mode: action.mode};
    case 'set-zap-mode':
      return {...state, zapMode: {anyZapped: action.anyZapped, isOn: action.isOn}};
    case 'set-picker-mode':
      return {...state, pickerModeEnabled: action.isOn};
    default:
      return assertNever(action);
  }
}

function assertNever(value: never): never {
  throw new Error(`Unhandled Boost action: ${String(value)}`);
}
