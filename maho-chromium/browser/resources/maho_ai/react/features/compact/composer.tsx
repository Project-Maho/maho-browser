import type {AppState} from '../../../types.js';
import {isSessionReadOnly} from '../../../types.js';
import {cn} from '../../lib/cn.js';
import {Button} from '../../ui/button.js';
import {Icon} from '../../ui/icon.js';
import {IconButton} from '../../ui/icon-button.js';
import {ComposerTextarea} from '../../ui/textarea.js';

export interface ComposerHandlers {
  onCancel: () => void;
  onPromptChange: (value: string) => void;
  onStartSession: () => void;
  onSubmit: () => void;
  onToggleBrowserContext: () => void;
}

export function Composer({handlers, state}: {handlers: ComposerHandlers; state: AppState}) {
  const session = state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;
  const readOnly = isSessionReadOnly(session);

  const canCancel =
      !readOnly && !!state.currentSessionId &&
      !!state.turnPendingBySessionId[state.currentSessionId];

  return (
    <section className={cn(
        'compact-composer',
        'compact-composer--docked',
        readOnly && 'compact-composer--read-only')}>
      <div className="compact-composer__body">
        <ComposerTextarea
          onSubmit={handlers.onSubmit}
          onValueChange={handlers.onPromptChange}
          placeholder={readOnly ?
            'This archived session is view-only. Start a new live session to continue.' :
            'Ask anything about this page…'}
          readOnly={readOnly}
          rows={3}
          value={state.composer.prompt}
        />
      </div>
      <div className="compact-composer__footer">
        <div className="compact-composer__footer-actions">
          <IconButton
            ariaLabel="Start new session"
            symbol="plus"
            variant="composer-icon"
            onClick={handlers.onStartSession}
          />
          <IconButton
            ariaLabel="Cancel current turn"
            disabled={!canCancel}
            symbol="stop"
            variant="composer-icon"
            onClick={handlers.onCancel}
          />
          <IconButton
            ariaLabel={state.composer.attachBrowserContext ?
              'Page context on — click to disable' :
              'Page context off — click to enable'}
            className={cn(
                state.composer.attachBrowserContext &&
                    'compact-composer__icon-button--context-on')}
            disabled={readOnly}
            symbol="globe"
            variant="composer-icon"
            onClick={handlers.onToggleBrowserContext}
          />
          <span className="compact-composer__hint">
            {readOnly ?
              'Start a new live session to continue' :
              (state.currentSessionId ?
                'Enter send · Shift+Enter newline' :
                'Enter start · Shift+Enter newline')}
          </span>
        </div>
        <Button
          aria-label={state.currentSessionId ? 'Send prompt' : 'Start session'}
          disabled={readOnly || !state.composer.prompt.trim()}
          variant="composer-send"
          onClick={handlers.onSubmit}>
          <span className="compact-composer__send-icon">
            <Icon symbol="composerSpark" />
          </span>
          <span className="compact-composer__send-label">
            {state.currentSessionId ? 'Send' : 'Start'}
          </span>
        </Button>
      </div>
    </section>
  );
}
