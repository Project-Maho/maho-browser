// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';
import {Plus, Trash2, Edit2, Loader2} from 'lucide-react';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {PaneShell, SectionCard} from './domain_panes.js';
import type {PaneDefinition} from '../models.js';
import type {MahoSettingsStore} from './store.js';

interface Signature {
  id: string;
  account_id?: string;
  name: string;
  content_html: string;
  is_default: boolean;
}

export function MailSignaturesPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const router = store.getCallbackRouter();
  const mountedRef = useRef(true);
  const retryCountRef = useRef(0);
  const retryTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const loadRef = useRef<() => void>(() => {});
  const [signatures, setSignatures] = useState<Signature[]>([]);
  const [loading, setLoading] = useState(true);
  const [editingSig, setEditingSig] = useState<Partial<Signature> | null>(null);
  const [error, setError] = useState('');
  const [loadError, setLoadError] = useState('');

  useEffect(() => {
    return () => {
      mountedRef.current = false;
      if (retryTimerRef.current !== null) {
        clearTimeout(retryTimerRef.current);
        retryTimerRef.current = null;
      }
    };
  }, []);

  // Returns true while a bounded retry is pending (≤3), false once the cap is hit.
  const scheduleRetry = useCallback((): boolean => {
    if (retryCountRef.current >= 3) {
      return false;
    }
    retryCountRef.current += 1;
    if (retryTimerRef.current !== null) {
      clearTimeout(retryTimerRef.current);
    }
    retryTimerRef.current = setTimeout(() => {
      retryTimerRef.current = null;
      if (mountedRef.current) {
        loadRef.current();
      }
    }, 800);
    return true;
  }, []);

  const loadSignatures = useCallback(async () => {
    setLoading(true);
    try {
      const {ok, resultJson} = await handler.mailListSignatures(null);
      if (!mountedRef.current) return;
      if (ok) {
        setSignatures(JSON.parse(resultJson) as Signature[]);
        setLoadError('');
        retryCountRef.current = 0;
        setLoading(false);
      } else if (!scheduleRetry()) {
        setLoadError('Failed to load signatures');
        setLoading(false);
      }
    } catch (e) {
      if (!mountedRef.current) return;
      if (!scheduleRetry()) {
        setLoadError('Failed to load signatures');
        setLoading(false);
      }
    }
  }, [handler, scheduleRetry]);

  useEffect(() => {
    loadRef.current = () => { void loadSignatures(); };
  }, [loadSignatures]);

  useEffect(() => {
    void loadSignatures();
  }, [loadSignatures]);

  useEffect(() => {
    const listenerId = router.onMailSignaturesChanged.addListener(() => {
      void loadSignatures();
    });
    return () => {
      router.removeListener(listenerId);
    };
  }, [router, loadSignatures]);

  const saveSignature = async () => {
    if (!editingSig?.name || !editingSig?.content_html) {
      setError('Please fill in both name and content.');
      return;
    }
    const {ok, error: err} = await handler.mailUpsertSignature(JSON.stringify(editingSig));
    if (ok) {
      setEditingSig(null);
      setError('');
      void loadSignatures();
    } else {
      setError(err || 'Failed to save signature');
    }
  };

  const deleteSignature = async (id: string) => {
    const {ok} = await handler.mailDeleteSignature(id);
    if (ok) {
      void loadSignatures();
    } else {
      setError('Failed to delete signature');
    }
  };

  return (
    <PaneShell pane={pane}>
      {loading ? (
        <p className="py-4 text-center text-sm text-muted-foreground"><Loader2 className="mr-2 inline size-4 animate-spin" />Loading signatures...</p>
      ) : (
        <div className="space-y-6">
          <SectionCard
            title="Email Signatures"
            description="Manage your email signatures."
            action={
              <Button onClick={() => setEditingSig({name: '', content_html: '', is_default: false})}>
                <Plus className="mr-2 size-4" /> Add Signature
              </Button>
            }
          >
            {error && <p className="p-4 text-sm text-destructive">{error}</p>}
            {loadError && (
              <div className="flex flex-wrap items-center justify-between gap-3 p-4 text-sm text-muted-foreground">
                <span>{loadError}</span>
                <Button type="button" size="sm" variant="outline" onClick={() => { retryCountRef.current = 0; setLoadError(''); void loadSignatures(); }}>Retry</Button>
              </div>
            )}
            <div className="divide-y divide-border">
              {signatures.map((sig) => (
                <div key={sig.id} className="flex items-center justify-between p-4">
                  <div>
                    <h4 className="font-semibold text-sm">{sig.name} {sig.is_default && <span className="text-xs text-primary font-normal">(Default)</span>}</h4>
                    <div className="mt-1 text-xs text-muted-foreground" dangerouslySetInnerHTML={{__html: sig.content_html}} />
                  </div>
                  <div className="flex gap-2">
                    <Button variant="outline" size="sm" onClick={() => setEditingSig(sig)}>
                      <Edit2 className="size-3.5" />
                    </Button>
                    <Button variant="outline" size="sm" className="text-destructive hover:bg-destructive/10" onClick={() => deleteSignature(sig.id)}>
                      <Trash2 className="size-3.5" />
                    </Button>
                  </div>
                </div>
              ))}
              {signatures.length === 0 && (
                <p className="p-4 text-center text-sm text-muted-foreground">No signatures found.</p>
              )}
            </div>
          </SectionCard>

          {editingSig && (
            <SectionCard title={editingSig.id ? 'Edit Signature' : 'New Signature'}>
              <div className="space-y-4 p-4">
                <div className="grid gap-2">
                  <label className="text-xs font-medium">Signature Name</label>
                  <Input value={editingSig.name || ''} onChange={(e) => setEditingSig({...editingSig, name: e.target.value})} placeholder="Standard Signature" />
                </div>
                <div className="grid gap-2">
                  <label className="text-xs font-medium">Signature HTML Content</label>
                  <Input value={editingSig.content_html || ''} onChange={(e) => setEditingSig({...editingSig, content_html: e.target.value})} placeholder="Regards,<br><b>John Doe</b>" />
                </div>
                <div className="flex items-center gap-2">
                  <input type="checkbox" checked={editingSig.is_default || false} onChange={(e) => setEditingSig({...editingSig, is_default: e.target.checked})} id="is-default-sig" className="rounded border-border bg-background" />
                  <label htmlFor="is-default-sig" className="text-xs font-medium select-none">Set as default signature</label>
                </div>
                <div className="flex gap-2 justify-end">
                  <Button variant="outline" onClick={() => setEditingSig(null)}>Cancel</Button>
                  <Button onClick={saveSignature}>Save</Button>
                </div>
              </div>
            </SectionCard>
          )}
        </div>
      )}
    </PaneShell>
  );
}
