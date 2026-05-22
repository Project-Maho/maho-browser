import {cn} from '../../lib/cn.js';
import {Icon} from '../../ui/icon.js';
import {IconButton} from '../../ui/icon-button.js';
import {OverflowMenu} from './overflow-menu.js';

export function CompactHeader(
    {
      hasMessages,
      onSetDeveloperMode,
      onStartSession,
    }: {
      hasMessages: boolean;
      onSetDeveloperMode: (enabled: boolean) => void;
      onStartSession: () => void;
    }) {
  return (
    <header className={cn(
        'compact-panel__header',
        !hasMessages && 'compact-panel__header--empty')}>
      <div className="compact-panel__brand compact-panel__brand--quiet">
        <div className="compact-panel__brand-mark">
          <Icon symbol="panelSpark" />
        </div>
        <span className="compact-panel__quiet-title">Maho AI</span>
      </div>
      <div className="compact-panel__header-actions compact-panel__header-actions--quiet">
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
    </header>
  );
}
