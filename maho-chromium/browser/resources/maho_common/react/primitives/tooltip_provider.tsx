// Copyright 2026 Maho Browser. All rights reserved.

/**
 * Shared TooltipProvider wrapping @radix-ui/react-tooltip.
 * Extracted from maho_notes primitives for cross-surface reuse.
 */

import * as TooltipPrimitive from '@radix-ui/react-tooltip';
import type {ComponentPropsWithoutRef} from 'react';

export function TooltipProvider(
    {delayDuration = 200, ...props}: ComponentPropsWithoutRef<typeof TooltipPrimitive.Provider>) {
  return <TooltipPrimitive.Provider delayDuration={delayDuration} {...props} />;
}
