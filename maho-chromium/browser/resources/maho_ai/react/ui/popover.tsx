import * as PopoverPrimitive from '@radix-ui/react-popover';
import type {ComponentPropsWithoutRef, ReactNode} from 'react';


export const Popover = PopoverPrimitive.Root;
export const PopoverTrigger = PopoverPrimitive.Trigger;

interface PopoverContentProps extends ComponentPropsWithoutRef<typeof PopoverPrimitive.Content> {
  children: ReactNode;
}

export function PopoverContent(
    {
      align,
      children,
      className,
      side = 'bottom',
      sideOffset = 8,
      ...props
    }: PopoverContentProps) {
  const container = typeof document === 'undefined' ? undefined : document.getElementById('app') || undefined;

  return (
    <PopoverPrimitive.Portal container={container}>
      <PopoverPrimitive.Content
        {...props}
        align={align}
        className={className}
        side={side}
        sideOffset={sideOffset}>
        {children}
      </PopoverPrimitive.Content>
    </PopoverPrimitive.Portal>
  );
}
