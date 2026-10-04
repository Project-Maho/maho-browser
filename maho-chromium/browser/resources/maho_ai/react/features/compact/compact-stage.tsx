import {EmptyState} from '../../components/empty-state.js';

export function CompactStage() {
  return (
    <main
        aria-label="Empty conversation"
        className="flex min-h-0 min-w-0 flex-1 items-center justify-center overflow-hidden">
      <EmptyState
        className="sr-only"
        description="Maho can read this page, answer questions, and take actions for you."
        title="Ask Maho to act on this page" />
    </main>
  );
}
