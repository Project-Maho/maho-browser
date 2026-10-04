// Copyright 2026 Maho Browser. All rights reserved.

import {Code, Loader2, Zap} from '@icons/lucide';
import {cn} from '@lib/utils';
import {Button} from '@ui/button';

export interface ZapSectionProps {
  readonly disabled?: boolean;
  readonly onOpenCode: () => void;
  readonly onToggleZap: () => void;
  readonly pendingAction?: 'code' | 'zap' | null;
  readonly zapCount: number;
  readonly zapModeEnabled: boolean;
}

const UTILITY_BUTTON_CLASS =
  'h-[38px] w-full min-w-0 justify-start gap-1.5 rounded-[6px] ' +
  'bg-[#ebebed] px-2.5 text-[10pt] font-normal text-[#242425] shadow-none ' +
  'hover:bg-[#ebebed] hover:opacity-[0.85] active:brightness-90 motion-reduce:transition-none';

export function ZapSection({
  disabled = false,
  onOpenCode,
  onToggleZap,
  pendingAction = null,
  zapCount,
  zapModeEnabled,
}: ZapSectionProps) {
  const safeZapCount = Math.max(0, zapCount);
  const legacyAttributes = {
    enabled: zapModeEnabled || safeZapCount > 0 ? 'true' : 'false',
    hideicon: safeZapCount > 0 ? 'true' : 'false',
  };
  const zapSelected = zapModeEnabled || safeZapCount > 0;
  const zapCountDescription = safeZapCount === 0 ?
    'No elements zapped' :
    `${safeZapCount} ${safeZapCount === 1 ? 'element' : 'elements'} zapped`;

  return (
    <section
      id="zap-section"
      aria-label="Boost utilities"
      className="zap-controls grid min-w-0 gap-3.5">
      <Button
        {...legacyAttributes}
        id="zen-boost-zap"
        aria-busy={pendingAction === 'zap'}
        aria-describedby="zen-boost-zap-description"
        aria-pressed={zapModeEnabled}
        className={cn(
          UTILITY_BUTTON_CLASS,
          zapSelected && 'bg-[#3a3a3a] text-[#fcfcfe] hover:bg-[#5b5b5c]',
        )}
        disabled={disabled || pendingAction !== null}
        size="sm"
        type="button"
        variant="secondary"
        onClick={onToggleZap}>
        {pendingAction === 'zap' ? (
          <Loader2
            aria-hidden="true"
            className="boost-control-icon size-3.5 shrink-0 animate-spin motion-reduce:animate-none"
          />
        ) : (
          <Zap aria-hidden="true" className="boost-control-icon size-3.5 shrink-0 fill-current" />
        )}
        <span id="zen-boost-zap-text" className="shrink-0">Zap</span>
        {safeZapCount > 0 && pendingAction !== 'zap' && (
          <span
            id="zen-boost-zap-value"
            className="ml-auto shrink-0 text-right font-semibold tabular-nums">
            {safeZapCount}
          </span>
        )}
      </Button>
      <span id="zen-boost-zap-description" className="sr-only">{zapCountDescription}</span>

      <Button
        id="zen-boost-code"
        aria-label="Open Code editor"
        aria-busy={pendingAction === 'code'}
        className={UTILITY_BUTTON_CLASS}
        disabled={disabled || pendingAction !== null}
        size="sm"
        type="button"
        variant="secondary"
        onClick={onOpenCode}>
        {pendingAction === 'code' ? (
          <Loader2
            aria-hidden="true"
            className="boost-control-icon size-3.5 shrink-0 animate-spin motion-reduce:animate-none"
          />
        ) : (
          <Code aria-hidden="true" className="boost-control-icon size-3.5 shrink-0" />
        )}
        <span id="zen-boost-code-text" className="shrink-0">Code</span>
      </Button>
    </section>
  );
}
