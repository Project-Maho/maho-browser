// Copyright 2026 Maho Browser. All rights reserved.

import {cn} from '@lib/utils';
import {Button} from '@ui/button';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@ui/select';
import {Separator} from '@ui/separator';
import {ToggleGroup, ToggleGroupItem} from '@ui/toggle-group';

export type BoostCaseMode = 'none' | 'uppercase' | 'lowercase' | 'capitalize';

export interface TypographySectionProps {
  readonly caseMode: BoostCaseMode;
  readonly commonFonts: readonly string[];
  readonly disabled?: boolean;
  readonly fontError?: string | null;
  readonly fontsLoading?: boolean;
  readonly onCaseCycle: () => void;
  readonly onFontChange: (fontFamily: string) => void;
  readonly onSizeCycle: () => void;
  readonly selectedFont: string;
  readonly sizePercent: 90 | 100 | 110 | 125 | 150;
  readonly systemFonts: readonly string[];
}

const DEFAULT_FONT_VALUE = '__maho_default_font__';

const CASE_LABELS: Readonly<Record<BoostCaseMode, string>> = {
  none: 'Default',
  uppercase: 'Uppercase',
  lowercase: 'Lowercase',
  capitalize: 'Capitalize',
};

const CASE_BUTTON_MODES: Readonly<Record<BoostCaseMode, string>> = {
  none: 'none',
  uppercase: 'orange',
  lowercase: 'orange-red',
  capitalize: 'red',
};

const CASE_BUTTON_STYLES: Readonly<Record<BoostCaseMode, string>> = {
  none: '',
  uppercase: 'bg-gradient-to-b from-[#ffbb5d] to-[#ffa01d] text-[#e3e9e4]',
  lowercase: 'bg-gradient-to-b from-[#ff8758] to-[#ff5b1b] text-[#e3e9e4]',
  capitalize: 'bg-gradient-to-b from-[#ff595f] to-[#ff121b] text-[#e3e9e4]',
};

const SIZE_BUTTON_MODES: Readonly<Record<TypographySectionProps['sizePercent'], string>> = {
  90: 'blue',
  100: 'none',
  110: 'orange',
  125: 'orange-red',
  150: 'red',
};

const SIZE_BUTTON_STYLES: Readonly<Record<TypographySectionProps['sizePercent'], string>> = {
  90: 'bg-gradient-to-b from-[#6650fc] to-[#4125ff] text-[#e3e9e4]',
  100: '',
  110: 'bg-gradient-to-b from-[#ffbb5d] to-[#ffa01d] text-[#e3e9e4]',
  125: 'bg-gradient-to-b from-[#ff8758] to-[#ff5b1b] text-[#e3e9e4]',
  150: 'bg-gradient-to-b from-[#ff595f] to-[#ff121b] text-[#e3e9e4]',
};

export function TypographySection({
  caseMode,
  commonFonts,
  disabled = false,
  fontError = null,
  fontsLoading = false,
  onCaseCycle,
  onFontChange,
  onSizeCycle,
  selectedFont,
  sizePercent,
  systemFonts,
}: TypographySectionProps) {
  const selectValue = selectedFont === '' ? DEFAULT_FONT_VALUE : selectedFont;
  const typographyDisabled = disabled || fontsLoading;
  const caseModeAttributes = {
    'case-mode': caseMode,
    mode: CASE_BUTTON_MODES[caseMode],
  };
  const fontSelectAttributes = {
    'has-selection': selectedFont === '' ? 'false' : 'true',
  };
  const sizeModeAttributes = {mode: SIZE_BUTTON_MODES[sizePercent]};

  return (
    <section id="typography-section" aria-label="Typography" className="grid min-w-0 gap-3.5">
      <div
        id="zen-boost-font-wrapper"
        className="flex min-w-0 flex-col rounded-[6px] bg-white pb-1 pt-1 text-black/60 shadow-[0_2px_6px_rgba(0,0,0,0.15)]">
        <div id="font-grid" className="min-w-0">
          <ToggleGroup
            id="zen-boost-font-grid"
            aria-label="Font"
            className="grid h-[82px] w-full min-w-0 grid-cols-5 grid-rows-3 gap-0 p-0.5"
            disabled={typographyDisabled}
            type="single"
            value={selectedFont}
            variant="default"
            onValueChange={onFontChange}>
            {commonFonts.map(font => (
              <ToggleGroupItem
                key={font}
                aria-label={font}
                aria-pressed={selectedFont === font}
                className={cn(
                  'font-button m-auto h-[26px] w-[26px] min-w-[26px] rounded-full p-0 text-[12px] font-normal leading-none text-black/60 shadow-none transition-[background-color,color,transform] duration-100 hover:scale-110 hover:bg-[#9a9a9a30] hover:text-black/60 data-[state=on]:bg-[#5454572f] data-[state=on]:text-black/60 motion-reduce:scale-100 motion-reduce:transition-none',
                  selectedFont === font && 'zen-boost-font-button-active',
                )}
                style={{fontFamily: font}}
                value={font}>
                Aa
              </ToggleGroupItem>
            ))}
          </ToggleGroup>
        </div>

        <Separator className="mx-3 mt-0.5 w-auto bg-[#9a9a9a] opacity-25" />

        <div id="zen-boost-font-toolbar" className="flex h-7 min-w-0 items-start justify-between">
          <Select
            disabled={typographyDisabled}
            value={selectValue}
            onValueChange={value => onFontChange(value === DEFAULT_FONT_VALUE ? '' : value)}>
            <SelectTrigger
              {...fontSelectAttributes}
              id="zen-boost-font-select"
              aria-label="Font"
              className={cn(
                'ml-2 mr-[3px] mt-2 h-5 min-w-0 flex-1 border-0 bg-transparent px-0.5 py-0 text-[12px] font-normal text-[#727272] opacity-75 shadow-none hover:opacity-100 focus:ring-1 focus:ring-ring motion-reduce:transition-none [&_svg]:size-3',
                selectedFont !== '' && 'bg-[#ebebed] opacity-100',
              )}>
              <SelectValue placeholder="Default font" />
            </SelectTrigger>
            <SelectContent
              className="text-xs motion-reduce:animate-none motion-reduce:transition-none"
              collisionPadding={8}>
              <SelectItem className="text-xs" value={DEFAULT_FONT_VALUE}>Default font</SelectItem>
              {systemFonts.map(font => (
                <SelectItem key={font} className="text-xs" style={{fontFamily: font}} value={font}>
                  {font}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
      </div>

      {fontsLoading && (
        <p className="text-xs font-medium text-muted-foreground" role="status" aria-live="polite">
          Loading fonts
        </p>
      )}
      {fontError && (
        <p className="text-xs font-medium text-destructive" role="alert">
          {fontError}
        </p>
      )}

      <div id="zen-boost-toolbar-wrapper" className="flex h-[38px] min-w-0 gap-2">
        <div id="size-cycle-btn" className="flex min-w-0 flex-1">
          <Button
            {...sizeModeAttributes}
            id="zen-boost-size"
            aria-label={`Size ${sizePercent}%`}
            aria-pressed={sizePercent !== 100}
            className={cn(
              'h-[38px] w-full min-w-0 rounded-[6px] bg-[#ebebed] px-2 text-[10pt] font-normal text-[#242425] shadow-none hover:bg-[#ebebed] hover:opacity-[0.85] active:brightness-90 motion-reduce:transition-none',
              SIZE_BUTTON_STYLES[sizePercent],
            )}
            disabled={disabled}
            size="sm"
            type="button"
            variant="secondary"
            onClick={onSizeCycle}>
            <span
              id="zen-boost-size-text"
              style={{display: sizePercent === 100 ? 'inline' : 'none'}}>
              Size
            </span>
            <span
              id="zen-boost-size-value"
              className="font-semibold"
              style={{display: sizePercent === 100 ? 'none' : 'inline'}}>
              {sizePercent}%
            </span>
          </Button>
        </div>

        <div id="case-cycle-btn" className="flex min-w-0 flex-1">
          <Button
            id="zen-boost-case"
            aria-label={`Case ${CASE_LABELS[caseMode]}`}
            aria-pressed={caseMode !== 'none'}
            className={cn(
              'h-[38px] w-full min-w-0 rounded-[6px] bg-[#ebebed] px-2 text-[10pt] font-normal text-[#242425] shadow-none hover:bg-[#ebebed] hover:opacity-[0.85] active:brightness-90 motion-reduce:transition-none',
              CASE_BUTTON_STYLES[caseMode],
            )}
            {...caseModeAttributes}
            disabled={disabled}
            size="sm"
            type="button"
            variant="secondary"
            onClick={onCaseCycle}>
            <span id="zen-boost-case-text" hidden={caseMode !== 'none'}>Case</span>
          </Button>
        </div>
      </div>
    </section>
  );
}
