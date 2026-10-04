import { Icon } from '../../ui/icon';
import { IconButton } from '../../ui/icon-button';

interface CompactTopbarProps {
  isRunning: boolean;
  onBack?: () => void;
  onNewSession: () => void;
  onOpenSettings: () => void;
}

export function CompactTopbar({
  isRunning,
  onBack,
  onNewSession,
  onOpenSettings,
}: CompactTopbarProps) {
  return (
    <header class="agent-topbar-wrap">
      <div class="agent-topbar" data-testid="agent-topbar">
        <IconButton
          label="Start new session"
          testId="agent-new-session"
          disabled={isRunning}
          onClick={onNewSession}
        >
          <Icon name="plus" size={19} aria-hidden />
        </IconButton>
        <div class="agent-topbar-spacer" />
        <div class="agent-topbar-actions">
          <IconButton label="Open settings" testId="agent-settings" onClick={onOpenSettings}>
            <Icon name="settings" size={18} aria-hidden />
          </IconButton>
          {onBack && (
            <IconButton label="Back" testId="agent-close" onClick={onBack}>
              <Icon name="chevron-left" size={19} aria-hidden />
            </IconButton>
          )}
        </div>
      </div>
    </header>
  );
}
