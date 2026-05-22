import type {AppState} from '../../../types.js';

export function CompactStage({state}: {state: AppState}) {
  const label = state.currentSessionId ? 'Live session ready' : 'Ready when you are';
  const description = state.currentSessionId ?
      'Ask anything about this page to start the conversation.' :
      'Start with a prompt below and Maho AI will open a live session in this thread.';

  return (
    <main className="compact-thread compact-thread--empty">
      <div className="compact-thread__list compact-thread__list--empty">
        <section className="compact-thread__row compact-thread__row--empty" aria-labelledby="compact-stage-title">
          <div className="compact-thread__welcome">
            <div className="compact-thread__welcome-copy">
              <h2 className="compact-thread__welcome-title" id="compact-stage-title">{label}</h2>
              <p className="compact-thread__welcome-text">{description}</p>
            </div>
          </div>
        </section>
      </div>
    </main>
  );
}
