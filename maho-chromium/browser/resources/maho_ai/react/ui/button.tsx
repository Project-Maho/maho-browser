import {Slot} from '@radix-ui/react-slot';
import {cva, type VariantProps} from 'class-variance-authority';
import * as React from 'react';

import {cn} from '../lib/cn.js';

export const buttonVariants = cva('', {
  variants: {
    variant: {
      'approval-approve':
          'compact-thread__approval-button compact-thread__approval-button--approve',
      'approval-deny':
          'compact-thread__approval-button compact-thread__approval-button--deny',
      'compact-icon': 'compact-icon-button',
      'compact-icon-accent': 'compact-icon-button compact-icon-button--accent',
      'compact-icon-ghost': 'compact-icon-button compact-icon-button--ghost',
      'composer-icon': 'compact-composer__icon-button',
      'composer-send': 'compact-composer__send',
      'console-default': 'console-button',
      'console-primary': 'console-button console-button--primary',
      'inspector-tab': 'inspector-tab',
      'mode-toggle': 'mode-toggle__button',
      'overflow-item': 'compact-overflow-menu__item',
      'session-list-item': 'session-list__item',
      'thread-action': 'compact-thread__action-button',
    },
    size: {
      auto: '',
      icon: '',
      medium: '',
      small: '',
    },
  },
  defaultVariants: {
    size: 'auto',
    variant: 'console-default',
  },
});

export type ButtonVariant = NonNullable<VariantProps<typeof buttonVariants>['variant']>;
export type ButtonSize = NonNullable<VariantProps<typeof buttonVariants>['size']>;

export interface ButtonProps
    extends React.ButtonHTMLAttributes<HTMLButtonElement>,
        VariantProps<typeof buttonVariants> {
  asChild?: boolean;
}

export const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(function Button(
    {asChild, className, size, type = 'button', variant, ...props}, ref) {
      const Comp = asChild ? Slot : 'button';

      return (
        <Comp
          ref={ref}
          className={cn(buttonVariants({variant, size}), className)}
          type={asChild ? undefined : type}
          {...props}
        />
      );
    });

Button.displayName = 'Button';
