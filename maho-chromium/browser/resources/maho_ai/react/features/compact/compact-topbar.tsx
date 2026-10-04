import {useRef} from 'react';
import {Button} from '@ui/button';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuPortal,
  DropdownMenuSub,
  DropdownMenuSubContent,
  DropdownMenuSubTrigger,
  DropdownMenuTrigger,
} from '@ui/dropdown-menu';
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from '@ui/tooltip';
import {
  ChevronRight,
  Edit,
  MoreHorizontal,
  History,
  Lock,
  RefreshCw,
  Settings,
  X,
} from '@icons/lucide';
import {RuntimeConnectionState} from '../../../maho_ai.mojom-webui.js';
import {cn} from '@lib/utils';
import type {ViewMode, ConnectionState} from '../../../types.js';
import {
  getConnectionStateLabel,
  getConnectionStateTone,
  getSessionTitle,
  isSessionReadOnly,
} from '../../../types.js';
import {useAppState} from '../../hooks/use-app-state.js';
import {ProfileSwitcher} from '../../components/profile-switcher.js';
import {MahoAiStore} from '../../../store.js';

const CONNECTION_DOT_CLASS: Record<string, string> = {
  success: 'bg-success',
  danger: 'bg-destructive',
  warning: 'bg-warning',
};

// Connected, Resumed and the transient Connecting handshake render no visual
// chrome; only a state the user must act on earns pixels. The status region
// stays mounted either way for screen readers.
function ConnectionIndicator({state}: {state: ConnectionState}) {
  const label = getConnectionStateLabel(state);
  const quiet = state === RuntimeConnectionState.kConnected ||
      state === RuntimeConnectionState.kResumed ||
      state === RuntimeConnectionState.kConnecting;
  const dotClassName =
      CONNECTION_DOT_CLASS[getConnectionStateTone(state)] ?? 'bg-border';
  return (
    <span
      aria-live="polite"
      className={cn(
          'min-w-0 shrink',
          quiet
              ? 'sr-only'
              : 'inline-flex h-7 items-center gap-1.5 rounded-full px-1.5 text-[11px] font-medium text-muted-foreground')}
      data-connection-state={label}
      role="status"
      title={label}>
      {quiet ? null : (
        <span
          aria-hidden="true"
          className={cn('size-[5px] shrink-0 rounded-full', dotClassName)} />
      )}
      <span className="truncate tracking-[-0.005em]" data-connection-label>
        {label}
      </span>
    </span>
  );
}

export function CompactTopbar(
    {
      store,
      onClosePanel,
      onOpenSettings,
      onSetViewMode: _onSetViewMode,
      onGetViewMode: _onGetViewMode,
      onOpenRoutines = () => {},
      onResumeSession,
      routinesActive = false,
      onStartSession,
    }: {
      store: MahoAiStore;
      onClosePanel: () => void;
      onOpenSettings: () => void;
      onSetViewMode: (mode: ViewMode) => void;
      onGetViewMode: () => Promise<ViewMode>;
      onOpenRoutines?: () => void;
      onResumeSession?: (sessionId: string) => void;
      routinesActive?: boolean;
      onStartSession: () => void;
    }) {
  const state = useAppState(store);
  const moreTriggerRef = useRef<HTMLButtonElement>(null);
  const iconButtonClass =
      'size-7 shrink-0 rounded-lg text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground';
  const {sessionsById, sessionOrder, currentSessionId} = state;
  const currentSession =
      currentSessionId ? sessionsById[currentSessionId] : null;
  const historySessionIds = sessionOrder.filter(
      sessionId => sessionId !== currentSessionId);

  return (
    <div
      className="flex min-w-0 items-center gap-0.5 [container-type:inline-size]"
      data-compact-topbar>
      <ProfileSwitcher store={store} />
      <ConnectionIndicator state={state.connectionState} />

      <div className="ml-auto flex shrink-0 items-center gap-0.5">
        {currentSession && isSessionReadOnly(currentSession) ? (
          <span
            aria-label="Read-only session"
            className="inline-flex size-7 shrink-0 items-center justify-center rounded-lg text-muted-foreground"
            data-readonly-badge
            role="status">
            <Lock aria-hidden="true" className="size-3.5 shrink-0" />
            <span className="sr-only">Read-only</span>
          </span>
        ) : null}

        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              aria-label="Start new session"
              className={iconButtonClass}
              size="icon"
              variant="ghost"
              onClick={onStartSession}>
              <Edit className="size-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>New session</TooltipContent>
        </Tooltip>

        <DropdownMenu>
          <Tooltip>
            <TooltipTrigger asChild>
              <DropdownMenuTrigger asChild>
                <Button
                  aria-label="More actions"
                  className={iconButtonClass}
                  ref={moreTriggerRef}
                  size="icon"
                  variant={routinesActive ? 'secondary' : 'ghost'}>
                  <MoreHorizontal className="size-3.5" />
                </Button>
              </DropdownMenuTrigger>
            </TooltipTrigger>
            <TooltipContent>More</TooltipContent>
          </Tooltip>
          <DropdownMenuContent
            align="end"
            className="w-48 rounded-xl border-border/80 bg-popover p-1.5 shadow-[var(--shadow-raised)]"
            onCloseAutoFocus={event => {
              event.preventDefault();
              moreTriggerRef.current?.focus();
            }}>
            <DropdownMenuSub>
              <DropdownMenuSubTrigger
                className="min-h-8 gap-2 rounded-lg px-2.5 py-1.5 text-xs"
                data-topbar-history-submenu>
                <History className="size-4" />
                <span>History</span>
                <ChevronRight aria-hidden="true" className="ml-auto size-3.5" />
              </DropdownMenuSubTrigger>
              <DropdownMenuPortal>
                <DropdownMenuSubContent
                  className="max-h-72 w-56 overflow-y-auto rounded-xl border-border/80 bg-popover p-1.5 shadow-[var(--shadow-raised)]">
                  {historySessionIds.length === 0 ? (
                    <DropdownMenuItem className="min-h-8 rounded-lg px-2.5 py-1.5 text-xs" disabled>
                      No session history
                    </DropdownMenuItem>
                  ) : historySessionIds.map(sessionId => {
                    const session = sessionsById[sessionId];
                    if (!session) {
                      return null;
                    }
                    return (
                      <DropdownMenuItem
                        className="flex min-h-8 cursor-pointer items-center justify-between gap-2 rounded-lg px-2.5 py-1.5 text-xs transition-colors hover:bg-surface-hover"
                        data-history-session={sessionId}
                        key={sessionId}
                        onSelect={() => onResumeSession?.(sessionId)}>
                        <span className={cn(
                            'min-w-0 truncate',
                            sessionId === currentSessionId && 'font-semibold text-primary')}>
                          {getSessionTitle(session)}
                        </span>
                        {isSessionReadOnly(session) ? (
                          <span className="shrink-0 text-[10px] font-medium uppercase tracking-wide text-muted-foreground">
                            Read-only
                          </span>
                        ) : null}
                      </DropdownMenuItem>
                    );
                  })}
                </DropdownMenuSubContent>
              </DropdownMenuPortal>
            </DropdownMenuSub>
            <DropdownMenuItem
              className="min-h-8 gap-2 rounded-lg px-2.5 py-1.5 text-xs"
              data-topbar-action="Open Routines"
              onSelect={() => onOpenRoutines()}>
              <RefreshCw className="size-4" />
              Open Routines
            </DropdownMenuItem>
            <DropdownMenuItem
              className="min-h-8 gap-2 rounded-lg px-2.5 py-1.5 text-xs"
              data-topbar-action="Open settings"
              onSelect={() => onOpenSettings()}>
              <Settings className="size-4" />
              Open settings
            </DropdownMenuItem>
          </DropdownMenuContent>
        </DropdownMenu>

        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              aria-label="Close panel"
              className={iconButtonClass}
              size="icon"
              variant="ghost"
              onClick={onClosePanel}>
              <X className="size-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>Close</TooltipContent>
        </Tooltip>
      </div>
    </div>
  );
}
