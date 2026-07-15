import * as DialogPrimitive from '@radix-ui/react-dialog';

import type {AppState} from '../../../types.js';
import {cn} from '@lib/utils';
import {Button} from '@ui/button';
import {MicOff, X} from '@icons/lucide';

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

function VoiceSmiley({listening}: {listening: boolean}) {
  return (
    <div className={cn(
        'relative grid size-32 place-items-center rounded-full border border-white/15 bg-white/8 shadow-[0_24px_100px_-36px_rgba(255,255,255,0.7)] backdrop-blur-xl',
        listening && 'animate-voice-breathe')}>
      <div className="pointer-events-none absolute inset-3 rounded-full bg-[radial-gradient(circle_at_50%_34%,rgba(255,255,255,0.28),transparent_62%)]" />
      <div aria-hidden="true" className="relative grid h-20 w-24 place-items-center">
        <div className="absolute top-3 flex w-14 items-center justify-between">
          <span className="animate-voice-blink h-7 w-2 rounded-full bg-white shadow-[0_0_24px_rgba(255,255,255,0.7)]" />
          <span className="animate-voice-blink h-7 w-2 rounded-full bg-white shadow-[0_0_24px_rgba(255,255,255,0.7)] [animation-delay:0.08s]" />
        </div>
        <svg
          aria-hidden="true"
          className="absolute bottom-2 h-10 w-16 overflow-visible text-white drop-shadow-[0_0_20px_rgba(255,255,255,0.5)]"
          fill="none"
          focusable="false"
          viewBox="0 0 64 40">
          <path
            d="M14 12C18 27 46 27 50 12"
            stroke="currentColor"
            strokeLinecap="round"
            strokeWidth="7"
          />
        </svg>
      </div>
    </div>
  );
}

function VoiceWaveform({level, listening}: {level: number; listening: boolean}) {
  return (
    <div className="mx-auto flex h-14 w-[min(17rem,78vw)] items-center justify-center gap-2 rounded-full border border-white/12 bg-white/10 px-5 shadow-[0_18px_70px_-36px_rgba(59,76,255,0.85)] backdrop-blur-2xl">
      {WAVEFORM_WEIGHTS.map((weight, index) => (
        <span
          aria-hidden="true"
          className={cn(
              'shrink-0 rounded-full bg-white/90 shadow-[0_0_18px_rgba(255,255,255,0.42)] transition-[height,opacity] duration-150 ease-out',
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
        <DialogPrimitive.Overlay className="fixed inset-0 z-40 bg-background/70 backdrop-blur-2xl data-[state=closed]:animate-voice-fade-out data-[state=open]:animate-voice-fade-in" />
        <DialogPrimitive.Content
          aria-describedby="voice-input-description"
          className="fixed inset-0 z-50 grid min-h-0 overflow-hidden outline-none data-[state=closed]:animate-voice-fade-out data-[state=open]:animate-voice-fade-in"
          onOpenAutoFocus={event => event.preventDefault()}>
          <div aria-hidden="true" className="voice-bottom-glow pointer-events-none absolute inset-x-[-22%] bottom-[-24%] h-[55%] opacity-90 blur-3xl" />
          <div aria-hidden="true" className="pointer-events-none absolute inset-0 bg-[radial-gradient(circle_at_50%_28%,rgba(255,255,255,0.08),transparent_34%)]" />

          <DialogPrimitive.Close asChild>
            <Button
              aria-label="Close voice input"
              className="absolute right-4 top-4 z-20 rounded-full border-white/10 bg-white/10 text-white shadow-none hover:bg-white/15 hover:text-white focus-visible:ring-white/70"
              size="icon"
              variant="outline">
              <X className="size-4" />
            </Button>
          </DialogPrimitive.Close>

          <section className="relative z-10 grid min-h-0 grid-rows-[1fr_auto] px-6 py-7 text-center text-white max-[520px]:px-4">
            <div className="flex min-h-0 flex-col items-center justify-center gap-7">
              {hasError ? (
                <div className="grid max-w-sm gap-5 rounded-[2rem] border border-white/12 bg-black/20 p-6 shadow-[0_28px_120px_-48px_rgba(226,59,154,0.8)] backdrop-blur-2xl">
                  <div className="mx-auto grid size-16 place-items-center rounded-full bg-white/10 text-white shadow-[0_0_48px_rgba(255,255,255,0.24)]">
                    <MicOff className="size-7" />
                  </div>
                  <div className="grid gap-2">
                    <DialogPrimitive.Title className="text-2xl font-semibold tracking-tight text-white">
                      Voice needs attention
                    </DialogPrimitive.Title>
                    <DialogPrimitive.Description id="voice-input-description" className="text-sm leading-6 text-white/72">
                      {errorMessage}
                    </DialogPrimitive.Description>
                  </div>
                  <div className="grid grid-cols-2 gap-2">
                    <Button className="rounded-full" type="button" variant="secondary" onClick={onRetry}>
                      Retry
                    </Button>
                    <Button className="rounded-full border-white/15 bg-white/10 text-white hover:bg-white/15 hover:text-white" type="button" variant="outline" onClick={onClose}>
                      Close
                    </Button>
                  </div>
                </div>
              ) : (
                <>
                  <VoiceSmiley listening={listening} />
                  <div className="grid w-full max-w-xl gap-3">
                    <DialogPrimitive.Title className="voice-headline-gradient mx-auto max-h-36 max-w-full overflow-y-auto text-balance break-words text-4xl font-semibold leading-tight tracking-[-0.04em] max-[520px]:text-3xl">
                      {headline}
                    </DialogPrimitive.Title>
                    <DialogPrimitive.Description id="voice-input-description" className="text-sm font-medium text-white/60">
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
