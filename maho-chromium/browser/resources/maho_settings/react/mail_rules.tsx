// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';
import {Plus, Trash2, Loader2, ArrowUp, ArrowDown} from 'lucide-react';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {PaneShell, SectionCard} from './domain_panes.js';
import {classifyMailSettingsFailure, createMailSettingsContentState, type MailSettingsState} from './mail_settings_state.js';
import type {PaneDefinition} from '../models.js';
import type {MahoSettingsStore} from './store.js';

interface MailRule {
  id: string;
  account_id: string;
  name: string;
  conditions_json: string;
  actions_json: string;
  priority: number;
  enabled: boolean;
}

export function MailRulesPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const router = store.getCallbackRouter();
  const mountedRef = useRef(true);
  const [rules, setRules] = useState<MailRule[]>([]);
  const [modelState, setModelState] = useState<MailSettingsState<MailRule[]>>({status: 'loading'});
  const [editingRule, setEditingRule] = useState<Partial<MailRule> | null>(null);
  const [error, setError] = useState('');


  useEffect(() => {
    return () => {
      mountedRef.current = false;

    };
  }, []);

  const loadRules = useCallback(async () => {
    setModelState({status: 'loading'});
    try {
      const {ok, resultJson} = await handler.mailListRules('');
      if (!mountedRef.current) return;
      if (ok) {
        const next = JSON.parse(resultJson) as MailRule[];
        setRules(next);
        setModelState(createMailSettingsContentState(next));
      } else {
        setModelState(classifyMailSettingsFailure(resultJson, 'Failed to load rules'));
      }
    } catch {
      if (!mountedRef.current) return;
      setModelState({status: 'error', message: 'Failed to load rules'});
    }
  }, [handler]);

  useEffect(() => {
    void loadRules();
  }, [loadRules]);

  useEffect(() => {
    const listenerId = router.onMailRulesChanged.addListener(() => {
      void loadRules();
    });
    return () => {
      router.removeListener(listenerId);
    };
  }, [router, loadRules]);

  const saveRule = async () => {
    if (!editingRule?.name || !editingRule?.conditions_json || !editingRule?.actions_json) {
      setError('Please fill in name, conditions and actions.');
      return;
    }
    const {ok, error: err} = await handler.mailUpsertRule(JSON.stringify(editingRule));
    if (ok) {
      setEditingRule(null);
      setError('');
      void loadRules();
    } else {
      setError(err || 'Failed to save rule');
    }
  };

  const deleteRule = async (id: string) => {
    const {ok} = await handler.mailDeleteRule(id);
    if (ok) {
      void loadRules();
    } else {
      setError('Failed to delete rule');
    }
  };

  const moveRule = async (index: number, direction: 'up' | 'down') => {
    const newRules = [...rules];
    const targetIndex = direction === 'up' ? index - 1 : index + 1;
    if (targetIndex < 0 || targetIndex >= rules.length) return;
    const temp = newRules[index]!;
    newRules[index] = newRules[targetIndex]!;
    newRules[targetIndex] = temp;
    const ruleIds = newRules.map(r => r.id);
    const {ok} = await handler.mailReorderRules('', JSON.stringify(ruleIds));
    if (ok) {
      void loadRules();
    } else {
      setError('Failed to reorder rules');
    }
  };

  return (
    <PaneShell pane={pane}>
      <div data-mail-settings-state={modelState.status}>
      {modelState.status === 'loading' ? (
        <p className="py-4 text-center text-sm text-muted-foreground"><Loader2 className="mr-2 inline size-4 animate-spin" />Loading rules...</p>
      ) : modelState.status === 'unavailable' || modelState.status === 'error' ? (
        <div className="flex flex-wrap items-center justify-between gap-3 p-4 text-sm text-muted-foreground"><span>{modelState.message}</span><Button type="button" size="sm" variant="outline" onClick={() => { void loadRules(); }}>Retry</Button></div>
      ) : (
        <div className="space-y-6">
          <SectionCard
            title="Mail Filters & Rules"
            description="Create rules to automate handling incoming messages."
            action={
              <Button onClick={() => setEditingRule({name: '', conditions_json: '{}', actions_json: '{}', enabled: true, account_id: ''})}>
                <Plus className="mr-2 size-4" /> Add Rule
              </Button>
            }
          >
            {error && <p className="p-4 text-sm text-destructive">{error}</p>}

            <div className="divide-y divide-border">
              {rules.map((rule, idx) => (
                <div key={rule.id} className="flex items-center justify-between p-4">
                  <div>
                    <h4 className="font-semibold text-sm">{rule.name} {!rule.enabled && <span className="text-xs text-muted-foreground font-normal">(Disabled)</span>}</h4>
                    <p className="mt-1 text-xs text-muted-foreground">Priority: {idx + 1}</p>
                  </div>
                  <div className="flex gap-2">
                    <Button variant="outline" size="sm" disabled={idx === 0} onClick={() => moveRule(idx, 'up')}>
                      <ArrowUp className="size-3.5" />
                    </Button>
                    <Button variant="outline" size="sm" disabled={idx === rules.length - 1} onClick={() => moveRule(idx, 'down')}>
                      <ArrowDown className="size-3.5" />
                    </Button>
                    <Button variant="outline" size="sm" className="text-destructive hover:bg-destructive/10" onClick={() => deleteRule(rule.id)}>
                      <Trash2 className="size-3.5" />
                    </Button>
                  </div>
                </div>
              ))}
              {rules.length === 0 && (
                <p className="p-4 text-center text-sm text-muted-foreground">No rules found.</p>
              )}
            </div>
          </SectionCard>

          {editingRule && (
            <SectionCard title={editingRule.id ? 'Edit Rule' : 'New Rule'}>
              <div className="space-y-4 p-4">
                <div className="grid gap-2">
                  <label className="text-xs font-medium" htmlFor="mail-rule-name">Rule Name</label>
                  <Input id="mail-rule-name" value={editingRule.name || ''} onChange={(e) => setEditingRule({...editingRule, name: e.target.value})} placeholder="Archive Newsletters" />
                </div>
                <div className="grid gap-2">
                  <label className="text-xs font-medium" htmlFor="mail-rule-conditions">Conditions (JSON)</label>
                  <Input id="mail-rule-conditions" value={editingRule.conditions_json || ''} onChange={(e) => setEditingRule({...editingRule, conditions_json: e.target.value})} />
                </div>
                <div className="grid gap-2">
                  <label className="text-xs font-medium" htmlFor="mail-rule-actions">Actions (JSON)</label>
                  <Input id="mail-rule-actions" value={editingRule.actions_json || ''} onChange={(e) => setEditingRule({...editingRule, actions_json: e.target.value})} />
                </div>
                <div className="flex items-center gap-2">
                  <input type="checkbox" checked={editingRule.enabled || false} onChange={(e) => setEditingRule({...editingRule, enabled: e.target.checked})} id="is-enabled-rule" className="rounded border-border bg-background" />
                  <label htmlFor="is-enabled-rule" className="text-xs font-medium select-none">Enabled</label>
                </div>
                <div className="flex gap-2 justify-end">
                  <Button variant="outline" onClick={() => setEditingRule(null)}>Cancel</Button>
                  <Button onClick={saveRule}>Save</Button>
                </div>
              </div>
            </SectionCard>
          )}
        </div>
      )}
      </div>
    </PaneShell>
  );
}
