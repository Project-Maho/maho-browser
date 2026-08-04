import {useEffect, useState} from 'react';
import {Button} from '@ui/button';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from '@ui/dropdown-menu';
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from '@ui/tooltip';
import {Edit, Settings, Grid, X} from '@icons/lucide';
import {ViewMode} from '../../../types.js';
import {ProfileSwitcher} from '../../components/profile-switcher.js';
import {MahoAiStore} from '../../../store.js';

export function CompactTopbar(
    {
      store,
      onClosePanel,
      onOpenSettings,
      onSetViewMode,
      onGetViewMode,
      onStartSession,
    }: {
      store: MahoAiStore;
      onClosePanel: () => void;
      onOpenSettings: () => void;
      onSetViewMode: (mode: ViewMode) => void;
      onGetViewMode: () => Promise<ViewMode>;
      onStartSession: () => void;
    }) {
  const [currentMode, setCurrentMode] = useState<ViewMode>(ViewMode.kSidebar);

  useEffect(() => {
    onGetViewMode().then(mode => {
      setCurrentMode(mode);
    }).catch(err => {
      console.error('[maho-ai] Failed to get view mode:', err);
    });
  }, [onGetViewMode]);

  const handleModeSelect = (mode: ViewMode) => {
    setCurrentMode(mode);
    onSetViewMode(mode);
  };

  const iconButtonClass = 'rounded-full text-muted-foreground hover:bg-surface-hover hover:text-foreground';

  return (
    <div className="flex items-center gap-2 px-1 py-1">
      <div className="flex items-center gap-2">
          <ProfileSwitcher store={store} />
          <Tooltip>
            <TooltipTrigger asChild>
              <Button
                aria-label="Start new session"
                className={iconButtonClass}
                size="icon"
                variant="ghost"
                onClick={onStartSession}>
                <Edit className="size-4" />
              </Button>
            </TooltipTrigger>
            <TooltipContent>New session</TooltipContent>
          </Tooltip>
        </div>

        <div className="min-w-0 flex-1" />

        <div className="flex items-center gap-1">
          <Tooltip>
            <TooltipTrigger asChild>
              <Button
                aria-label="Open settings"
                className={iconButtonClass}
                size="icon"
                variant="ghost"
                onClick={onOpenSettings}>
                <Settings className="size-4" />
              </Button>
            </TooltipTrigger>
            <TooltipContent>Settings</TooltipContent>
          </Tooltip>

          <DropdownMenu>
            <Tooltip>
              <TooltipTrigger asChild>
                <DropdownMenuTrigger asChild>
                  <Button
                    aria-label="View mode"
                    className={iconButtonClass}
                    size="icon"
                    variant="ghost">
                    <Grid className="size-4" />
                  </Button>
                </DropdownMenuTrigger>
              </TooltipTrigger>
              <TooltipContent>View mode</TooltipContent>
            </Tooltip>
            <DropdownMenuContent align="end">
              <DropdownMenuItem
                disabled={currentMode === ViewMode.kSidebar}
                onSelect={() => handleModeSelect(ViewMode.kSidebar)}>
                Sidebar Mode {currentMode === ViewMode.kSidebar && '\u2713'}
              </DropdownMenuItem>
              <DropdownMenuItem
                disabled={currentMode === ViewMode.kFloating}
                onSelect={() => handleModeSelect(ViewMode.kFloating)}>
                Floating {currentMode === ViewMode.kFloating && '\u2713'}
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
                <X className="size-4" />
              </Button>
            </TooltipTrigger>
            <TooltipContent>Close</TooltipContent>
          </Tooltip>
        </div>
      </div>
  );
}
