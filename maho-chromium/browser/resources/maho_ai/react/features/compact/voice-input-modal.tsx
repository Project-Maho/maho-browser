import * as DialogPrimitive from '@radix-ui/react-dialog';

import type {AppState} from '../../../types.js';
import {cn} from '@lib/utils';
import {Button} from '@ui/button';
import {Mic, MicOff, X} from '@icons/lucide';

interface VoiceInputModalProps {
  onClose: () => void;
  onRetry: () => void;
  state: AppState;
}

const WAVEFORM_WEIGHTS = [1, 0.94, 0.88, 0.8, 0.72, 0.64, 0.56, 0.48, 0.42, 0.36, 0.3, 0.25, 0.2, 0.16] as const;
const WAVEFORM_HEIGHT_CLASSES = [
  'h-2',
  'h-3',
  'h-4',
  'h-5',
  'h-6',
  'h-7',
  'h-8',
  'h-9',
  'h-10',
] as const;

function getPortalContainer(): HTMLElement|undefined {
  if (typeof document === 'undefined') {
    return undefined;
  }
  return document.getElementById('app') || undefined;
}

function getWaveformHeightClass(level: number, weight: number): string {
  if (level <= 0.03) {
    return 'h-1';
  }

  const bucket = Math.min(
      WAVEFORM_HEIGHT_CLASSES.length - 1,
      Math.max(0, Math.round(level * weight * (WAVEFORM_HEIGHT_CLASSES.length - 1))));
  return WAVEFORM_HEIGHT_CLASSES[bucket] || 'h-2';
}

function VoiceStatusIndicator({listening}: {listening: boolean}) {
  return (
    <div className={cn(
        'grid size-24 place-items-center rounded-full border border-border bg-secondary text-muted-foreground shadow-[var(--shadow-raised)]',
        listening && 'bg-primary/10 text-primary')}>
      <Mic className="size-9" />
    </div>
  );
}

function VoiceWaveform({level, listening}: {level: number; listening: boolean}) {
  return (
    <div className="mx-auto flex h-14 w-[min(17rem,78vw)] items-center justify-center gap-2 rounded-full border border-border bg-secondary/70 px-5 shadow-[var(--shadow-raised)]">
      {WAVEFORM_WEIGHTS.map((weight, index) => (
        <span
          aria-hidden="true"
          className={cn(
              'shrink-0 rounded-full bg-foreground/80 transition-[height,opacity] duration-150 ease-out',
              listening ? `w-1.5 ${getWaveformHeightClass(level, weight)}` : 'size-1 opacity-70')}
          key={`voice-wave-${index}`}
        />
      ))}
    </div>
  );
}

export function VoiceInputModal({onClose, onRetry, state}: VoiceInputModalProps) {
  const voice = state.voice;
  const transcript = voice.transcript.trim();
  const listening = voice.status === 'listening';
  const hasError = voice.status === 'error' || voice.status === 'unsupported';
  const headline = listening && transcript ? transcript : 'Hi! How can I help?';
  const errorMessage = voice.error || (voice.status === 'unsupported' ?
    'Voice input is not supported in this WebUI host.' :
    'Voice input stopped unexpectedly.');

  return (
    <DialogPrimitive.Root open={voice.active} onOpenChange={open => { if (!open) onClose(); }}>
      <DialogPrimitive.Portal container={getPortalContainer()}>
        <DialogPrimitive.Overlay className="fixed inset-0 z-40 bg-background/80 data-[state=closed]:animate-voice-fade-out data-[state=open]:animate-voice-fade-in" />
        <DialogPrimitive.Content
          aria-describedby="voice-input-description"
          className="fixed inset-0 z-50 grid min-h-0 overflow-hidden outline-none data-[state=closed]:animate-voice-fade-out data-[state=open]:animate-voice-fade-in"
          onOpenAutoFocus={event => event.preventDefault()}>
          <DialogPrimitive.Close asChild>
            <Button
              aria-label="Close voice input"
              className="absolute right-4 top-4 z-20 rounded-full border-border bg-secondary text-muted-foreground shadow-none hover:bg-surface-hover hover:text-foreground"
              size="icon"
              variant="outline">
              <X className="size-4" />
            </Button>
          </DialogPrimitive.Close>

          <section className="relative z-10 grid min-h-0 grid-rows-[1fr_auto] px-6 py-7 text-center text-foreground max-[520px]:px-4">
            <div className="flex min-h-0 flex-col items-center justify-center gap-7">
              {hasError ? (
                <div className="grid max-w-sm gap-5 rounded-[2rem] border border-border bg-card p-6 shadow-[var(--shadow-overlay)]">
                  <div className="mx-auto grid size-16 place-items-center rounded-full bg-secondary text-muted-foreground">
                    <MicOff className="size-7" />
                  </div>
                  <div className="grid gap-2">
                    <DialogPrimitive.Title className="text-2xl font-semibold tracking-tight text-foreground">
                      Voice needs attention
                    </DialogPrimitive.Title>
                    <DialogPrimitive.Description id="voice-input-description" className="text-sm leading-6 text-muted-foreground">
                      {errorMessage}
                    </DialogPrimitive.Description>
                  </div>
                  <div className="grid grid-cols-2 gap-2">
                    <Button className="rounded-full" type="button" variant="secondary" onClick={onRetry}>
                      Retry
                    </Button>
                    <Button className="rounded-full" type="button" variant="outline" onClick={onClose}>
                      Close
                    </Button>
                  </div>
                </div>
              ) : (
                <>
                  <VoiceStatusIndicator listening={listening} />
                  <div className="grid w-full max-w-xl gap-3">
                    <DialogPrimitive.Title className="mx-auto max-h-36 max-w-full overflow-y-auto text-balance break-words text-4xl font-semibold leading-tight tracking-[-0.04em] text-foreground max-[520px]:text-3xl">
                      {headline}
                    </DialogPrimitive.Title>
                    <DialogPrimitive.Description id="voice-input-description" className="text-sm font-medium text-muted-foreground">
                      {listening ? 'Listening… speak naturally.' : 'Opening microphone…'}
                    </DialogPrimitive.Description>
                  </div>
                </>
              )}
            </div>

            <VoiceWaveform level={voice.level} listening={listening && !hasError} />
          </section>
        </DialogPrimitive.Content>
      </DialogPrimitive.Portal>
    </DialogPrimitive.Root>
  );
}
