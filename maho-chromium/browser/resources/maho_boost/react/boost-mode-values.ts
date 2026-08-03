import {
  CaseMode,
  SizeMode,
} from '../maho_boost.mojom-webui.js';
import type {BoostCaseMode} from './components/typography-section.js';
import type {Point} from '../maho_boost.mojom-webui.js';

export const COMMON_FONTS = [
  'Arial',
  'Times New Roman',
  'Courier New',
  'Georgia',
  'Comic Sans MS',
  'Verdana',
  'Trebuchet MS',
  'Impact',
  'Palatino Linotype',
  'Tahoma',
  'Helvetica',
  'Garamond',
  'Century Gothic',
  'Arial Black',
  'Papyrus',
] as const;

const CASE_SEQUENCE: readonly CaseMode[] = [
  CaseMode.kNone,
  CaseMode.kUpper,
  CaseMode.kLower,
  CaseMode.kCapitalize,
];

const SIZE_SEQUENCE: readonly SizeMode[] = [
  SizeMode.k100,
  SizeMode.k110,
  SizeMode.k125,
  SizeMode.k150,
  SizeMode.k90,
];

export function caseModeName(mode: CaseMode): BoostCaseMode {
  switch (mode) {
    case CaseMode.kUpper:
      return 'uppercase';
    case CaseMode.kLower:
      return 'lowercase';
    case CaseMode.kCapitalize:
      return 'capitalize';
    case CaseMode.kNone:
    default:
      return 'none';
  }
}

export function nextCaseMode(mode: CaseMode): CaseMode {
  const index = CASE_SEQUENCE.indexOf(mode);
  return CASE_SEQUENCE[(index + 1) % CASE_SEQUENCE.length] ?? CaseMode.kNone;
}

export function nextSizeMode(mode: SizeMode): SizeMode {
  const index = SIZE_SEQUENCE.indexOf(mode);
  return SIZE_SEQUENCE[(index + 1) % SIZE_SEQUENCE.length] ?? SizeMode.k100;
}

export function sizeModePercent(mode: SizeMode): 90 | 100 | 110 | 125 | 150 {
  switch (mode) {
    case SizeMode.k90:
      return 90;
    case SizeMode.k110:
      return 110;
    case SizeMode.k125:
      return 125;
    case SizeMode.k150:
      return 150;
    case SizeMode.k100:
    default:
      return 100;
  }
}

export function pointForColor(hue: number, distance: number): Point {
  const radians = (hue - 100) * Math.PI / 180;
  const radius = Math.max(0, Math.min(1, distance)) / 2;
  return {
    x: 0.5 + Math.cos(radians) * radius,
    y: 0.5 + Math.sin(radians) * radius,
  };
}
