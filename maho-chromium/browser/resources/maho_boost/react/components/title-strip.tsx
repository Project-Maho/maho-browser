// Copyright 2026 Maho Browser. All rights reserved.

import * as React from 'react';

import {Button} from '@ui/button';
import {cn} from '@lib/utils';
import {
  DropdownMenu,
  DropdownMenuCheckboxItem,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuRadioGroup,
  DropdownMenuRadioItem,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@ui/dropdown-menu';
import {Input} from '@ui/input';
import {
  ArrowUpRight,
  ChevronDown,
  Download,
  Edit,
  RefreshCw,
  Trash2,
  X,
} from '@icons/lucide';

export interface TitleActionAvailability {
  readonly delete: boolean;
  readonly export: boolean;
  readonly import: boolean;
  readonly rename: boolean;
  readonly reset: boolean;
  readonly shuffle: boolean;
}

export interface TitleBoostRow {
  readonly id: string;
  readonly name: string;
}

export interface TitleStripProps {
  readonly activeBoostId: string | null;
  readonly availability: TitleActionAvailability;
  readonly boostName: string;
  readonly boosts: readonly TitleBoostRow[];
  readonly busy?: boolean;
  readonly onClose: () => void;
  readonly onDelete: () => void;
  readonly onExport: () => void;
  readonly onImport: () => void;
  readonly onRename: (name: string) => void;
  readonly onReset: () => void;
  readonly onSelectBoost: (boostId: string) => void;
  readonly onSiteBoostEnabledChange: (enabled: boolean) => void;
  readonly onShuffle: () => void;
  readonly selectedBoostId: string | null;
}

const TITLE_ICON_BUTTON_CLASS =
  'subviewbutton mod-button title-button !h-6 !w-6 shrink-0 rounded-full ' +
  'bg-transparent !p-0 text-[#3a3a3b] opacity-[0.45] shadow-none ' +
  'transition-opacity duration-[250ms] ease-[cubic-bezier(0.075,0.82,0.165,1)] ' +
  'hover:bg-transparent hover:opacity-[0.6] active:transform-none ' +
  'focus-visible:opacity-[0.7] [&_svg]:!size-4 ' +
  '[-webkit-app-region:no-drag] motion-reduce:transition-none';

const NAME_TRIGGER_CLASS =
  'relative ml-1 !h-[26px] min-w-0 max-w-[120px] justify-center gap-0 ' +
  'rounded-[8px] bg-transparent !py-1 !pl-0.5 !pr-[18px] text-[8pt] ' +
  'font-semibold leading-none text-[#3a3a3b] opacity-[0.75] shadow-none ' +
  'transition-[opacity,background-color] duration-[250ms] ' +
  'ease-[cubic-bezier(0.075,0.82,0.165,1)] hover:bg-[#aaa2] ' +
  'hover:opacity-[0.8] focus-visible:opacity-[0.9] ' +
  '[-webkit-app-region:no-drag] motion-reduce:transition-none';

const RENAME_INPUT_CLASS =
  '!h-[26px] w-full max-w-[100px] !rounded-none !border-0 bg-transparent px-0 ' +
  'py-0 text-center text-[8pt] font-semibold leading-none text-[#3a3a3b] ' +
  'opacity-[0.75] shadow-none outline-none [-webkit-app-region:no-drag] ' +
  'focus-visible:!ring-0 focus-visible:shadow-[inset_0_0_0_1px_var(--color-ring)]';

const MENU_ITEM_CLASS = 'h-7 px-2 py-1 text-xs';
const MENU_SELECTION_ITEM_CLASS =
  'h-7 min-w-0 py-1 pl-7 pr-2 text-xs [-webkit-app-region:no-drag]';

export function TitleStrip({
  activeBoostId,
  availability,
  boostName,
  boosts,
  busy = false,
  onClose,
  onDelete,
  onExport,
  onImport,
  onRename,
  onReset,
  onSelectBoost,
  onSiteBoostEnabledChange,
  onShuffle,
  selectedBoostId,
}: TitleStripProps) {
  const [renameValue, setRenameValue] = React.useState(boostName);
  const [renaming, setRenaming] = React.useState(false);
  const [confirmingDelete, setConfirmingDelete] = React.useState(false);
  const nameTriggerRef = React.useRef<HTMLButtonElement | null>(null);
  const renameRef = React.useRef<HTMLInputElement | null>(null);
  const isMacos = document.documentElement.dataset.platform === 'macos';
  const boostIds = new Set<string>();
  const visibleBoosts = boosts.filter(boost => {
    if (boost.id === '' || boostIds.has(boost.id)) {
      return false;
    }
    boostIds.add(boost.id);
    return true;
  });
  const selectedBoostExists = selectedBoostId !== null &&
    visibleBoosts.some(boost => boost.id === selectedBoostId);
  const siteBoostEnabled = activeBoostId !== null;

  React.useEffect(() => {
    if (!renaming) {
      setRenameValue(boostName);
      return;
    }
    renameRef.current?.focus();
    renameRef.current?.select();
  }, [boostName, renaming]);

  const finishRename = (commit: boolean): void => {
    const nextName = renameValue.trim().slice(0, 10);
    setRenaming(false);
    if (commit && !busy && nextName !== '' && nextName !== boostName) {
      onRename(nextName);
    }
  };

  return (
    <header
      id="zen-boost-head-wrapper"
      aria-busy={busy}
      className={cn(
        'flex h-10 min-h-10 max-h-10 w-full shrink-0 items-center border border-[#e7e7e7ab] bg-[#f6f6f8] text-[#3a3a3b] [-webkit-app-region:drag]',
        isMacos ? 'flex-row' : 'flex-row-reverse',
      )}>
      <Button
        id="zen-boost-close"
        aria-label="Close Boost editor"
        className={cn(
          TITLE_ICON_BUTTON_CLASS,
          isMacos ? 'ml-[6px] mr-0' : 'ml-0 mr-[6px]',
        )}
        disabled={busy}
        size="icon"
        title="Close"
        type="button"
        variant="ghost"
        onClick={onClose}>
        <X aria-hidden="true" className="boost-control-icon size-4" />
      </Button>

      <div
        id="zen-boost-name"
        className="flex h-[26px] min-w-0 flex-1 items-center justify-center bg-transparent text-[8pt] font-semibold text-[#3a3a3b] [-webkit-app-region:drag]">
        {renaming ? (
          <Input
            ref={renameRef}
            id="zen-boost-name-container"
            aria-label="Rename Boost"
            className={RENAME_INPUT_CLASS}
            disabled={busy}
            maxLength={10}
            value={renameValue}
            onBlur={() => finishRename(true)}
            onChange={event => setRenameValue(event.currentTarget.value)}
            onKeyDown={event => {
              if (event.key === 'Enter') {
                event.preventDefault();
                finishRename(true);
              } else if (event.key === 'Escape') {
                event.preventDefault();
                finishRename(false);
                window.requestAnimationFrame(() => nameTriggerRef.current?.focus());
              }
            }}
          />
        ) : (
          <DropdownMenu
            onOpenChange={open => {
              if (!open) {
                setConfirmingDelete(false);
              }
            }}>
            <DropdownMenuTrigger asChild>
              <Button
                ref={nameTriggerRef}
                id="zen-boost-name-container"
                aria-label={`${boostName} Boost actions`}
                className={NAME_TRIGGER_CLASS}
                disabled={busy}
                size="sm"
                type="button"
                variant="ghost">
                <span
                  id="zen-boost-name-text"
                  className="block h-min w-full max-w-[100px] min-w-0 truncate text-center">
                  {boostName}
                </span>
                <ChevronDown
                  aria-hidden="true"
                  className="boost-control-icon absolute right-0.5 top-[calc(50%_+_2px)] size-3 -translate-y-1/2 shrink-0 text-[#3a3a3b] opacity-75"
                />
              </Button>
            </DropdownMenuTrigger>
            <DropdownMenuContent
              id="zenBoostContextMenu"
              aria-label="Boosts and actions"
              align="center"
              className="max-h-[calc(100vh-16px)] w-[168px] max-w-[calc(100vw-16px)] min-w-0 overflow-y-auto text-xs [-webkit-app-region:no-drag] motion-reduce:animate-none motion-reduce:transition-none"
              collisionPadding={8}
              sideOffset={4}>
              <DropdownMenuCheckboxItem
                id="zen-boost-site-toggle"
                aria-label="Apply Boost on this site"
                checked={siteBoostEnabled}
                className={MENU_SELECTION_ITEM_CLASS}
                disabled={busy || (!siteBoostEnabled && !selectedBoostExists)}
                onCheckedChange={enabled => {
                  const nextEnabled = enabled === true;
                  if (nextEnabled !== siteBoostEnabled &&
                      (!nextEnabled || selectedBoostExists)) {
                    onSiteBoostEnabledChange(nextEnabled);
                  }
                }}
                onSelect={event => event.preventDefault()}>
                <span className="min-w-0 flex-1 truncate">Site Boosts</span>
                <span aria-hidden="true" className="ml-auto shrink-0 text-[8pt] opacity-60">
                  {siteBoostEnabled ? 'On' : 'Off'}
                </span>
              </DropdownMenuCheckboxItem>

              <DropdownMenuRadioGroup
                id="zen-boost-list"
                aria-label="Boost to edit"
                className="max-h-[196px] overflow-y-auto overscroll-contain"
                value={selectedBoostId ?? ''}
                onValueChange={onSelectBoost}>
                {visibleBoosts.map(boost => {
                  const isActive = boost.id === activeBoostId;
                  const isSelected = boost.id === selectedBoostId;
                  const stateName = [
                    isSelected ? 'selected for editing' : '',
                    isActive ? 'active on this site' : '',
                  ].filter(Boolean).join(', ');
                  return (
                    <DropdownMenuRadioItem
                      key={boost.id}
                      aria-current={isActive ? 'true' : undefined}
                      aria-label={`${boost.name}${stateName === '' ? '' : `, ${stateName}`}`}
                      className={MENU_SELECTION_ITEM_CLASS}
                      data-active={isActive}
                      data-boost-id={boost.id}
                      data-selected={isSelected}
                      disabled={busy}
                      value={boost.id}>
                      <span className="min-w-0 flex-1 truncate">{boost.name}</span>
                      {(isActive || isSelected) && (
                        <span
                          aria-hidden="true"
                          className="ml-auto shrink-0 text-[8pt] opacity-60">
                          {isActive ? 'Active' : 'Selected'}
                        </span>
                      )}
                    </DropdownMenuRadioItem>
                  );
                })}
              </DropdownMenuRadioGroup>
              <DropdownMenuSeparator />

              <DropdownMenuItem
                id="zen-boost-edit-rename"
                className={MENU_ITEM_CLASS}
                disabled={!availability.rename || busy}
                onSelect={() => setRenaming(true)}>
                <Edit aria-hidden="true" className="boost-control-icon size-4" />
                Rename Boost
              </DropdownMenuItem>
              <DropdownMenuItem
                id="zen-boost-edit-shuffle"
                className={MENU_ITEM_CLASS}
                disabled={!availability.shuffle || busy}
                onSelect={onShuffle}>
                <RefreshCw aria-hidden="true" className="boost-control-icon size-4" />
                Shuffle Vibes
              </DropdownMenuItem>
              <DropdownMenuItem
                id="zen-boost-edit-reset"
                className={MENU_ITEM_CLASS}
                disabled={!availability.reset || busy}
                onSelect={onReset}>
                <RefreshCw aria-hidden="true" className="boost-control-icon size-4" />
                Reset All Edits
              </DropdownMenuItem>
              <DropdownMenuSeparator />
              <DropdownMenuItem
                id="zen-boost-load"
                className={MENU_ITEM_CLASS}
                disabled={!availability.import || busy}
                onSelect={onImport}>
                <Download aria-hidden="true" className="boost-control-icon size-4" />
                Import Boost
              </DropdownMenuItem>
              <DropdownMenuItem
                id="zen-boost-save"
                className={MENU_ITEM_CLASS}
                disabled={!availability.export || busy}
                onSelect={onExport}>
                <ArrowUpRight aria-hidden="true" className="boost-control-icon size-4" />
                Export Boost
              </DropdownMenuItem>
              <DropdownMenuSeparator />
              {confirmingDelete ? (
                <>
                  <DropdownMenuItem
                    id="zen-boost-edit-delete-confirm"
                    className={`${MENU_ITEM_CLASS} text-destructive focus:text-destructive`}
                    disabled={!availability.delete || busy}
                    onSelect={() => {
                      setConfirmingDelete(false);
                      onDelete();
                    }}>
                    <Trash2 aria-hidden="true" className="boost-control-icon size-4" />
                    Delete permanently
                  </DropdownMenuItem>
                  <DropdownMenuItem
                    id="zen-boost-edit-delete-cancel"
                    className={MENU_ITEM_CLASS}
                    disabled={busy}
                    onSelect={() => setConfirmingDelete(false)}>
                    Keep Boost
                  </DropdownMenuItem>
                </>
              ) : (
                <DropdownMenuItem
                  id="zen-boost-edit-delete"
                  className={`${MENU_ITEM_CLASS} text-destructive focus:text-destructive`}
                  disabled={!availability.delete || busy}
                  // Deleting a Boost cannot be undone, so require a second,
                  // explicit confirmation instead of acting on the first click.
                  onSelect={event => {
                    event.preventDefault();
                    setConfirmingDelete(true);
                  }}>
                  <Trash2 aria-hidden="true" className="boost-control-icon size-4" />
                  Delete Boost
                </DropdownMenuItem>
              )}
            </DropdownMenuContent>
          </DropdownMenu>
        )}
      </div>
    </header>
  );
}
