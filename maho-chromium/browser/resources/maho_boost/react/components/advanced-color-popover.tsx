import * as React from 'react';

import {Sliders} from '@icons/lucide';
import {Button} from '@ui/button';
import {Label} from '@ui/label';
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from '@ui/popover';
import {Slider} from '@ui/slider';

export interface AdvancedColorValues {
  readonly brightness: number;
  readonly contrast: number;
  readonly saturation: number;
}

export interface AdvancedColorPopoverProps {
  readonly disabled?: boolean;
  readonly onOpenChange: (open: boolean) => void;
  readonly onValueChange: (values: AdvancedColorValues) => void;
  readonly open: boolean;
  readonly values: AdvancedColorValues;
}

function SliderField(
    {disabled, id, label, maximum, minimum, onChange, value}: {
      readonly disabled: boolean;
      readonly id: string;
      readonly label: string;
      readonly maximum: number;
      readonly minimum: number;
      readonly onChange: (value: number) => void;
      readonly value: number;
    }) {
  return (
    <div className="grid gap-2 [-webkit-app-region:no-drag]">
      <div className="flex items-center justify-between gap-2">
        <Label className="text-xs font-semibold" id={`${id}-label`}>{label}</Label>
        <output className="text-xs tabular-nums text-muted-foreground" id={`${id}-value`}>
          {Math.round(value * 100)}%
        </output>
      </div>
      <Slider
        className="w-full [&>span:first-child]:h-1 [&>span:first-child]:bg-[var(--advanced-color-muted-track)] [&>span:first-child>span]:bg-transparent [-webkit-app-region:no-drag]"
        disabled={disabled}
        id={id}
        max={maximum}
        min={minimum}
        step={0.01}
        thumbProps={{
          'aria-describedby': `${id}-value`,
          'aria-label': label,
          'aria-valuetext': `${Math.round(value * 100)} percent`,
          className: 'size-[18px] border-[var(--advanced-color-thumb-border)] bg-[var(--advanced-color-thumb)] shadow-[0_2px_6px_rgba(19,23,28,0.12)] active:bg-[var(--advanced-color-thumb-active)] [-webkit-app-region:no-drag]',
        }}
        value={[value]}
        onValueChange={nextValue => onChange(nextValue[0] ?? value)}
      />
    </div>
  );
}

export function AdvancedColorPopover(
    {disabled = false, onOpenChange, onValueChange, open, values}:
    AdvancedColorPopoverProps) {
  const triggerRef = React.useRef<HTMLButtonElement | null>(null);
  const contentRef = React.useRef<HTMLDivElement | null>(null);
  const focusFrameRef = React.useRef<number | null>(null);

  React.useEffect(() => () => {
    if (focusFrameRef.current !== null) {
      window.cancelAnimationFrame(focusFrameRef.current);
      focusFrameRef.current = null;
    }
  }, []);

  const updateValue = <Key extends keyof AdvancedColorValues>(
      key: Key, value: AdvancedColorValues[Key]) => {
    onValueChange({...values, [key]: value});
  };

  return (
    <div
      id="advanced-color-toggle"
      className="min-w-0 [-webkit-app-region:no-drag]">
      <Popover open={open} onOpenChange={onOpenChange}>
        <PopoverTrigger asChild>
          <Button
            ref={triggerRef}
            aria-controls="advanced-color-popup"
            aria-expanded={open}
            aria-label="Advanced Color Controls"
            className="mod-button small h-[38px] w-full bg-secondary p-0 text-secondary-foreground shadow-none hover:bg-surface-hover aria-expanded:bg-surface-selected motion-reduce:transition-none"
            disabled={disabled}
            id="zen-boost-controls"
            size="icon"
            title="Advanced Color Controls"
            type="button"
            variant="secondary">
            <Sliders aria-hidden="true" className="boost-control-icon size-3" />
            <span className="boost-color-action-label">Tune</span>
          </Button>
        </PopoverTrigger>
        <PopoverContent
          ref={contentRef}
          align="center"
          aria-label="Advanced color controls"
          className="z-50 box-border w-[calc(100vw-16px)] max-w-[168px] overflow-hidden rounded-[13px] border border-border bg-popover p-3 text-popover-foreground shadow-md motion-reduce:animate-none motion-reduce:transition-none [-webkit-app-region:no-drag]"
          collisionPadding={8}
          id="advanced-color-popup"
          side="bottom"
          sideOffset={8}
          onCloseAutoFocus={event => {
            event.preventDefault();
            if (focusFrameRef.current !== null) {
              window.cancelAnimationFrame(focusFrameRef.current);
              focusFrameRef.current = null;
            }
            triggerRef.current?.focus();
          }}
          onOpenAutoFocus={event => {
            event.preventDefault();
            if (focusFrameRef.current !== null) {
              window.cancelAnimationFrame(focusFrameRef.current);
            }
            focusFrameRef.current = window.requestAnimationFrame(() => {
              focusFrameRef.current = null;
              const slider = contentRef.current?.querySelector('[role="slider"]');
              if (slider instanceof HTMLElement) {
                slider.focus();
              }
            });
          }}>
          <form
            className="!static !z-auto !grid !w-full !min-w-0 !max-w-none !gap-3 !overflow-hidden !rounded-none !border-0 !bg-transparent !p-0 !shadow-none"
            id="zen-boost-advanced-color-options-panel"
            onSubmit={event => event.preventDefault()}>
            <SliderField
              disabled={disabled}
              id="zen-boost-color-contrast"
              label="Contrast"
              maximum={0.9}
              minimum={0.05}
              value={values.contrast}
              onChange={value => updateValue('contrast', value)}
            />
            <SliderField
              disabled={disabled}
              id="zen-boost-color-brightness"
              label="Brightness"
              maximum={1}
              minimum={0}
              value={values.brightness}
              onChange={value => updateValue('brightness', value)}
            />
            <SliderField
              disabled={disabled}
              id="zen-boost-color-saturation"
              label="Original Saturation"
              maximum={1}
              minimum={0}
              value={values.saturation}
              onChange={value => updateValue('saturation', value)}
            />
          </form>
        </PopoverContent>
      </Popover>
    </div>
  );
}
