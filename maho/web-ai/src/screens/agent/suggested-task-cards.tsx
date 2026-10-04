import type { SuggestedTask } from './suggested-tasks';

interface SuggestedTaskCardsProps {
  readonly cards: readonly SuggestedTask[];
  /** Throws nothing; start failures surface through the thread error banner. */
  readonly onStart: (task: SuggestedTask) => void;
}

/**
 * Idle suggested-task cards (plan row 14, mobile parity of the desktop row-12
 * panel cards). One button per backend suggestion; clicking starts the task
 * through the SPA's session-create path with the guard / final_confirm /
 * proactive flag set. Hidden entirely when the catalog is empty (suggestion
 * settings disabled).
 */
export function SuggestedTaskCards({ cards, onStart }: SuggestedTaskCardsProps) {
  if (cards.length === 0) {
    return null;
  }

  return (
    <section
      class="agent-suggested-tasks"
      data-testid="agent-suggested-tasks"
      aria-label="Suggested tasks"
    >
      <p class="agent-suggested-title">Suggested tasks</p>
      <div class="agent-suggested-list">
        {cards.map((task) => (
          <button
            key={task.title}
            type="button"
            class="agent-suggested-card"
            data-testid="agent-suggested-task"
            aria-label={`Start suggested task: ${task.title}`}
            onClick={() => onStart(task)}
          >
            <span class="agent-suggested-card-title">{task.title}</span>
            <span class="agent-suggested-card-prompt">{task.prompt}</span>
          </button>
        ))}
      </div>
    </section>
  );
}
