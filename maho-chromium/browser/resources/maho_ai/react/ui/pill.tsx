import {cva, type VariantProps} from 'class-variance-authority';

import {cn} from '../lib/cn.js';

export const pillVariants = cva('console-pill', {
  variants: {
    tone: {
      accent: 'console-pill--accent',
      danger: 'console-pill--danger',
      neutral: 'console-pill--neutral',
      success: 'console-pill--success',
      warning: 'console-pill--warning',
    },
  },
  defaultVariants: {
    tone: 'neutral',
  },
});

export type PillTone = NonNullable<VariantProps<typeof pillVariants>['tone']>;

export function Pill(
    {className, text, tone}: {className?: string; text: string} &
        VariantProps<typeof pillVariants>) {
  return <span className={cn(pillVariants({tone}), className)}>{text}</span>;
}
