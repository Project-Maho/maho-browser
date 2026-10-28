import React, {useEffect, useState} from 'react';

import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {Switch} from '@ui/switch';
import {ActionButton, ManagedSettingRow, PaneShell, SectionCard} from './domain_panes.js';
import type {PaneDefinition} from '../models.js';
import type {PageHandlerRemote, SettingValue} from '../mojo.js';
import type {MahoSettingsStore} from './store.js';

interface EpisodicMemorySettings {
  enabled: boolean;
  paused: boolean;
  excludedDomains: string[];
}

export function MemorySettingsPane({pane, settings, store}: {
  pane: PaneDefinition;
  settings: SettingValue[];
  store: MahoSettingsStore;
}) {
  const handler = store.getHandler() as PageHandlerRemote;
  const [memory, setMemory] = useState<EpisodicMemorySettings>({
    enabled: false,
    paused: false,
    excludedDomains: [],
  });
  const [domainDraft, setDomainDraft] = useState('');
  const [siteDraft, setSiteDraft] = useState('');
  const [message, setMessage] = useState('');
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    const value = settings.find(item => item.key === 'memory.episodic')?.value;
    if (!value) return;
    try {
      const parsed = JSON.parse(value) as Partial<EpisodicMemorySettings>;
      setMemory({
        enabled: parsed.enabled === true,
        paused: parsed.paused === true,
        excludedDomains: Array.isArray(parsed.excludedDomains) ?
            parsed.excludedDomains.filter((domain): domain is string => typeof domain === 'string') : [],
      });
    } catch {
      setMessage('Could not read episodic memory settings.');
    }
  }, [settings]);

  const save = async (next: EpisodicMemorySettings) => {
    if (saving) return false;
    const previous = memory;
    setMemory(next);
    setSaving(true);
    setMessage('');
    try {
      const {success} = await handler.setEpisodicMemory(
          next.enabled, next.paused, next.excludedDomains);
      if (!success) throw new Error('Settings update rejected');
      return true;
    } catch {
      setMemory(previous);
      setMessage('Could not save episodic memory settings.');
      return false;
    } finally {
      setSaving(false);
    }
  };

  const addDomain = async () => {
    const domain = domainDraft.trim().toLowerCase().replace(/^\*\./, '');
    if (!domain || memory.excludedDomains.includes(domain)) return;
    if (await save({...memory, excludedDomains: [...memory.excludedDomains, domain]})) {
      setDomainDraft('');
    }
  };

  return (
    <PaneShell pane={pane}>
      <SectionCard title="Episodic memory" description="Remember brief summaries of pages you visit. Recording is off by default. Private and sensitive sites are never recorded.">
        <ManagedSettingRow title="Remember browsing episodes" description="When enabled, Maho stores a short page title and summary in local memory for later retrieval.">
          <Switch aria-label="Remember browsing episodes" checked={memory.enabled} disabled={saving} onCheckedChange={enabled => void save({...memory, enabled})} />
        </ManagedSettingRow>
        <ManagedSettingRow title="Pause recording" description="Keep episodic memory enabled, but temporarily stop recording new pages.">
          <Switch aria-label="Pause episodic memory" checked={memory.paused} disabled={saving || !memory.enabled} onCheckedChange={paused => void save({...memory, paused})} />
        </ManagedSettingRow>
        <div className="space-y-3 border-t border-border px-4 py-4 sm:px-6">
          <div>
            <h4 className="text-sm font-medium">Excluded domains</h4>
            <p className="mt-1 text-xs text-muted-foreground">Pages on these domains and their subdomains will not be remembered.</p>
          </div>
          <div className="flex gap-2">
            <Input aria-label="Domain to exclude" placeholder="example.com" value={domainDraft} onChange={event => setDomainDraft(event.currentTarget.value)} onKeyDown={event => { if (event.key === 'Enter') { event.preventDefault(); void addDomain(); } }} />
            <Button type="button" variant="outline" disabled={!domainDraft.trim() || saving} onClick={() => void addDomain()}>Add domain</Button>
          </div>
          <ul className="space-y-2">
            {memory.excludedDomains.map(domain => (
              <li className="flex items-center justify-between gap-2 rounded-md border border-border px-3 py-2 text-sm" key={domain}>
                <span>{domain}</span>
                <Button type="button" variant="outline" aria-label={`Remove ${domain}`} disabled={saving} onClick={() => void save({...memory, excludedDomains: memory.excludedDomains.filter(item => item !== domain)})}>Remove</Button>
              </li>
            ))}
          </ul>
        </div>
      </SectionCard>
      <div className="mt-6">
        <SectionCard title="Forget a site" description="Delete all stored browsing episodes associated with a site origin.">
          <div className="flex flex-col gap-3 border-t border-border px-4 py-4 sm:flex-row sm:px-6">
            <Input aria-label="Site to forget" placeholder="https://example.com" value={siteDraft} disabled={saving} onChange={event => setSiteDraft(event.currentTarget.value)} />
            <ActionButton label="Forget site" disabled={!siteDraft.trim()} onClick={async () => {
              setMessage('');
              setSaving(true);
              try {
                const {deletedCount} = await handler.forgetEpisodeSite(siteDraft.trim());
                if (deletedCount < 0n) setMessage('Could not forget episodes for that site.');
                else setMessage(`Forgot ${deletedCount} episode${deletedCount === 1n ? '' : 's'}.`);
              } catch {
                setMessage('Could not forget episodes for that site.');
              } finally {
                setSaving(false);
              }
            }} />
          </div>
        </SectionCard>
      </div>
      {message ? <p className="mt-3 text-sm" role="status">{message}</p> : null}
    </PaneShell>
  );
}
