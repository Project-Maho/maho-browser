import {cn} from '@lib/utils';
import {Eye, EyeOff, Sparkles} from '@icons/lucide';
import {Button} from '@ui/button';
import {
  AdvancedColorPopover,
  type AdvancedColorValues,
} from './advanced-color-popover.js';
import {
  type ColorWheelValue,
  useColorWheel,
} from '../use-color-wheel.js';

export interface ColorSectionProps {
  readonly advancedColorOpen: boolean;
  readonly advancedValues: AdvancedColorValues;
  readonly colorAdjustmentsEnabled: boolean;
  readonly magicTheme: boolean;
  readonly onAdvancedColorOpenChange: (open: boolean) => void;
  readonly onAdvancedValuesChange: (values: AdvancedColorValues) => void;
  readonly onColorAdjustmentsEnabledChange: (enabled: boolean) => void;
  readonly onMagicThemeChange: (enabled: boolean) => void;
  readonly onSmartInvertChange: (enabled: boolean) => void;
  readonly onWheelValueChange: (value: ColorWheelValue) => void;
  readonly smartInvert: boolean;
  readonly wheelValue: ColorWheelValue;
}

const ICON_BUTTON_CLASS =
    'mod-button small h-[38px] w-full min-w-0 bg-secondary p-0 text-[#3a3a3b] shadow-none hover:bg-surface-hover motion-reduce:transition-none [-webkit-app-region:no-drag]';

const MAGIC_THEME_BACKGROUND =
    'linear-gradient(180deg, color-mix(in srgb, #7e57c2 70%, white) 0%, #7e57c2 100%)';

function hslForHue(hue: number, distance: number, lightness: number): string {
  return `hsl(${hue}deg ${Math.max(0, Math.min(1, distance)) * 100}% ${lightness}%)`;
}

export function ColorSection(
    {
      advancedColorOpen,
      advancedValues,
      colorAdjustmentsEnabled,
      magicTheme,
      onAdvancedColorOpenChange,
      onAdvancedValuesChange,
      onColorAdjustmentsEnabledChange,
      onMagicThemeChange,
      onSmartInvertChange,
      onWheelValueChange,
      smartInvert,
      wheelValue,
    }: ColorSectionProps) {
  const automaticColorMode = magicTheme || !colorAdjustmentsEnabled;
  const {
    activeHandle,
    geometry,
    onWheelPointerDown,
    primaryBindings,
    secondaryBindings,
    wheelRef,
  } = useColorWheel({
    onChange: onWheelValueChange,
    value: wheelValue,
  });
  const primaryColor = hslForHue(wheelValue.hue, wheelValue.distance, 55);
  const secondaryHue = wheelValue.hue + wheelValue.secondaryHueOffset;
  const secondaryColor = hslForHue(secondaryHue, wheelValue.distance, 20);

  return (
    <section
      aria-label="Color"
      className="grid gap-2 [-webkit-app-region:no-drag]"
      id="color-section">
      <fieldset
        aria-label="Color wheel"
        aria-disabled={false}
        className={cn(
          'color-wheel-container zen-boost-color-picker-gradient group relative m-0 mb-1 mt-2.5 aspect-square w-full min-w-0 touch-none select-none overflow-hidden rounded-2xl border-0 p-0 shadow-[0_4px_12px_#00000021] focus-within:ring-1 focus-within:ring-ring motion-reduce:transition-none [-webkit-app-region:no-drag]',
          automaticColorMode && 'zen-boost-panel-disabled',
        )}
        data-color-mode={magicTheme ? 'auto' :
          colorAdjustmentsEnabled ? 'manual' : 'off'}
        data-disabled="false"
        id="color-wheel"
        ref={wheelRef}
        style={magicTheme ? {background: MAGIC_THEME_BACKGROUND} : undefined}
        onPointerDown={onWheelPointerDown}>
        <span
          className="absolute left-1/2 top-0 z-20 -translate-x-1/2 [-webkit-app-region:no-drag]"
          id="magic-theme-toggle">
          <Button
            aria-label="Use automatic theme colors"
            aria-pressed={magicTheme}
            className={cn(
              'mod-button h-6 !w-auto gap-1 rounded-md bg-background px-1.5 py-0 text-[#3a3a3b] shadow-sm hover:bg-surface-hover motion-reduce:transition-none',
              magicTheme && 'zen-boost-button-active',
            )}
            id="zen-boost-magic-theme"
            size="icon"
            title="Use automatic theme colors"
            type="button"
            variant="secondary"
            onPointerDown={event => event.stopPropagation()}
            onClick={() => onMagicThemeChange(!magicTheme)}>
            <Sparkles
              aria-hidden="true"
              className="boost-control-icon size-3"
              data-icon-slot="sparkles"
            />
            <span className="boost-color-action-label">Auto</span>
          </Button>
        </span>
        <button
          {...primaryBindings}
          aria-describedby="primary-color-value"
          className="zen-boost-color-picker-dot [-webkit-app-region:no-drag]"
          data-dragging={activeHandle === 'primary'}
          data-handle="primary"
          id="zen-boost-color-picker-dot-primary"
          style={{
            backgroundColor: primaryColor,
            left: geometry.primaryLeft,
            top: geometry.primaryTop,
          }}
          type="button">
          <span
            aria-hidden="true"
            className="pointer-events-none absolute inset-0 rounded-full"
            id="dot-primary"
          />
        </button>
        <span className="sr-only" id="primary-color-value">
          {primaryBindings['aria-valuetext']}
        </span>
        <button
          {...secondaryBindings}
          aria-describedby="secondary-color-value"
          className="zen-boost-color-picker-dot [-webkit-app-region:no-drag]"
          data-dragging={activeHandle === 'secondary'}
          data-handle="secondary"
          id="zen-boost-color-picker-dot-secondary"
          style={{
            backgroundColor: secondaryColor,
            left: geometry.secondaryLeft,
            top: geometry.secondaryTop,
          }}
          type="button">
          <span
            aria-hidden="true"
            className="pointer-events-none absolute inset-0 rounded-full"
            id="dot-secondary"
          />
        </button>
        <span className="sr-only" id="secondary-color-value">
          {secondaryBindings['aria-valuetext']}
        </span>
        <div
          aria-hidden="true"
          className="zen-boost-color-picker-circle group-focus-within:opacity-40 motion-reduce:transition-none"
          style={{
            height: `${geometry.radiusPercent}%`,
            opacity: activeHandle === null ? undefined : 0.4,
            width: `${geometry.radiusPercent}%`,
          }}
        />
        <svg
          aria-hidden="true"
          className="zen-boost-color-picker-arc-svg pointer-events-none absolute inset-0 z-[3] size-full group-focus-within:opacity-40 motion-reduce:transition-none"
          style={{opacity: activeHandle === null ? undefined : 0.4}}
          viewBox="0 0 100 100">
          <defs>
            <linearGradient
              gradientUnits="userSpaceOnUse"
              id="zen-boost-color-arc-gradient"
              x1={geometry.arc.x1}
              x2={geometry.arc.x2}
              y1={geometry.arc.y1}
              y2={geometry.arc.y2}>
              <stop offset="0%" stopColor={primaryColor} />
              <stop offset="100%" stopColor={secondaryColor} />
            </linearGradient>
          </defs>
          <path
            className="arc-fill"
            d={geometry.arc.path}
            fill="url(#zen-boost-color-arc-gradient)"
            opacity="0.65"
          />
        </svg>
      </fieldset>

      <div className="grid h-[38px] grid-cols-3 gap-2" id="zen-boost-toolbar-wrapper-colors">
        <div className="min-w-0" id="smart-invert-toggle">
          <Button
            aria-label="Smart Invert Colors"
            aria-pressed={smartInvert}
            className={cn(
              ICON_BUTTON_CLASS,
              smartInvert && 'zen-boost-button-active',
            )}
            id="zen-boost-invert"
            size="icon"
            title="Smart Invert Colors"
            type="button"
            variant="secondary"
            onClick={() => onSmartInvertChange(!smartInvert)}>
            <Eye aria-hidden="true" className="boost-control-icon size-3" />
            <span className="boost-color-action-label">Invert</span>
          </Button>
        </div>
        <AdvancedColorPopover
          disabled={!colorAdjustmentsEnabled}
          open={advancedColorOpen}
          values={advancedValues}
          onOpenChange={onAdvancedColorOpenChange}
          onValueChange={onAdvancedValuesChange}
        />
        <Button
          aria-label="Disable Color Adjustments"
          aria-pressed={!colorAdjustmentsEnabled}
          className={cn(
            ICON_BUTTON_CLASS,
            !colorAdjustmentsEnabled && 'zen-boost-button-active',
          )}
          id="zen-boost-disable"
          size="icon"
          title="Disable Color Adjustments"
          type="button"
          variant="secondary"
          onClick={() => onColorAdjustmentsEnabledChange(!colorAdjustmentsEnabled)}>
          <EyeOff aria-hidden="true" className="boost-control-icon size-3" />
          <span className="boost-color-action-label">Off</span>
        </Button>
      </div>
    </section>
  );
}
