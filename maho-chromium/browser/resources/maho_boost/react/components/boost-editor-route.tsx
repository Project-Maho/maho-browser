import * as React from 'react';

import {WindowMode} from '../../maho_boost.mojom-webui.js';
import {
  createBoostUpdate,
  createColorBoostUpdate,
  createTypographyBoostUpdate,
} from '../boost-state.js';
import {
  caseModeName,
  COMMON_FONTS,
  nextCaseMode,
  nextSizeMode,
  pointForColor,
  sizeModePercent,
} from '../boost-mode-values.js';
import type {BoostActions} from '../use-boost-actions.js';
import type {BoostController} from '../use-boost-controller.js';
import {ColorSection} from './color-section.js';
import {TitleStrip} from './title-strip.js';
import {TypographySection} from './typography-section.js';
import {ZapSection} from './zap-section.js';

export interface BoostEditorRouteProps {
  readonly actions: BoostActions;
  readonly active: boolean;
  readonly controller: BoostController;
  readonly disabled: boolean;
}

export function BoostEditorRoute(
    {actions, active, controller, disabled}: BoostEditorRouteProps) {
  const {state} = controller;
  const boost = state.boost;
  const [advancedColorOpen, setAdvancedColorOpen] = React.useState(false);

  React.useEffect(() => {
    if (!active || disabled) {
      setAdvancedColorOpen(false);
    }
  }, [active, disabled]);

  React.useLayoutEffect(() => {
    if (!advancedColorOpen) {
      return;
    }
    const content = document.getElementById('advanced-color-popup');
    const wrapper = content?.closest<HTMLElement>('[data-radix-popper-content-wrapper]');
    const portalSurface = wrapper ?? content;
    if (!portalSurface) {
      return;
    }
    portalSurface.inert = disabled;
    if (disabled) {
      portalSurface.setAttribute('aria-disabled', 'true');
    } else {
      portalSurface.removeAttribute('aria-disabled');
    }
    return () => {
      portalSurface.inert = false;
      portalSurface.removeAttribute('aria-disabled');
    };
  }, [advancedColorOpen, disabled]);

  const applyUpdate = React.useCallback((
      ...parameters: Parameters<BoostController['applyUpdate']>
  ): void => {
    if (disabled) {
      return;
    }
    controller.applyUpdate(...parameters);
  }, [controller, disabled]);

  const updateColor = React.useCallback((key: 'brightness' | 'contrast' | 'disable' | 'invert' | 'magic-theme' | 'saturation', values: Parameters<typeof createColorBoostUpdate>[0], slider = false): void => {
    applyUpdate(
        key,
        createBoostUpdate({color: createColorBoostUpdate(values)}),
        slider ? 'slider' : 'general');
  }, [applyUpdate]);

  if (!active) {
    return null;
  }

  return (
    <main
      aria-busy={actions.busy || state.isLoading}
      aria-disabled={disabled}
      className="boost-route boost-mode overflow-hidden bg-background text-foreground"
      data-route-state={disabled ? 'disabled' : 'active'}
      id="boost-editor">
      <div className="flex h-full min-h-0 flex-col" id="zen-boost-editor-root">
        <TitleStrip {...actions.titleProps} busy={disabled} />
        {boost && (
          <div
            aria-disabled={disabled}
            className="boost-route-body flex min-h-0 flex-1 flex-col gap-3.5 overflow-hidden p-4 pt-1.5"
            data-interaction-state={disabled ? 'disabled' : 'ready'}
            id="zen-boost-filter-wrapper"
            inert={disabled}>
            <ColorSection
              advancedColorOpen={advancedColorOpen}
              advancedValues={boost.color}
              colorAdjustmentsEnabled={boost.color.colorBoostEnabled}
              magicTheme={boost.color.magicTheme}
              smartInvert={boost.color.smartInvert}
              wheelValue={{
                distance: boost.color.dotDistance,
                hue: boost.color.dotAngleDeg,
                secondaryHueOffset: boost.color.secondaryDotAngleDegDelta,
              }}
              onAdvancedColorOpenChange={setAdvancedColorOpen}
              onAdvancedValuesChange={values => {
                const key = values.contrast !== boost.color.contrast ? 'contrast' :
                  values.brightness !== boost.color.brightness ? 'brightness' : 'saturation';
                updateColor(key, values, true);
              }}
              onColorAdjustmentsEnabledChange={enabled => updateColor('disable', {colorBoostEnabled: enabled})}
              onMagicThemeChange={enabled => updateColor('magic-theme', {colorBoostEnabled: true, magicTheme: enabled})}
              onSmartInvertChange={enabled => updateColor('invert', {smartInvert: enabled})}
              onWheelValueChange={value => applyUpdate(
                   'color-picker-primary',
                  createBoostUpdate({
                    color: createColorBoostUpdate({
                      colorBoostEnabled: true,
                      dotAngleDeg: value.hue,
                      dotDistance: value.distance,
                      dotPos: pointForColor(value.hue, value.distance),
                      magicTheme: false,
                      secondaryDotAngleDegDelta: value.secondaryHueOffset,
                      secondaryDotPos: pointForColor(
                          value.hue + value.secondaryHueOffset, value.distance),
                    }),
                  }))}
            />
            <TypographySection
              caseMode={caseModeName(boost.typography.caseMode)}
              commonFonts={COMMON_FONTS}
              disabled={disabled}
              selectedFont={boost.typography.fontFamily}
              sizePercent={sizeModePercent(boost.typography.sizeMode)}
              systemFonts={state.systemFonts}
              onCaseCycle={() => applyUpdate(
                  'case',
                  createBoostUpdate({
                    typography: createTypographyBoostUpdate({
                      caseMode: nextCaseMode(boost.typography.caseMode),
                    }),
                  }))}
              onFontChange={fontFamily => applyUpdate(
                  'font',
                  createBoostUpdate({
                    typography: createTypographyBoostUpdate({fontFamily, setFontFamily: true}),
                  }))}
              onSizeCycle={() => applyUpdate(
                  'size',
                  createBoostUpdate({
                    typography: createTypographyBoostUpdate({
                      setSizeMode: true,
                      sizeMode: nextSizeMode(boost.typography.sizeMode),
                    }),
                  }))}
            />
            <ZapSection
              disabled={disabled}
              pendingAction={actions.pendingAction === 'zap' || actions.pendingAction === 'code' ?
                actions.pendingAction : null}
              zapCount={boost.zapSelectors.length}
              zapModeEnabled={state.zapMode.isOn}
              onOpenCode={() => void actions.runAction({
                action: async () => {
                  setAdvancedColorOpen(false);
                  await controller.setMode(WindowMode.kCode);
                },
                loadingMessage: 'Opening Code editor',
                pending: 'code',
              })}
              onToggleZap={() => void actions.runAction({
                action: controller.toggleZap,
                loadingMessage: state.zapMode.isOn ? 'Exiting Zap mode' : 'Entering Zap mode',
                pending: 'zap',
              })}
            />
          </div>
        )}
      </div>
    </main>
  );
}
