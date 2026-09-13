// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import {
  AlertCircle,
  Check,
  Download,
  KeyRound,
  RefreshCw,
  ShieldCheck,
  Sparkles,
} from 'lucide-react';

import {Alert, AlertDescription, AlertTitle} from '@ui/alert';
import {Button} from '@ui/button';
import type {MahoWelcomeStore} from '../store.js';
import type {WelcomeState} from '../types.js';

interface SyncKeyBackupProps {
  readonly snapshot: WelcomeState;
  readonly store: MahoWelcomeStore;
  readonly onSaveFile?: (content: string, filename: string) => Promise<boolean> | boolean;
  readonly onGenerateSyncKey?: () => Promise<{syncKey: string; roomId: string; recoveryPhrase: string} | null>;
  readonly onRegisterSaveHandler?: (handler: (() => Promise<void>) | null) => void;
}

export function SyncKeyBackupSidebar({store}: {store: MahoWelcomeStore}) {
  return (
    <>
      <div className="flex size-11 items-center justify-center rounded-xl border border-border bg-background/70 text-foreground shadow-sm">
        <KeyRound className="size-5 text-primary" aria-hidden="true" />
      </div>
      <h1 className="text-balance">
        {store.getString('IDS_MAHO_WELCOME_SYNC_BACKUP_TITLE') || 'Back up your sync recovery key'}
      </h1>
      <p className="text-pretty">
        {store.getString('IDS_MAHO_WELCOME_SYNC_BACKUP_BODY') || 'Your sync key encrypts your data across devices. Save your recovery phrase to continue.'}
      </p>
    </>
  );
}

export function SyncKeyBackupContent({
  snapshot,
  store,
  onSaveFile,
  onGenerateSyncKey,
  onRegisterSaveHandler,
}: SyncKeyBackupProps) {
  const {syncKeyBackup} = snapshot;
  const [localPhrase, setLocalPhrase] = useState<string | null>(syncKeyBackup.recoveryPhrase);
  const [localRoomId, setLocalRoomId] = useState<string | null>(syncKeyBackup.roomId);
  const [localSyncKey, setLocalSyncKey] = useState<string | null>(syncKeyBackup.syncKey);
  const [isGenerating, setIsGenerating] = useState(false);
  const [isSaving, setIsSaving] = useState(false);
  const [saveError, setSaveError] = useState<string | null>(null);

  const phraseRef = useRef<string | null>(null);
  const savePendingRef = useRef(false);

  const words = useMemo(() => {
    const phrase = localPhrase || syncKeyBackup.recoveryPhrase;
    if (!phrase) return [];
    return phrase.trim().split(/\s+/).filter(Boolean);
  }, [localPhrase, syncKeyBackup.recoveryPhrase]);

  const activeRoomId = localRoomId || syncKeyBackup.roomId;
  const activeSyncKey = localSyncKey || syncKeyBackup.syncKey;
  const isSaved = syncKeyBackup.savedToFile;

  useEffect(() => {
    phraseRef.current = localPhrase || syncKeyBackup.recoveryPhrase;
  }, [localPhrase, syncKeyBackup.recoveryPhrase]);

  // Scrub phrase on unmount
  useEffect(() => {
    return () => {
      phraseRef.current = null;
      setLocalPhrase(null);
      setLocalRoomId(null);
      setLocalSyncKey(null);
    };
  }, []);

  const handleGenerate = useCallback(async () => {
    setIsGenerating(true);
    setSaveError(null);
    try {
      if (onGenerateSyncKey) {
        const res = await onGenerateSyncKey();
        if (res) {
          setLocalPhrase(res.recoveryPhrase);
          setLocalRoomId(res.roomId);
          setLocalSyncKey(res.syncKey);
        }
      } else {
        setSaveError('Sync key generation is unavailable in this preview.');
      }
    } catch (err) {
      setSaveError(err instanceof Error ? err.message : 'Failed to generate sync key.');
    } finally {
      setIsGenerating(false);
    }
  }, [onGenerateSyncKey]);

  // Auto generate if empty on load
  useEffect(() => {
    if (words.length === 0 && !isGenerating && syncKeyBackup.status === 'idle') {
      void handleGenerate();
    }
  }, [words.length, isGenerating, syncKeyBackup.status, handleGenerate]);

  const handleDownload = useCallback(async () => {
    if (savePendingRef.current) return;
    savePendingRef.current = true;
    setIsSaving(true);
    setSaveError(null);
    const phraseToSave = localPhrase || syncKeyBackup.recoveryPhrase || '';
    const roomIdToSave = activeRoomId || '';

    const content = `====================================================
MAHO BROWSER SYNC RECOVERY KEY BACKUP
====================================================

IMPORTANT SECURITY NOTICE:
Keep this file in a safe, offline place.
This recovery phrase allows you to decrypt your synced
browser data on another Maho Browser installation.

Room ID: ${roomIdToSave}

Recovery Phrase:
${phraseToSave}

====================================================
Generated: ${new Date().toISOString()}
====================================================
`;
    const filename = `maho-sync-key-backup-${roomIdToSave || 'recovery'}.txt`;

    try {
      let success = false;
      if (onSaveFile) {
        success = await onSaveFile(content, filename);
      } else {
        success = await store.saveSyncKeyBackup(content);
      }

      if (success) {
        store.setSyncKeyBackupSavedToFile(true);
      } else {
        setSaveError('File download was cancelled or failed. Please try saving again.');
        store.setSyncKeyBackupSavedToFile(false);
      }
    } catch (err) {
      setSaveError(err instanceof Error ? err.message : 'Failed to save recovery phrase file.');
      store.setSyncKeyBackupSavedToFile(false);
    } finally {
      savePendingRef.current = false;
      setIsSaving(false);
    }
  }, [localPhrase, syncKeyBackup.recoveryPhrase, activeRoomId, onSaveFile, store]);

  useEffect(() => {
    if (onRegisterSaveHandler) {
      if (words.length > 0 && !isSaving) {
        onRegisterSaveHandler(handleDownload);
      } else {
        onRegisterSaveHandler(null);
      }
    }
    return () => {
      if (onRegisterSaveHandler) {
        onRegisterSaveHandler(null);
      }
    };
  }, [onRegisterSaveHandler, handleDownload, words.length, isSaving]);

  const errorMessage = saveError || syncKeyBackup.errorMessage;

  return (
    <div className="flex h-full w-full max-w-lg flex-col justify-center p-1 text-foreground">
      <div className="min-h-0 flex-1 space-y-4 overflow-y-auto pr-1 pb-4">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2.5">
            <div className="flex size-8 items-center justify-center rounded-lg border border-border/40 bg-primary/10 text-primary">
              <ShieldCheck className="size-4" aria-hidden="true" />
            </div>
            <h2 className="text-sm font-semibold leading-none tracking-tight text-foreground">
              Cross-Device Encryption Key
            </h2>
          </div>
          {activeRoomId ? (
            <span className="font-mono text-[10px] bg-muted/60 px-2 py-0.5 rounded text-muted-foreground">
              Room: {activeRoomId.slice(0, 12)}…
            </span>
          ) : null}
        </div>
        <p className="text-pretty text-xs leading-5 text-muted-foreground">
          This private key decrypts your bookmarks, history, and tabs across devices. Maho servers cannot read your phrase.
        </p>

        {words.length > 0 ? (
          <section
            className="grid grid-cols-3 gap-2 rounded-xl border border-border/70 bg-muted/30 p-3"
            aria-label="Recovery phrase word grid"
          >
            {words.map((word, idx) => (
              <div
                key={`${idx}-${word}`}
                className="flex items-center gap-2 rounded-md border border-border/40 bg-background/80 px-2.5 py-1.5 shadow-xs"
              >
                <span className="w-4 select-none font-mono text-[11px] font-semibold text-muted-foreground/70">
                  {idx + 1}.
                </span>
                <span className="font-mono text-xs font-medium text-foreground tracking-tight select-all">
                  {word}
                </span>
              </div>
            ))}
          </section>
        ) : (
          <div className="flex h-32 items-center justify-center rounded-xl border border-dashed border-border bg-muted/20 p-4 text-center">
            {isGenerating ? (
              <p className="text-xs text-muted-foreground flex items-center gap-2">
                <RefreshCw className="size-3.5 animate-spin" /> Generating secure recovery phrase…
              </p>
            ) : (
              <div className="space-y-2">
                <p className="text-xs text-muted-foreground">No recovery phrase generated yet.</p>
                <Button
                  type="button"
                  variant="outline"
                  size="sm"
                  onClick={() => { void handleGenerate(); }}
                >
                  Generate Sync Key
                </Button>
              </div>
            )}
          </div>
        )}

        {isSaved ? (
          <div className="flex items-center gap-2.5 rounded-lg bg-emerald-500/10 p-3 text-xs font-medium text-emerald-600 dark:text-emerald-400 border border-emerald-500/20">
            <Check className="size-4 shrink-0 stroke-[2.5]" />
            <span>Recovery key backup file saved successfully! You may now start syncing.</span>
          </div>
        ) : (
          <div className="flex items-center gap-2 text-xs text-muted-foreground bg-muted/40 p-3 rounded-lg border border-border/50">
            <Sparkles className="size-4 shrink-0 text-amber-500" />
            <span>Save file is mandatory to enable sync. Click "Save File (.txt)" in the footer to unlock "Start syncing".</span>
          </div>
        )}

        {errorMessage ? (
          <Alert variant="destructive" aria-live="assertive">
            <AlertCircle className="size-4" />
            <AlertTitle>Save file incomplete</AlertTitle>
            <AlertDescription>{errorMessage}</AlertDescription>
          </Alert>
        ) : null}
      </div>
    </div>
  );
}
