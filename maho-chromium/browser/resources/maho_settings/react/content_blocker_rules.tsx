import React, {useCallback, useEffect, useRef, useState} from 'react';

import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {ManagedSettingRow, SectionCard} from './domain_panes.js';
import type {ContentBlockerTestResult, PageHandlerRemote} from '../maho_settings.mojom-webui.js';

// Same cap as the core (MAX_LIST_BODY_BYTES); larger files are rejected
// before any bytes leave the page.
export const MAX_LOCAL_FILTER_LIST_BYTES = 16 * 1024 * 1024;

const REQUEST_TYPES = ['script', 'image', 'stylesheet', 'xmlhttprequest', 'sub_frame', 'document', 'other'] as const;

/**
 * Custom rules, request tester and local filter-file import for the native
 * content blocker. Local files are read with the page's own file chooser and
 * sent as text; the browser never opens a user-typed path.
 */
export function ContentBlockerRulesCard({handler, disabled, onChanged}: {
  handler: PageHandlerRemote;
  disabled: boolean;
  onChanged: () => void;
}) {
  const [rules, setRules] = useState('');
  const [savedRules, setSavedRules] = useState('');
  const [rulesMessage, setRulesMessage] = useState<{error: boolean; text: string} | null>(null);
  const [busy, setBusy] = useState(false);
  const [testUrl, setTestUrl] = useState('');
  const [testSource, setTestSource] = useState('');
  const [testType, setTestType] = useState<string>('script');
  const [testResult, setTestResult] = useState<ContentBlockerTestResult | null>(null);
  const [importMessage, setImportMessage] = useState<{error: boolean; text: string} | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    let cancelled = false;
    void handler.getCustomFilterRules().then(({rules: loaded}) => {
      if (!cancelled) {
        setRules(loaded);
        setSavedRules(loaded);
      }
    });
    return () => { cancelled = true; };
  }, [handler]);

  const saveRules = useCallback(async () => {
    setBusy(true);
    setRulesMessage(null);
    try {
      const {success, mutation} = await handler.saveCustomFilterRules(rules);
      if (success) {
        setSavedRules(rules);
        setRulesMessage({error: false, text: rules.trim() ? 'Custom rules saved.' : 'Custom rules removed.'});
        onChanged();
      } else {
        setRulesMessage({error: true, text: mutation.errorMessage || 'Custom rules were rejected.'});
      }
    } finally {
      setBusy(false);
    }
  }, [handler, onChanged, rules]);

  const runTest = useCallback(async () => {
    const url = testUrl.trim();
    if (!url) {
      return;
    }
    const source = testSource.trim() || url;
    const {result} = await handler.testContentBlockerRequest(url, source, testType);
    setTestResult(result);
  }, [handler, testSource, testType, testUrl]);

  const importFile = useCallback(async (file: File) => {
    setImportMessage(null);
    if (file.size > MAX_LOCAL_FILTER_LIST_BYTES) {
      setImportMessage({error: true, text: 'This file is larger than 16 MB.'});
      return;
    }
    setBusy(true);
    try {
      let content: string;
      try {
        content = await readFileAsText(file);
      } catch {
        setImportMessage({error: true, text: 'The file could not be read.'});
        return;
      }
      const name = file.name.replace(/\.[^.]+$/, '');
      const {success, mutation} = await handler.importLocalFilterList(name, content);
      if (success) {
        setImportMessage({error: false, text: `Imported ${file.name}.`});
        onChanged();
      } else {
        setImportMessage({error: true, text: mutation.errorMessage || 'The file is not a valid filter list.'});
      }
    } finally {
      setBusy(false);
      if (fileInputRef.current) {
        fileInputRef.current.value = '';
      }
    }
  }, [handler, onChanged]);

  return (
    <SectionCard title="Custom rules and diagnostics" className="mt-6">
      <ManagedSettingRow
        title="Custom rules"
        description={
          <>
            <span className="block">One rule per line, in Adblock Plus syntax. Saved rules apply on top of your filter lists.</span>
            {rulesMessage ? (
              <span role="status" className={rulesMessage.error ? 'block text-destructive mt-1 text-xs' : 'block text-muted-foreground mt-1 text-xs'}>{rulesMessage.text}</span>
            ) : null}
          </>
        }>
        <div className="flex flex-col gap-2 w-full">
          <textarea
            aria-label="Custom filter rules"
            className="min-h-[96px] w-full rounded-md border bg-background p-2 font-mono text-xs"
            disabled={disabled || busy}
            spellCheck={false}
            value={rules}
            onChange={event => setRules(event.currentTarget.value)}
          />
          <div>
            <Button type="button" disabled={disabled || busy || rules === savedRules} onClick={() => void saveRules()}>
              Save rules
            </Button>
          </div>
        </div>
      </ManagedSettingRow>

      <ManagedSettingRow
        title="Test a request"
        description="Check whether a request would be blocked and which rule decides it.">
        <div className="flex flex-col gap-2 w-full">
          <Input aria-label="Request URL" placeholder="https://ads.example.com/banner.js" value={testUrl}
            onChange={event => setTestUrl(event.currentTarget.value)} />
          <Input aria-label="Page URL" placeholder="https://news.example.com/ (optional)" value={testSource}
            onChange={event => setTestSource(event.currentTarget.value)} />
          <select aria-label="Request type" className="rounded-md border bg-background p-1 text-sm" value={testType}
            onChange={event => setTestType(event.currentTarget.value)}>
            {REQUEST_TYPES.map(type => <option key={type} value={type}>{type}</option>)}
          </select>
          <div>
            <Button type="button" variant="outline" disabled={!testUrl.trim()} onClick={() => void runTest()}>Test</Button>
          </div>
          {testResult ? <TestResultView result={testResult} /> : null}
        </div>
      </ManagedSettingRow>

      <ManagedSettingRow
        title="Import a filter list file"
        description={
          <>
            <span className="block">Add a filter list from a text file on this computer. It is stored in your profile and never fetched from the network.</span>
            {importMessage ? (
              <span role="status" className={importMessage.error ? 'block text-destructive mt-1 text-xs' : 'block text-muted-foreground mt-1 text-xs'}>{importMessage.text}</span>
            ) : null}
          </>
        }>
        <input
          ref={fileInputRef}
          aria-label="Filter list file"
          type="file"
          accept=".txt,text/plain"
          className="hidden"
          onChange={event => {
            const file = event.currentTarget.files?.[0];
            if (file) {
              void importFile(file);
            }
          }}
        />
        <Button type="button" variant="outline" disabled={disabled || busy} onClick={() => fileInputRef.current?.click()}>
          Choose file…
        </Button>
      </ManagedSettingRow>
    </SectionCard>
  );
}

function readFileAsText(file: Blob): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result ?? ''));
    reader.onerror = () => reject(reader.error);
    reader.readAsText(file);
  });
}

function TestResultView({result}: {result: ContentBlockerTestResult}) {
  let verdict: string;
  if (!result.validRequest) {
    verdict = 'Not a valid request URL.';
  } else if (!result.nativeMode) {
    verdict = 'Native blocking is off, so nothing is blocked.';
  } else if (result.siteExcepted) {
    verdict = 'Allowed: this site is on your exception list.';
  } else if (result.blocked) {
    verdict = result.important ? 'Blocked (important rule).' : 'Blocked.';
  } else if (result.exception) {
    verdict = 'Allowed by an exception rule.';
  } else {
    verdict = 'Allowed: no rule matches.';
  }
  return (
    <div role="status" aria-label="Request test result" className="rounded-md border p-2 text-xs">
      <div className="font-medium">{verdict}</div>
      {result.matchedFilter ? <div className="font-mono mt-1">Rule: {result.matchedFilter}</div> : null}
      {result.exception ? <div className="font-mono mt-1">Exception: {result.exception}</div> : null}
      {result.listId ? <div className="mt-1">List: {result.listId}</div> : null}
      {result.redirect ? <div className="mt-1">Redirected to a placeholder resource.</div> : null}
    </div>
  );
}
