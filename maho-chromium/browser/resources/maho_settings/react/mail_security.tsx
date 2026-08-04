// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';
import {Loader2, Key, Shield, Plus, Trash2, Check} from 'lucide-react';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from '@ui/select';
import {PaneShell, SectionCard} from './domain_panes.js';
import type {PaneDefinition} from '../models.js';
import type {MahoSettingsStore} from './store.js';

interface PgpKeyInfo {
  key_id: string;
  email: string;
  fingerprint: string;
  is_default: boolean;
  has_private_key: boolean;
  expires_at: number;
}

interface SmimeIdentityInfo {
  id: string;
  email: string;
  display_name: string;
  is_default: boolean;
  has_private_key: boolean;
  expires_at: number;
}

interface MailAccountOption {
  id: string;
  email: string;
}

function parseAccountOptions(resultJson: string): MailAccountOption[] {
  let parsed: unknown;
  try {
    parsed = JSON.parse(resultJson);
  } catch {
    return [];
  }
  if (!Array.isArray(parsed)) {
    return [];
  }
  const options: MailAccountOption[] = [];
  for (const value of parsed) {
    if (typeof value === 'object' && value !== null &&
        'id' in value && typeof value.id === 'string' &&
        'email' in value && typeof value.email === 'string') {
      options.push({id: value.id, email: value.email});
    }
  }
  return options;
}

export function MailSecurityPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const router = store.getCallbackRouter();
  const mountedRef = useRef(true);
  const retryCountRef = useRef(0);
  const retryTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const loadAccountsRef = useRef<() => void>(() => {});
  const [accounts, setAccounts] = useState<MailAccountOption[]>([]);
  const [selectedAccountId, setSelectedAccountId] = useState('');
  const [pgpKeys, setPgpKeys] = useState<PgpKeyInfo[]>([]);
  const [smimeIdentities, setSmimeIdentities] = useState<SmimeIdentityInfo[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [loadError, setLoadError] = useState('');
  const [success, setSuccess] = useState('');

  const [generatingPgp, setGeneratingPgp] = useState(false);
  const [genEmail, setGenEmail] = useState('');
  const [genPassphrase, setGenPassphrase] = useState('');

  const [importingPgp, setImportingPgp] = useState(false);
  const [importKeyBlock, setImportKeyBlock] = useState('');
  const [importPassphrase, setImportPassphrase] = useState('');

  const [importingSmime, setImportingSmime] = useState(false);
  const [importP12Data, setImportP12Data] = useState('');
  const [importSmimePassphrase, setImportSmimePassphrase] = useState('');

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
        loadAccountsRef.current();
      }
    }, 800);
    return true;
  }, []);

  const loadAccounts = useCallback(async () => {
    try {
      const {ok, resultJson} = await handler.mailListAccounts();
      if (!mountedRef.current) return;
      if (!ok) {
        if (!scheduleRetry()) {
          setLoadError('Failed to load mail accounts');
        }
        return;
      }
      setLoadError('');
      retryCountRef.current = 0;
      const options = parseAccountOptions(resultJson);
      setAccounts(options);
      setSelectedAccountId((prev) =>
        prev && options.some((a) => a.id === prev)
          ? prev
          : (options.length > 0 ? options[0].id : ''));
    } catch (e) {
      if (!mountedRef.current) return;
      if (!scheduleRetry()) {
        setLoadError('Failed to load mail accounts');
      }
    }
  }, [handler, scheduleRetry]);

  useEffect(() => {
    loadAccountsRef.current = () => { void loadAccounts(); };
  }, [loadAccounts]);

  useEffect(() => {
    void loadAccounts();
  }, [loadAccounts]);

  const loadSecurityData = useCallback(async () => {
    if (!selectedAccountId) {
      setPgpKeys([]);
      setSmimeIdentities([]);
      setLoading(false);
      return;
    }
    setLoading(true);
    try {
      const {ok: okPgp, resultJson: pgpJson} = await handler.mailListPgpKeys(selectedAccountId);
      if (okPgp && pgpJson) {
        setPgpKeys(JSON.parse(pgpJson) as PgpKeyInfo[]);
      } else {
        setError('Failed to load PGP keys');
      }

      const {ok: okSmime, resultJson: smimeJson} = await handler.mailListSmimeIdentities(selectedAccountId);
      if (okSmime && smimeJson) {
        setSmimeIdentities(JSON.parse(smimeJson) as SmimeIdentityInfo[]);
      } else {
        setError('Failed to load S/MIME identities');
      }
    } catch (e) {
      if (mountedRef.current) setError('Failed to load security keys');
    } finally {
      if (mountedRef.current) setLoading(false);
    }
  }, [handler, selectedAccountId]);

  useEffect(() => {
    void loadSecurityData();
  }, [loadSecurityData]);

  useEffect(() => {
    const listenerId = router.onMailSecurityChanged.addListener(() => {
      void loadSecurityData();
    });
    return () => {
      router.removeListener(listenerId);
    };
  }, [router, loadSecurityData]);

  const generatePgp = async () => {
    if (!genEmail || !selectedAccountId) return;
    setGeneratingPgp(true);
    const {ok, error: err} = await handler.mailGeneratePgpKey(selectedAccountId, genEmail, genPassphrase);
    setGeneratingPgp(false);
    if (ok) {
      setGenEmail('');
      setGenPassphrase('');
      setSuccess('PGP key generated.');
      void loadSecurityData();
    } else {
      setError(err || 'Failed to generate PGP key');
    }
  };

  const importPgp = async () => {
    if (!importKeyBlock || !selectedAccountId) return;
    const {ok, error: err} = await handler.mailImportPgpKey(selectedAccountId, importKeyBlock, importPassphrase || null);
    if (ok) {
      setImportKeyBlock('');
      setImportPassphrase('');
      setImportingPgp(false);
      setSuccess('PGP key imported.');
      void loadSecurityData();
    } else {
      setError(err || 'Failed to import PGP key');
    }
  };

  const importSmime = async () => {
    if (!importP12Data || !selectedAccountId) return;
    const {ok, error: err} = await handler.mailImportSmimeIdentity(selectedAccountId, importP12Data, importSmimePassphrase);
    if (ok) {
      setImportP12Data('');
      setImportSmimePassphrase('');
      setImportingSmime(false);
      setSuccess('S/MIME identity imported.');
      void loadSecurityData();
    } else {
      setError(err || 'Failed to import S/MIME identity');
    }
  };

  const deletePgp = async (keyId: string) => {
    const {ok} = await handler.mailDeletePgpKey(keyId);
    if (ok) {
      void loadSecurityData();
    } else {
      setError('Failed to delete PGP key');
    }
  };

  const deleteSmime = async (id: string) => {
    const {ok} = await handler.mailDeleteSmimeIdentity(id);
    if (ok) {
      void loadSecurityData();
    } else {
      setError('Failed to delete S/MIME identity');
    }
  };

  return (
    <PaneShell pane={pane}>
      {loading ? (
        <p className="py-4 text-center text-sm text-muted-foreground"><Loader2 className="mr-2 inline size-4 animate-spin" />Loading security settings...</p>
      ) : (
        <div className="space-y-6">
          {error && <p className="p-4 text-sm text-destructive">{error}</p>}
          {success && <p className="p-4 text-sm text-success flex items-center gap-1"><Check className="size-4" />{success}</p>}
          {loadError && (
            <div className="flex flex-wrap items-center justify-between gap-3 p-4 text-sm text-muted-foreground">
              <span>{loadError}</span>
              <Button type="button" size="sm" variant="outline" onClick={() => { retryCountRef.current = 0; setLoadError(''); void loadAccounts(); }}>Retry</Button>
            </div>
          )}

          {accounts.length === 0 && !loadError ? (
            <p className="p-4 text-center text-sm text-muted-foreground">Connect a mail account to manage security keys.</p>
          ) : accounts.length > 0 ? (
            <SectionCard title="Account" description="PGP keys and S/MIME identities are scoped to the selected mail account.">
              <div className="flex items-center justify-between p-4">
                <h4 className="font-semibold text-sm">Mail Account</h4>
                <Select value={selectedAccountId} onValueChange={setSelectedAccountId}>
                  <SelectTrigger className="w-[240px]">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {accounts.map((account) => (
                      <SelectItem key={account.id} value={account.id}>{account.email}</SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
            </SectionCard>
          ) : null}

          {accounts.length > 0 && (
          <>
          <SectionCard
            title="PGP Encryption Keys"
            description="Manage your OpenPGP public and private keys."
            action={
              <div className="flex gap-2">
                <Button variant="outline" size="sm" onClick={() => setImportingPgp(true)}>Import Key</Button>
                <Button size="sm" onClick={() => setGenEmail('user@example.com')}>Generate Key</Button>
              </div>
            }
          >
            <div className="divide-y divide-border">
              {pgpKeys.map((key) => (
                <div key={key.key_id} className="flex items-center justify-between p-4">
                  <div className="flex items-start gap-3">
                    <Key className="size-4 mt-1 text-muted-foreground" />
                    <div>
                      <h4 className="font-semibold text-sm">{key.email} {key.is_default && <span className="text-xs text-primary font-normal">(Default)</span>}</h4>
                      <p className="text-xs text-muted-foreground font-mono">Fingerprint: {key.fingerprint}</p>
                      <p className="text-xs text-muted-foreground">Type: {key.has_private_key ? 'Keypair' : 'Public Key'}</p>
                    </div>
                  </div>
                  <Button variant="outline" size="sm" className="text-destructive hover:bg-destructive/10" onClick={() => deletePgp(key.key_id)}>
                    <Trash2 className="size-3.5" />
                  </Button>
                </div>
              ))}
              {pgpKeys.length === 0 && (
                <p className="p-4 text-center text-sm text-muted-foreground">No PGP keys found.</p>
              )}
            </div>
          </SectionCard>

          {genEmail && (
            <SectionCard title="Generate PGP Key">
              <div className="space-y-4 p-4">
                <div className="grid gap-2">
                  <label htmlFor="pgp-gen-email" className="text-xs font-medium">Email Address</label>
                  <Input id="pgp-gen-email" value={genEmail} onChange={(e) => setGenEmail(e.target.value)} />
                </div>
                <div className="grid gap-2">
                  <label htmlFor="pgp-gen-passphrase" className="text-xs font-medium">Passphrase (Optional)</label>
                  <Input id="pgp-gen-passphrase" type="password" value={genPassphrase} onChange={(e) => setGenPassphrase(e.target.value)} />
                </div>
                <div className="flex gap-2 justify-end">
                  <Button variant="outline" onClick={() => setGenEmail('')}>Cancel</Button>
                  <Button onClick={generatePgp} disabled={generatingPgp}>{generatingPgp ? 'Generating...' : 'Generate'}</Button>
                </div>
              </div>
            </SectionCard>
          )}

          {importingPgp && (
            <SectionCard title="Import PGP Key">
              <div className="space-y-4 p-4">
                <div className="grid gap-2">
                  <label htmlFor="pgp-import-keyblock" className="text-xs font-medium">ASCII-Armored Key Block</label>
                  <Input id="pgp-import-keyblock" value={importKeyBlock} onChange={(e) => setImportKeyBlock(e.target.value)} placeholder="-----BEGIN PGP PUBLIC KEY BLOCK-----" />
                </div>
                <div className="grid gap-2">
                  <label htmlFor="pgp-import-passphrase" className="text-xs font-medium">Key Passphrase (if private key)</label>
                  <Input id="pgp-import-passphrase" type="password" value={importPassphrase} onChange={(e) => setImportPassphrase(e.target.value)} />
                </div>
                <div className="flex gap-2 justify-end">
                  <Button variant="outline" onClick={() => setImportingPgp(false)}>Cancel</Button>
                  <Button onClick={importPgp}>Import</Button>
                </div>
              </div>
            </SectionCard>
          )}

          <SectionCard
            title="S/MIME Identities"
            description="Manage your S/MIME email certificates."
            action={
              <Button size="sm" onClick={() => setImportingSmime(true)}>Import Certificate</Button>
            }
          >
            <div className="divide-y divide-border">
              {smimeIdentities.map((id) => (
                <div key={id.id} className="flex items-center justify-between p-4">
                  <div className="flex items-start gap-3">
                    <Shield className="size-4 mt-1 text-muted-foreground" />
                    <div>
                      <h4 className="font-semibold text-sm">{id.email} {id.is_default && <span className="text-xs text-primary font-normal">(Default)</span>}</h4>
                      <p className="text-xs text-muted-foreground">User: {id.display_name}</p>
                    </div>
                  </div>
                  <Button variant="outline" size="sm" className="text-destructive hover:bg-destructive/10" onClick={() => deleteSmime(id.id)}>
                    <Trash2 className="size-3.5" />
                  </Button>
                </div>
              ))}
              {smimeIdentities.length === 0 && (
                <p className="p-4 text-center text-sm text-muted-foreground">No S/MIME certificates found.</p>
              )}
            </div>
          </SectionCard>

          {importingSmime && (
            <SectionCard title="Import S/MIME Certificate">
              <div className="space-y-4 p-4">
                <div className="grid gap-2">
                  <label htmlFor="smime-import-p12" className="text-xs font-medium">PKCS#12 (.p12 / .pfx) Base64 Encoded Data</label>
                  <Input id="smime-import-p12" value={importP12Data} onChange={(e) => setImportP12Data(e.target.value)} />
                </div>
                <div className="grid gap-2">
                  <label htmlFor="smime-import-passphrase" className="text-xs font-medium">Certificate Passphrase</label>
                  <Input id="smime-import-passphrase" type="password" value={importSmimePassphrase} onChange={(e) => setImportSmimePassphrase(e.target.value)} />
                </div>
                <div className="flex gap-2 justify-end">
                  <Button variant="outline" onClick={() => setImportingSmime(false)}>Cancel</Button>
                  <Button onClick={importSmime}>Import</Button>
                </div>
              </div>
            </SectionCard>
          )}
          </>
          )}
        </div>
      )}
    </PaneShell>
  );
}
