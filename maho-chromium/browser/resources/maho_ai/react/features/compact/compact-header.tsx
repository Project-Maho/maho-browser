import {IconButton} from '../../ui/icon-button.js';
import {OverflowMenu} from './overflow-menu.js';

export function CompactFloatingActions(
    {
      hasMessages,
      onSetDeveloperMode,
      onStartSession,
    }: {
      hasMessages: boolean;
      onSetDeveloperMode: (enabled: boolean) => void;
      onStartSession: () => void;
    }) {
  // `hasMessages` is reserved for future state-aware action visibility.
  return (
    <div className="compact-panel__floating-actions">
      <IconButton
        ariaLabel="Start new session"
        symbol="plus"
        variant="compact-icon-ghost"
        onClick={onStartSession}
      />
      <OverflowMenu
        onNewSession={onStartSession}
        onOpenDeveloperMode={() => onSetDeveloperMode(true)}
      />
    </div>
  );
}

