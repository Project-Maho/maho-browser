import * as React from 'react';

const LEGACY_HUE_OFFSET_DEGREES = 100;
const LEGACY_PICKER_PADDING_PIXELS = 50;
const REFERENCE_PICKER_SIZE_PIXELS = 152;
const KEYBOARD_STEP = 1;
const KEYBOARD_LARGE_STEP = 10;
const PICKER_RADIUS_PERCENT =
    (REFERENCE_PICKER_SIZE_PIXELS - LEGACY_PICKER_PADDING_PIXELS) /
    REFERENCE_PICKER_SIZE_PIXELS * 50;
const ARC_THICKNESS_PERCENT = 2 / REFERENCE_PICKER_SIZE_PIXELS * 100;

export type ColorWheelValue = Readonly<{hue: number; distance: number; secondaryHueOffset: number}>;

export type ColorWheelHandle = 'primary' | 'secondary';

export type UseColorWheelOptions = Readonly<{disabled?: boolean; onChange: (value: ColorWheelValue) => void; value: ColorWheelValue}>;

export type ColorWheelHandleBindings = Readonly<{
  'aria-label': string; 'aria-valuetext': string; disabled: boolean;
  ref: React.Ref<HTMLButtonElement>;
  onKeyDown: (event: React.KeyboardEvent<HTMLButtonElement>) => void;
  onLostPointerCapture: (event: React.PointerEvent<HTMLButtonElement>) => void;
  onPointerDown: (event: React.PointerEvent<HTMLButtonElement>) => void;
  onPointerMove: (event: React.PointerEvent<HTMLButtonElement>) => void;
  onPointerUp: (event: React.PointerEvent<HTMLButtonElement>) => void;
  onPointerCancel: (event: React.PointerEvent<HTMLButtonElement>) => void;
}>;

type ArcGeometry = Readonly<{path: string; x1: number; x2: number; y1: number; y2: number}>;

export type ColorWheelGeometry = Readonly<{
  arc: ArcGeometry; radiusPercent: number; primaryLeft: string; primaryTop: string;
  secondaryLeft: string; secondaryTop: string
}>;

export type UseColorWheelResult = Readonly<{
  activeHandle: ColorWheelHandle | null; geometry: ColorWheelGeometry;
  onWheelPointerDown: (event: React.PointerEvent<HTMLFieldSetElement>) => void;
  primaryBindings: ColorWheelHandleBindings; secondaryBindings: ColorWheelHandleBindings;
  wheelRef: React.RefObject<HTMLFieldSetElement | null>;
}>;

type PointerPoint = Readonly<{clientX: number; clientY: number}>;

type PointerInteraction = Readonly<{
  handle: ColorWheelHandle;
  owner: HTMLButtonElement;
  pointerId: number;
}>;

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.min(Math.max(value, minimum), maximum);
}

function normalizeDegrees(value: number): number {
  return ((value % 360) + 360) % 360;
}

function pointForAngle(angleDegrees: number, radius: number) {
  const radians = angleDegrees * Math.PI / 180;
  return {x: 50 + Math.cos(radians) * radius, y: 50 + Math.sin(radians) * radius};
}

function positionForAngle(angleDegrees: number, distance: number) {
  const point = pointForAngle(
      angleDegrees, clamp(distance, 0, 1) * PICKER_RADIUS_PERCENT);
  return {left: `${point.x}%`, top: `${point.y}%`};
}

function arcForAngles(
    primaryAngle: number, secondaryAngle: number, distance: number): ArcGeometry {
  const radius = clamp(distance, 0, 1) * PICKER_RADIUS_PERCENT;
  if (radius <= ARC_THICKNESS_PERCENT) {
    return {path: '', x1: 50, x2: 50, y1: 50, y2: 50};
  }
  const outerRadius = radius + ARC_THICKNESS_PERCENT / 2;
  const innerRadius = radius - ARC_THICKNESS_PERCENT / 2;
  const outerStart = pointForAngle(primaryAngle, outerRadius);
  const outerEnd = pointForAngle(secondaryAngle, outerRadius);
  const innerEnd = pointForAngle(secondaryAngle, innerRadius);
  const innerStart = pointForAngle(primaryAngle, innerRadius);
  const largeArc = normalizeDegrees(secondaryAngle - primaryAngle) > 180 ? 1 : 0;
  return {
    path: `M ${outerStart.x} ${outerStart.y} A ${outerRadius} ${outerRadius} 0 ${largeArc} 1 ${outerEnd.x} ${outerEnd.y} L ${innerEnd.x} ${innerEnd.y} A ${innerRadius} ${innerRadius} 0 ${largeArc} 0 ${innerStart.x} ${innerStart.y} Z`,
    x1: outerStart.x,
    x2: outerEnd.x,
    y1: outerStart.y,
    y2: outerEnd.y,
  };
}

export function useColorWheel(
    {disabled = false, onChange, value}: UseColorWheelOptions): UseColorWheelResult {
  const wheelRef = React.useRef<HTMLFieldSetElement | null>(null);
  const primaryButtonRef = React.useRef<HTMLButtonElement | null>(null);
  const secondaryButtonRef = React.useRef<HTMLButtonElement | null>(null);
  const activePointerRef = React.useRef<PointerInteraction | null>(null);
  const animationFrameRef = React.useRef<number | null>(null);
  const lastAppliedPointerRef = React.useRef<PointerPoint | null>(null);
  const pendingPointerRef = React.useRef<PointerPoint | null>(null);
  const valueRef = React.useRef(value);
  const onChangeRef = React.useRef(onChange);
  const [activeHandle, setActiveHandle] = React.useState<ColorWheelHandle | null>(null);
  valueRef.current = value;
  onChangeRef.current = onChange;

  const commitValue = React.useCallback((nextValue: ColorWheelValue) => {
    valueRef.current = nextValue;
    onChangeRef.current(nextValue);
  }, []);

  const updateFromPoint = React.useCallback(
      (handle: ColorWheelHandle, clientX: number, clientY: number) => {
        const rect = wheelRef.current?.getBoundingClientRect();
        if (!rect) {
          return;
        }
        const centerX = rect.left + rect.width / 2;
        const centerY = rect.top + rect.height / 2;
        // The rendered handle position is a percentage of the element, so the
        // pointer must be measured against that same percentage. Deriving the
        // radius in raw pixels here made the handle trail the cursor whenever
        // the wheel was not exactly REFERENCE_PICKER_SIZE_PIXELS wide.
        const radius =
            Math.min(rect.width, rect.height) * PICKER_RADIUS_PERCENT / 100;
        const deltaX = clientX - centerX;
        const deltaY = clientY - centerY;
        const angle = normalizeDegrees(Math.atan2(deltaY, deltaX) * 180 / Math.PI);
        const currentValue = valueRef.current;
        if (handle === 'secondary') {
          const primaryAngle = normalizeDegrees(
              currentValue.hue - LEGACY_HUE_OFFSET_DEGREES);
          commitValue({
            ...currentValue,
            secondaryHueOffset: normalizeDegrees(angle - primaryAngle),
          });
          return;
        }
        commitValue({
          ...currentValue,
          distance: radius > 0 ? clamp(Math.hypot(deltaX, deltaY) / radius, 0, 1) : 0,
          hue: normalizeDegrees(angle + LEGACY_HUE_OFFSET_DEGREES),
        });
      }, [commitValue]);

  const flushPointerFrame = React.useCallback(() => {
    animationFrameRef.current = null;
    const point = pendingPointerRef.current;
    const interaction = activePointerRef.current;
    pendingPointerRef.current = null;
    if (!point || !interaction) {
      return;
    }
    const lastPoint = lastAppliedPointerRef.current;
    if (lastPoint?.clientX === point.clientX &&
        lastPoint.clientY === point.clientY) {
      return;
    }
    lastAppliedPointerRef.current = point;
    updateFromPoint(interaction.handle, point.clientX, point.clientY);
  }, [updateFromPoint]);

  const schedulePointerUpdate = React.useCallback((clientX: number, clientY: number) => {
    pendingPointerRef.current = {clientX, clientY};
    animationFrameRef.current ??=
        window.requestAnimationFrame(flushPointerFrame);
  }, [flushPointerFrame]);

  const endPointerInteraction = React.useCallback((
      flush: boolean, releaseCapture: boolean, pointerId?: number) => {
    const interaction = activePointerRef.current;
    if (!interaction ||
        (pointerId !== undefined && interaction.pointerId !== pointerId)) {
      return;
    }
    if (animationFrameRef.current !== null) {
      window.cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
    }
    if (flush) {
      flushPointerFrame();
    } else {
      pendingPointerRef.current = null;
    }
    activePointerRef.current = null;
    lastAppliedPointerRef.current = null;
    setActiveHandle(null);
    if (releaseCapture &&
        interaction.owner.hasPointerCapture(interaction.pointerId)) {
      interaction.owner.releasePointerCapture(interaction.pointerId);
    }
  }, [flushPointerFrame]);

  React.useEffect(() => {
    return () => {
      if (animationFrameRef.current !== null) {
        window.cancelAnimationFrame(animationFrameRef.current);
      }
      const interaction = activePointerRef.current;
      animationFrameRef.current = null;
      pendingPointerRef.current = null;
      lastAppliedPointerRef.current = null;
      activePointerRef.current = null;
      if (interaction?.owner.hasPointerCapture(interaction.pointerId)) {
        interaction.owner.releasePointerCapture(interaction.pointerId);
      }
    };
  }, []);

  React.useEffect(() => {
    if (disabled) {
      endPointerInteraction(false, true);
    }
  }, [disabled, endPointerInteraction]);

  const beginPointerInteraction = React.useCallback(
      (handle: ColorWheelHandle, owner: HTMLButtonElement,
          pointerId: number, clientX: number, clientY: number) => {
        if (disabled) {
          return;
        }
        endPointerInteraction(false, true);
        owner.setPointerCapture(pointerId);
        activePointerRef.current = {handle, owner, pointerId};
        lastAppliedPointerRef.current = null;
        setActiveHandle(handle);
        schedulePointerUpdate(clientX, clientY);
      }, [disabled, endPointerInteraction, schedulePointerUpdate]);

  const finishCapturedPointer = React.useCallback(
      (event: React.PointerEvent<HTMLButtonElement>, flush: boolean) => {
        if (flush) {
          schedulePointerUpdate(event.clientX, event.clientY);
        }
        endPointerInteraction(flush, true, event.pointerId);
      }, [endPointerInteraction, schedulePointerUpdate]);

  const handleKeyboard = React.useCallback(
      (handle: ColorWheelHandle, event: React.KeyboardEvent<HTMLButtonElement>) => {
        if (disabled) {
          return;
        }
        const currentValue = valueRef.current;
        const step = event.shiftKey ? KEYBOARD_LARGE_STEP : KEYBOARD_STEP;
        if (event.key === 'Home') {
          event.preventDefault();
          commitValue(handle === 'primary' ?
            {...currentValue, distance: 0, hue: 0} :
            {...currentValue, distance: 0, secondaryHueOffset: 0});
          return;
        }
        if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key)) {
          return;
        }
        event.preventDefault();
        const direction = event.key === 'ArrowLeft' || event.key === 'ArrowDown' ? -1 : 1;
        if (handle === 'secondary') {
          commitValue({
            ...currentValue,
            secondaryHueOffset: normalizeDegrees(
                currentValue.secondaryHueOffset + direction * step),
          });
        } else if (event.key === 'ArrowUp' || event.key === 'ArrowDown') {
          const distanceDirection = event.key === 'ArrowDown' ? -1 : 1;
          commitValue({
            ...currentValue,
            distance: clamp(
                currentValue.distance + distanceDirection * step / 100, 0, 1),
          });
        } else {
          commitValue({
            ...currentValue,
            hue: normalizeDegrees(currentValue.hue + direction * step),
          });
        }
      }, [commitValue, disabled]);

  const primaryAngle = normalizeDegrees(value.hue - LEGACY_HUE_OFFSET_DEGREES);
  const secondaryAngle = normalizeDegrees(primaryAngle + value.secondaryHueOffset);
  const normalizedDistance = clamp(value.distance, 0, 1);
  const bindingsFor = (handle: ColorWheelHandle): ColorWheelHandleBindings => ({
    'aria-label': handle === 'primary' ? 'Primary color' : 'Secondary color',
    'aria-valuetext': handle === 'primary' ?
      `Hue ${Math.round(normalizeDegrees(value.hue))} degrees, saturation ${Math.round(normalizedDistance * 100)} percent` :
      `Hue ${Math.round(normalizeDegrees(value.hue + value.secondaryHueOffset))} degrees, offset ${Math.round(normalizeDegrees(value.secondaryHueOffset))} degrees`,
    disabled,
    onKeyDown: event => handleKeyboard(handle, event),
    onLostPointerCapture: event =>
      endPointerInteraction(false, false, event.pointerId),
    onPointerDown: event => {
      event.preventDefault();
      event.stopPropagation();
      event.currentTarget.focus();
      beginPointerInteraction(
          handle, event.currentTarget, event.pointerId,
          event.clientX, event.clientY);
    },
    onPointerMove: event => {
      if (activePointerRef.current?.handle === handle &&
          activePointerRef.current.pointerId === event.pointerId) {
        schedulePointerUpdate(event.clientX, event.clientY);
      }
    },
    onPointerUp: event => {
      if (activePointerRef.current?.handle === handle) {
        finishCapturedPointer(event, true);
      }
    },
    onPointerCancel: event => {
      if (activePointerRef.current?.handle === handle) {
        finishCapturedPointer(event, false);
      }
    },
    ref: handle === 'primary' ? primaryButtonRef : secondaryButtonRef,
  });

  const primaryPosition = positionForAngle(primaryAngle, normalizedDistance);
  const secondaryPosition = positionForAngle(secondaryAngle, normalizedDistance);
  return {
    activeHandle,
    geometry: {
      arc: arcForAngles(primaryAngle, secondaryAngle, normalizedDistance),
      primaryLeft: primaryPosition.left,
      primaryTop: primaryPosition.top,
      radiusPercent: normalizedDistance * PICKER_RADIUS_PERCENT * 2,
      secondaryLeft: secondaryPosition.left,
      secondaryTop: secondaryPosition.top,
    },
    onWheelPointerDown: event => {
      event.preventDefault();
      const primaryButton = primaryButtonRef.current;
      if (!primaryButton) {
        return;
      }
      primaryButton.focus();
      beginPointerInteraction(
          'primary', primaryButton, event.pointerId,
          event.clientX, event.clientY);
    },
    primaryBindings: bindingsFor('primary'),
    secondaryBindings: bindingsFor('secondary'),
    wheelRef,
  };
}
