import * as TooltipPrimitive from '@radix-ui/react-tooltip';
import {forwardRef} from 'react';
import type {
  ButtonHTMLAttributes,
  ComponentPropsWithoutRef,
  InputHTMLAttributes,
  PropsWithChildren,
  TextareaHTMLAttributes,
} from 'react';
import {joinClassNames} from '//resources/maho_common/react/primitives/join_class_names.js';
export {TooltipProvider} from '//resources/maho_common/react/primitives/tooltip_provider.js';

type ButtonVariant = 'danger'|'ghost'|'primary'|'secondary';

type ButtonProps = PropsWithChildren<{
  className?: string;
  prefix?: string;
  variant?: ButtonVariant;
} & ButtonHTMLAttributes<HTMLButtonElement>>;

type TooltipButtonProps = ButtonProps & {
  tooltip: string;
  tooltipSide?: ComponentPropsWithoutRef<typeof TooltipPrimitive.Content>['side'];
  tooltipSideOffset?: number;
};

export const Button = forwardRef<HTMLButtonElement, ButtonProps>(function Button(
    {children, className, prefix, variant = 'secondary', ...props}, ref) {
      return (
        <button
          {...props}
          ref={ref}
          className={joinClassNames(
              'notes-button',
              `notes-button--${variant}`,
              className)}>
          {prefix ? <span className="notes-button__prefix">{prefix}</span> : null}
          {children}
        </button>
      );
    });

export function TooltipButton(
    {tooltip, tooltipSide = 'top', tooltipSideOffset = 8, ...props}: TooltipButtonProps) {
  return (
    <TooltipPrimitive.Root>
      <TooltipPrimitive.Trigger asChild>
        <Button {...props} />
      </TooltipPrimitive.Trigger>
      <TooltipPrimitive.Portal>
        <TooltipPrimitive.Content
          className="notes-badge notes-badge--accent"
          side={tooltipSide}
          sideOffset={tooltipSideOffset}>
          {tooltip}
        </TooltipPrimitive.Content>
      </TooltipPrimitive.Portal>
    </TooltipPrimitive.Root>
  );
}

Button.displayName = 'Button';

export function Badge({accent = false, children}: PropsWithChildren<{accent?: boolean}>) {
  return (
    <span className={joinClassNames('notes-badge', accent ? 'notes-badge--accent' : undefined)}>
      {children}
    </span>
  );
}

export function TextInput(props: InputHTMLAttributes<HTMLInputElement>) {
  return <input {...props} className={joinClassNames('notes-input', props.className)} />;
}

export function TextArea(props: TextareaHTMLAttributes<HTMLTextAreaElement>) {
  return <textarea {...props} className={joinClassNames('notes-textarea', props.className)} />;
}

export function Field({children, label}: PropsWithChildren<{label: string}>) {
  return (
    <label className="notes-field">
      <span className="notes-field__label">{label}</span>
      {children}
    </label>
  );
}


