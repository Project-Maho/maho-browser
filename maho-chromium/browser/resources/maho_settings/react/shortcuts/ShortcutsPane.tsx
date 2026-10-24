import React, {useCallback, useRef, useState} from 'react';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@ui/dialog';
import {Button} from '@ui/button';
import {MahoSettingsStore} from '../store.js';
import {MojoKeyCombo, ShortcutBinding} from '../../mojo.js';
import {useShortcutInterceptor} from '../use_shortcut_interceptor.js';
import {formatKeyCombo, keyComboSearchText} from './key_combo.js';
import {exportShortcutsToFile, importShortcutsFromFile} from './import_export.js';
import {useShortcuts} from './useShortcuts.js';

// Subcomponents
import {ShortcutsSearchBar} from './ShortcutsSearchBar.js';
import {ShortcutsHelpBanner} from './ShortcutsHelpBanner.js';
import {ShortcutsCheatSheet} from './ShortcutsCheatSheet.js';
import {ShortcutsKebabMenu} from './ShortcutsKebabMenu.js';
import {ShortcutRecordingDialog} from './ShortcutRecordingDialog.js';
import {ShortcutsDetailPanel} from './ShortcutsDetailPanel.js';
import {ShortcutsSection} from './ShortcutsSection.js';

interface ShortcutsPaneProps {
  store: MahoSettingsStore;
}

export function ShortcutsPane({store}: ShortcutsPaneProps) {
  const {
    shortcuts,
    loading,
    error,
    reload,
    setShortcut,
    resetShortcut,
    resetAllShortcuts,
    toggleShortcut,
    checkShortcutConflict,
  } = useShortcuts(store);
  const handler = store.getHandler();

  const [searchQuery, setSearchQuery] = useState('');
  // Only the id is held so the detail panel always reflects the reloaded list.
  const [selectedActionId, setSelectedActionId] = useState<string | null>(null);
  const selectedAction = shortcuts.find(b => b.action === selectedActionId) ?? null;
  const selectAction = useCallback((binding: ShortcutBinding) => {
    setSelectedActionId(binding.action);
  }, []);
  const [pendingReset, setPendingReset] = useState<'all' | 'customized' | null>(null);
  const [recordingError, setRecordingError] = useState<string | null>(null);
  const [recordingBinding, setRecordingBinding] = useState<ShortcutBinding | null>(null);
  const [recordedCombo, setRecordedCombo] = useState<MojoKeyCombo | null>(null);
  const [conflictAction, setConflictAction] = useState<string | null>(null);
  const [showCheatSheet, setShowCheatSheet] = useState(false);
  const [showBanner, setShowBanner] = useState(() => {
    return localStorage.getItem('maho_shortcuts_banner_dismissed') !== 'true';
  });
  const [collapsedCategories, setCollapsedCategories] = useState<Record<string, boolean>>({});

  const toggleCategory = (cat: string) => {
    setCollapsedCategories(prev => ({...prev, [cat]: !prev[cat]}));
  };

  const conflictRequestIdRef = useRef(0);

  const handleClear = useCallback(async () => {
    if (!recordingBinding) return;
    const emptyCombo: MojoKeyCombo = {key: '', modifiers: []};
    const {result} = await setShortcut(recordingBinding.action, emptyCombo);
    if (result.success) {
      setRecordingBinding(null);
      setRecordedCombo(null);
      setConflictAction(null);
      setRecordingError(null);
    } else if (result.conflictAction) {
      setConflictAction(result.conflictAction);
    } else {
      setRecordingError('This shortcut could not be cleared. Try again.');
    }
  }, [recordingBinding, setShortcut]);

  useShortcutInterceptor(
    store,
    !!recordingBinding && !conflictAction,
    useCallback((keyCombo) => {
      // If the Mojo callback registers a backspace/delete only, trigger clear
      if ((keyCombo.key === 'backspace' || keyCombo.key === 'delete') && (!keyCombo.modifiers || keyCombo.modifiers.length === 0)) {
        void handleClear();
        return;
      }
      setRecordedCombo(keyCombo);
      setRecordingError(null);
      setConflictAction(null);
      // Only the latest combo's conflict check may commit its result.
      const requestId = ++conflictRequestIdRef.current;
      void (async () => {
        const {conflictAction: conflict} = await checkShortcutConflict(keyCombo);
        if (requestId === conflictRequestIdRef.current) {
          setConflictAction(conflict ?? null);
        }
      })();
    }, [checkShortcutConflict, handleClear])
  );

  const saveShortcut = async () => {
    if (!recordingBinding || !recordedCombo) return;
    const {result} = await setShortcut(recordingBinding.action, recordedCombo);
    if (result.success) {
      setRecordingBinding(null);
      setRecordedCombo(null);
      setConflictAction(null);
      setRecordingError(null);
    } else if (result.conflictAction) {
      setConflictAction(result.conflictAction);
    } else {
      setRecordingError('This shortcut could not be saved. Try another combination.');
    }
  };

  const cancelRecording = () => {
    conflictRequestIdRef.current += 1;
    setRecordingBinding(null);
    setRecordedCombo(null);
    setConflictAction(null);
    setRecordingError(null);
  };

  const tryAgain = () => {
    conflictRequestIdRef.current += 1;
    setConflictAction(null);
    setRecordedCombo(null);
    setRecordingError(null);
  };

  const exportAllShortcuts = async () => {
    await exportShortcutsToFile(handler);
  };

  const importAllShortcuts = async () => {
    await importShortcutsFromFile(
        handler,
        () => reload(),
        message => console.error(message)
    );
  };

  const handleResetCustomized = async () => {
    const customized = shortcuts.filter(b => b.isCustom);
    for (const b of customized) {
      await resetShortcut(b.action);
    }
  };

  const customizedCount = shortcuts.filter(b => b.isCustom).length;

  const confirmReset = async () => {
    const scope = pendingReset;
    setPendingReset(null);
    if (scope === 'all') {
      await resetAllShortcuts();
    } else if (scope === 'customized') {
      await handleResetCustomized();
    }
  };



  const handleCopyShortcut = (binding: ShortcutBinding) => {
    const text = formatKeyCombo(binding.keyCombo);
    void navigator.clipboard.writeText(text);
  };

  const categories = [
    {key: 'navigation', label: 'Navigation'},
    {key: 'tabs', label: 'Tabs'},
    {key: 'spaces', label: 'Spaces'},
    {key: 'window', label: 'Window'},
    {key: 'edit', label: 'Edit'},
    {key: 'view', label: 'View'},
    {key: 'developer', label: 'Developer'},
    {key: 'custom', label: 'Custom'},
  ];

  const searchTerm = keyComboSearchText(searchQuery);
  const filteredShortcuts = shortcuts.filter(binding => {
    const term = searchQuery.toLowerCase();
    const label = (binding.label || '').toLowerCase();
    const action = binding.action.toLowerCase();
    const combo = keyComboSearchText(formatKeyCombo(binding.keyCombo));
    return label.includes(term) || action.includes(term) || combo.includes(searchTerm);
  });

  const customizedShortcuts = filteredShortcuts.filter(b => b.isCustom);

  return (
    <div className="space-y-6">
      {showBanner && (
        <ShortcutsHelpBanner
          onDismiss={() => {
            localStorage.setItem('maho_shortcuts_banner_dismissed', 'true');
            setShowBanner(false);
          }}
        />
      )}

      <div className="flex gap-3 justify-between items-center bg-muted/20 p-3 rounded-lg border border-border/50">
        <ShortcutsSearchBar value={searchQuery} onChange={setSearchQuery} />

        <div className="flex gap-2">
          <button
            type="button"
            onClick={() => setShowCheatSheet(true)}
            className="inline-flex items-center justify-center rounded-md text-sm font-medium transition-colors focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:pointer-events-none disabled:opacity-50 border border-input bg-background shadow-sm hover:bg-surface-hover h-9 px-3"
          >
            Cheat Sheet
          </button>
          <ShortcutsKebabMenu
            onExport={exportAllShortcuts}
            onImport={importAllShortcuts}
            onResetAll={() => setPendingReset('all')}
            onResetCustomized={() => setPendingReset('customized')}
            onShowCheatSheet={() => setShowCheatSheet(true)}
          />
        </div>
      </div>

      {loading ? (
        <div className="text-center py-8 text-muted-foreground">Loading shortcuts...</div>
      ) : error ? (
        <div className="text-center py-8 text-destructive">{error}</div>
      ) : (
        <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
          <div className="md:col-span-2 space-y-4">
            {customizedShortcuts.length > 0 && (
              <ShortcutsSection
                key="customized"
                categoryKey="customized"
                categoryLabel="Customized Shortcuts"
                shortcuts={customizedShortcuts}
                isCollapsed={!!collapsedCategories["customized"]}
                onToggleCollapse={() => toggleCategory("customized")}
                selectedAction={selectedAction}
                onSelectAction={selectAction}
                onEdit={setRecordingBinding}
                onCopy={handleCopyShortcut}
                onReset={resetShortcut}
              />
            )}

            {categories.map(cat => {
              const categoryShortcuts = filteredShortcuts.filter(b => b.category === cat.key);
              if (categoryShortcuts.length === 0) return null;
              const isCollapsed = collapsedCategories[cat.key];

              return (
                <ShortcutsSection
                  key={cat.key}
                  categoryKey={cat.key}
                  categoryLabel={cat.label}
                  shortcuts={categoryShortcuts}
                  isCollapsed={!!isCollapsed}
                  onToggleCollapse={() => toggleCategory(cat.key)}
                  selectedAction={selectedAction}
                  onSelectAction={selectAction}
                  onEdit={setRecordingBinding}
                  onCopy={handleCopyShortcut}
                  onReset={resetShortcut}
                />
              );
            })}
          </div>

          <div>
            <ShortcutsDetailPanel
              selectedAction={selectedAction}
              onEdit={setRecordingBinding}
              onReset={resetShortcut}
              onToggleEnabled={toggleShortcut}
            />
          </div>
        </div>
      )}

      {/* Recording Dialog */}
      <ShortcutRecordingDialog
        open={!!recordingBinding}
        binding={recordingBinding}
        recordedCombo={recordedCombo}
        conflictAction={conflictAction}
        error={recordingError}
        onSave={saveShortcut}
        onCancel={cancelRecording}
        onTryAgain={tryAgain}
        onClear={handleClear}
      />

      {/* Reset confirmation */}
      <Dialog open={pendingReset !== null} onOpenChange={(o) => { if (!o) setPendingReset(null); }}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>
              {pendingReset === 'all' ? 'Reset all shortcuts?' : 'Reset customized shortcuts?'}
            </DialogTitle>
            <DialogDescription>
              {customizedCount === 0
                ? 'All shortcuts already use their defaults.'
                : `${customizedCount} customized ${customizedCount === 1 ? 'shortcut' : 'shortcuts'} will go back to the default. This can't be undone.`}
            </DialogDescription>
          </DialogHeader>
          <DialogFooter className="gap-2">
            <Button variant="secondary" onClick={() => setPendingReset(null)}>Cancel</Button>
            <Button variant="destructive" disabled={customizedCount === 0} onClick={() => { void confirmReset(); }}>
              Reset
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* Cheat Sheet Modal */}
      <ShortcutsCheatSheet
        open={showCheatSheet}
        onOpenChange={setShowCheatSheet}
        shortcuts={shortcuts}
      />
    </div>
  );
}
