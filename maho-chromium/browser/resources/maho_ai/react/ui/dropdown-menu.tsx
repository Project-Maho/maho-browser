import * as DropdownMenuPrimitive from '@radix-ui/react-dropdown-menu';
import * as React from 'react';

import {cn} from '../lib/cn.js';

export const DropdownMenu = DropdownMenuPrimitive.Root;
export const DropdownMenuTrigger = DropdownMenuPrimitive.Trigger;

export const DropdownMenuContent = React.forwardRef<
    React.ElementRef<typeof DropdownMenuPrimitive.Content>,
    React.ComponentPropsWithoutRef<typeof DropdownMenuPrimitive.Content>>(
        function DropdownMenuContent(
            {
              align = 'end',
              className,
              side = 'bottom',
              sideOffset = 8,
              ...props
            },
            ref) {
          const container =
              typeof document === 'undefined' ?
              undefined :
              document.getElementById('app') || undefined;

          return (
            <DropdownMenuPrimitive.Portal container={container}>
              <DropdownMenuPrimitive.Content
                {...props}
                ref={ref}
                align={align}
                className={cn('compact-overflow-menu', className)}
                side={side}
                sideOffset={sideOffset}
              />
            </DropdownMenuPrimitive.Portal>
          );
        });

DropdownMenuContent.displayName = DropdownMenuPrimitive.Content.displayName;

export const DropdownMenuItem = React.forwardRef<
    React.ElementRef<typeof DropdownMenuPrimitive.Item>,
    React.ComponentPropsWithoutRef<typeof DropdownMenuPrimitive.Item>>(
        function DropdownMenuItem({className, ...props}, ref) {
          return (
            <DropdownMenuPrimitive.Item
              {...props}
              ref={ref}
              className={cn('compact-overflow-menu__item', className)}
            />
          );
        });

DropdownMenuItem.displayName = DropdownMenuPrimitive.Item.displayName;

export const DropdownMenuSeparator = React.forwardRef<
    React.ElementRef<typeof DropdownMenuPrimitive.Separator>,
    React.ComponentPropsWithoutRef<typeof DropdownMenuPrimitive.Separator>>(
        function DropdownMenuSeparator({className, ...props}, ref) {
          return (
            <DropdownMenuPrimitive.Separator
              {...props}
              ref={ref}
              className={cn('compact-overflow-menu__separator', className)}
            />
          );
        });

DropdownMenuSeparator.displayName = DropdownMenuPrimitive.Separator.displayName;

export const DropdownMenuLabel = React.forwardRef<
    React.ElementRef<typeof DropdownMenuPrimitive.Label>,
    React.ComponentPropsWithoutRef<typeof DropdownMenuPrimitive.Label>>(
        function DropdownMenuLabel({className, ...props}, ref) {
          return (
            <DropdownMenuPrimitive.Label
              {...props}
              ref={ref}
              className={cn('compact-overflow-menu__label', className)}
            />
          );
        });

DropdownMenuLabel.displayName = DropdownMenuPrimitive.Label.displayName;
