export type ScenePhase = 'establish' | 'zoom-in' | 'action' | 'hold' | 'zoom-out' | 'rest';

export type RuntimeSubstate =
  | 'idle'
  | 'waiting-ready'
  | 'handoff'
  | 'zoom-in'
  | 'action'
  | 'hold'
  | 'zoom-out'
  | 'suspended';

export type PlaybackMode = 'autoplay' | 'manual' | 'paused' | 'frozen';

export type RunningPlaybackMode = Exclude<PlaybackMode, 'paused' | 'frozen'>;

export interface PlaybackState {
  readonly mode: PlaybackMode;
  readonly resumeMode: RunningPlaybackMode;
  readonly substate: RuntimeSubstate;
  readonly resumeSubstate: RuntimeSubstate;
  readonly scene: import('./timeline').SceneKey;
}

export type PlaybackEvent =
  | { readonly type: 'ready' }
  | { readonly type: 'select'; readonly scene: import('./timeline').SceneKey }
  | { readonly type: 'pause' }
  | { readonly type: 'resume' }
  | { readonly type: 'replay' }
  | { readonly type: 'suspend' }
  | { readonly type: 'restore' }
  | { readonly type: 'freeze' }
  | { readonly type: 'segment-finished'; readonly segment: RuntimeSubstate };

export function initialPlaybackState(mode: PlaybackMode): PlaybackState {
  const runningMode = mode === 'manual' ? 'manual' : 'autoplay';
  return {
    mode,
    resumeMode: runningMode,
    substate: mode === 'frozen' ? 'idle' : 'waiting-ready',
    resumeSubstate: 'waiting-ready',
    scene: 'spaces',
  };
}

function assertNever(value: never): never {
  throw new TypeError(`Unhandled playback event: ${String(value)}`);
}

export function transitionPlaybackState(state: PlaybackState, event: PlaybackEvent): PlaybackState {
  switch (event.type) {
    case 'ready':
      return state.mode === 'frozen' ? state : { ...state, substate: 'handoff' };
    case 'select':
      return { ...state, mode: 'manual', resumeMode: 'manual', scene: event.scene, substate: 'waiting-ready' };
    case 'pause':
    case 'suspend':
      return state.mode === 'frozen'
        ? state
        : { ...state, mode: 'paused', resumeMode: state.mode === 'paused' ? state.resumeMode : state.mode, resumeSubstate: state.substate, substate: 'suspended' };
    case 'resume':
      if (state.substate === 'hold') {
        return { ...state, mode: 'autoplay', resumeMode: 'autoplay', substate: 'zoom-out' };
      }
      return state.mode === 'paused'
        ? { ...state, mode: state.resumeMode, substate: state.resumeSubstate }
        : state;
    case 'restore':
      return state.mode === 'paused'
        ? { ...state, mode: state.resumeMode, substate: state.resumeSubstate }
        : state;
    case 'replay':
      return { ...state, mode: 'autoplay', resumeMode: 'autoplay', scene: 'spaces', substate: 'waiting-ready' };
    case 'freeze':
      return { ...state, mode: 'frozen', substate: 'idle', resumeSubstate: 'idle' };
    case 'segment-finished':
      switch (event.segment) {
        case 'handoff': return { ...state, substate: 'zoom-in' };
        case 'zoom-in': return { ...state, substate: 'action' };
        case 'action': return { ...state, substate: 'hold' };
        case 'zoom-out': return { ...state, substate: 'idle' };
        case 'idle':
        case 'waiting-ready':
        case 'hold':
        case 'suspended': return state;
        default: return assertNever(event.segment);
      }
    default:
      return assertNever(event);
  }
}

export type TransformOwner =
  | 'responsive-shell'
  | 'camera-stage'
  | 'scene-action'
  | 'pointer-drag'
  | 'text-track';

export const LANDING_INTERSECTION_THRESHOLDS = [0, 0.1, 0.25, 0.35, 0.4, 0.5, 0.75, 1] as const;

export interface MotionOwnerInput {
  readonly activeOwner: 'hero' | 'showcase' | null;
  readonly heroRatio: number;
  readonly showcaseRatio: number;
  readonly heroManual: boolean;
  readonly showcaseManual: boolean;
}

export function selectMotionOwner(input: MotionOwnerInput): 'hero' | 'showcase' | null {
  const heroThreshold = input.activeOwner === 'hero' ? (input.heroManual ? 0.1 : 0.25) : (input.heroManual ? 0.1 : 0.35);
  const showcaseThreshold = input.activeOwner === 'showcase' ? (input.showcaseManual ? 0.1 : 0.25) : (input.showcaseManual ? 0.1 : 0.4);
  const heroEligible = input.heroRatio >= heroThreshold;
  const showcaseEligible = input.showcaseRatio >= showcaseThreshold;
  if (input.heroManual && heroEligible) return 'hero';
  if (input.showcaseManual && showcaseEligible) return 'showcase';
  if (heroEligible && !showcaseEligible) return 'hero';
  if (showcaseEligible && !heroEligible) return 'showcase';
  if (!heroEligible || !showcaseEligible) return null;
  if (input.activeOwner === 'hero') return input.showcaseRatio >= input.heroRatio + 0.1 ? 'showcase' : 'hero';
  if (input.activeOwner === 'showcase') return input.heroRatio >= input.showcaseRatio + 0.1 ? 'hero' : 'showcase';
  return input.heroRatio >= input.showcaseRatio ? 'hero' : 'showcase';
}

export function substateForSegment(segment: string): RuntimeSubstate {
  switch (segment) {
    case 'handoff': return 'handoff';
    case 'zoom-in':
    case 'hero-intro': return 'zoom-in';
    case 'action': return 'action';
    case 'zoom-out': return 'zoom-out';
    default: return 'idle';
  }
}

export interface CameraTarget {
  readonly focusX: number; // 0..1 percentage of width
  readonly focusY: number; // 0..1 percentage of height
  readonly maxScale: number;
}

export const HERO_CAMERA_TARGET: CameraTarget = {
  focusX: 0.63,
  focusY: 0.46,
  maxScale: 1.06,
};

export const CAMERA_TARGETS = {
  hero: HERO_CAMERA_TARGET,
  spaces: { focusX: 0.18, focusY: 0.58, maxScale: 1.16 },
  'split-view': { focusX: 0.59, focusY: 0.50, maxScale: 1.06 },
  'command-bar': { focusX: 0.59, focusY: 0.51, maxScale: 1.05 },
  agent: { focusX: 0.82, focusY: 0.54, maxScale: 1.06 },
} as const satisfies Record<string, CameraTarget>;

export const HERO_PHASE_WINDOWS = {
  establish: [0.0, 0.14],
  'zoom-in': [0.14, 0.32],
  hold: [0.32, 0.68],
  'zoom-out': [0.68, 0.84],
  rest: [0.84, 1.0],
} as const;

export const SHOWCASE_PHASE_WINDOWS = {
  establish: [0.0, 0.12],
  'zoom-in': [0.12, 0.28],
  action: [0.28, 0.72],
  hold: [0.72, 0.84],
  'zoom-out': [0.84, 1.0],
} as const;

export const HANDOFF_TIMING_MS = 600;
export const HANDOFF_SUBWINDOWS = {
  outgoingSettleMs: 180,
  incomingEstablishMs: 420,
} as const;

export const EASING_FAMILIES = {
  zoomIn: 'cubic-bezier(0.22, 1, 0.36, 1)',
  zoomOut: 'cubic-bezier(0.4, 0, 0.2, 1)',
  contentTransition: 'cubic-bezier(0.22, 1, 0.36, 1)',
} as const;

export interface TrajectoryPoint {
  readonly x: number;
  readonly y: number;
  readonly opacity: number;
}

export function sampleQuadraticTrajectory(
  start: { x: number; y: number },
  control: { x: number; y: number },
  end: { x: number; y: number },
  sampleCount: number = 24
): TrajectoryPoint[] {
  const count = Math.max(24, sampleCount);
  const points: TrajectoryPoint[] = [];

  for (let i = 0; i <= count; i++) {
    const t = i / count;
    const oneMinusT = 1 - t;

    const x =
      oneMinusT * oneMinusT * start.x +
      2 * oneMinusT * t * control.x +
      t * t * end.x;
    const y =
      oneMinusT * oneMinusT * start.y +
      2 * oneMinusT * t * control.y +
      t * t * end.y;
    const opacity = Math.sin(t * Math.PI); // Smooth fade in and out

    points.push({
      x: Number(x.toFixed(2)),
      y: Number(y.toFixed(2)),
      opacity: Number(opacity.toFixed(3)),
    });
  }

  return points;
}

export function resolveCameraTransform(
  target: CameraTarget,
  progress: number
): { transform: string; transformOrigin: string } {
  if (progress <= 0 || progress >= 1) {
    return {
      transform: 'scale(1)',
      transformOrigin: '50% 50%',
    };
  }

  const currentScale = 1 + (target.maxScale - 1) * progress;
  const originX = (target.focusX * 100).toFixed(1);
  const originY = (target.focusY * 100).toFixed(1);

  return {
    transform: `scale(${currentScale.toFixed(4)})`,
    transformOrigin: `${originX}% ${originY}%`,
  };
}

export function parseStrictProgress(rawProgress: string | null, defaultValue: number = 0): number {
  if (rawProgress === null || rawProgress === undefined || rawProgress === '') {
    return defaultValue;
  }

  // Reject strings like '0.5extra', 'NaN', '1.0.0'
  if (!/^-?\d+(\.\d+)?$/.test(rawProgress.trim())) {
    return defaultValue;
  }

  const parsed = Number(rawProgress);
  if (!Number.isFinite(parsed) || Number.isNaN(parsed)) {
    return defaultValue;
  }

  return Math.max(0, Math.min(1, parsed));
}

export interface SegmentSpec {
  readonly name: string;
  readonly owner: 'camera' | 'action' | 'pointer' | 'text';
  readonly durationMs: number;
  readonly easing: string;
  readonly keyframes: Keyframe[];
}

export interface SceneMotionSpec {
  readonly sceneKey: string;
  readonly cameraTarget: CameraTarget;
  readonly segments: readonly SegmentSpec[];
  readonly representativeHoldProgress: number;
  readonly onMilestone?: (milestone: string) => void;
  readonly dispose?: () => void;
}
