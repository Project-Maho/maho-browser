import {Card, CardContent} from '@ui/card';
import type {AppState} from '../../../types.js';

export function CompactStage({state}: {state: AppState}) {
  const label = state.currentSessionId ? 'Live session ready' : 'Ready when you are';
  const description = state.currentSessionId ?
      'Ask anything about this page to start the conversation.' :
      'Start with a prompt below and Maho AI will open a live session in this thread.';

  return (
    <main className="flex min-h-0 flex-1 flex-col px-1 pb-6 pt-4">
      <div className="flex min-h-0 flex-1 items-center justify-center">
        <section className="w-full max-w-xl" aria-labelledby="compact-stage-title">
          <Card className="relative overflow-hidden border-border/80 bg-card/90 shadow-[0_28px_90px_-56px_var(--color-accent-glow)] backdrop-blur-xl">
            <div className="pointer-events-none absolute inset-x-0 top-0 flex justify-center">
              <div className="animate-halo-drift mt-[-5rem] h-40 w-40 rounded-full bg-[radial-gradient(circle_at_center,var(--color-accent-glow),transparent_72%)] blur-3xl" />
            </div>
            <CardContent className="relative z-10 p-8 text-center max-[520px]:p-6">
              <div className="grid gap-3">
                <h2 className="text-2xl font-semibold tracking-tight text-foreground max-[520px]:text-xl" id="compact-stage-title">
                  {label}
                </h2>
                <p className="text-sm leading-6 text-muted-foreground">
                  {description}
                </p>
              </div>
            </CardContent>
          </Card>
        </section>
      </div>
    </main>
  );
}
