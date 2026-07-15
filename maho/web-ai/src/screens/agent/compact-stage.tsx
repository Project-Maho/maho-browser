interface CompactStageProps {
  hasSession: boolean;
}

export function CompactStage({ hasSession }: CompactStageProps) {
  return (
    <main class="agent-stage-shell" data-testid="agent-stage">
      <section class="agent-stage-card" aria-labelledby="agent-stage-title">
        <div class="agent-stage-glow" aria-hidden="true" />
        <div class="agent-stage-content">
          <h1 class="agent-stage-title" id="agent-stage-title">
            {hasSession ? 'Live session ready' : 'Ready when you are'}
          </h1>
          <p class="agent-stage-description">
            {hasSession
              ? 'Ask anything about this page to start.'
              : 'Describe a task and Maho Agent will run it here.'}
          </p>
          <p class="agent-stage-hint">Use the composer below to start.</p>
        </div>
      </section>
    </main>
  );
}
