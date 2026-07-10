import React, {useEffect, useState} from 'react';
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
import {Edit, Sparkles, Settings, Grid, X} from '@icons/lucide';
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
      onTidyTabs,
    }: {
      store: MahoAiStore;
      onClosePanel: () => void;
      onOpenSettings: () => void;
      onSetViewMode: (mode: ViewMode) => void;
      onGetViewMode: () => Promise<ViewMode>;
      onStartSession: () => void;
      onTidyTabs?: () => void;
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

  const iconButtonClass = 'rounded-full';

  return (
    <div className="flex items-center gap-2 rounded-[1.25rem] border border-border/80 bg-card/90 px-3 py-2 shadow-[0_20px_60px_-48px_rgba(8,12,24,0.85)] backdrop-blur-xl supports-[backdrop-filter]:bg-card/85">
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
          {onTidyTabs && (
            <Tooltip>
              <TooltipTrigger asChild>
                <Button
                  aria-label="Tidy tabs"
                  className={iconButtonClass}
                  size="icon"
                  variant="ghost"
                  onClick={onTidyTabs}>
                  <Sparkles className="size-4" />
                </Button>
              </TooltipTrigger>
              <TooltipContent>Tidy tabs</TooltipContent>
            </Tooltip>
          )}
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
