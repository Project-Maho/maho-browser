import React, {useEffect, useState} from 'react';
import {IconButton} from '../../ui/icon-button.js';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from '../../ui/dropdown-menu.js';
import {ViewMode} from '../../../types.js';

export function CompactTopbar(
    {
      onClosePanel,
      onOpenSettings,
      onSetViewMode,
      onGetViewMode,
      onStartSession,
    }: {
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

  return (
    <div className="compact-panel__topbar">
      <div className="compact-panel__topbar-left">
        <IconButton
          ariaLabel="Start new session"
          symbol="edit"
          variant="compact-icon-ghost"
          onClick={onStartSession}
        />
      </div>
      <div className="compact-panel__topbar-right">
        <IconButton
          ariaLabel="Open settings"
          symbol="settingsGear"
          variant="compact-icon-ghost"
          onClick={onOpenSettings}
        />

        <DropdownMenu>
          <DropdownMenuTrigger asChild>
            <IconButton
              ariaLabel="View mode"
              symbol="viewModeToggle"
              variant="compact-icon-ghost"
            />
          </DropdownMenuTrigger>
          <DropdownMenuContent>
            <DropdownMenuItem
              disabled={currentMode === ViewMode.kSidebar}
              onSelect={() => handleModeSelect(ViewMode.kSidebar)}>
              Sidebar Mode {currentMode === ViewMode.kSidebar && '✓'}
            </DropdownMenuItem>
            <DropdownMenuItem
              disabled={currentMode === ViewMode.kFloating}
              onSelect={() => handleModeSelect(ViewMode.kFloating)}>
              Floating {currentMode === ViewMode.kFloating && '✓'}
            </DropdownMenuItem>
          </DropdownMenuContent>
        </DropdownMenu>

        <IconButton
          ariaLabel="Close panel"
          symbol="xmark"
          variant="compact-icon-ghost"
          onClick={onClosePanel}
        />
      </div>
    </div>
  );
}
