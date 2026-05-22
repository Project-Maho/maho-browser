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

export const DropdownMenuSeparator = DropdownMenuPrimitive.Separator;
export const DropdownMenuLabel = DropdownMenuPrimitive.Label;
