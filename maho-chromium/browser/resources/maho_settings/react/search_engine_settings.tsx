import {useCallback, useEffect, useState} from 'react';

import {Check, Search} from '@icons/lucide';
import {cn} from '@lib/utils';
import type {SearchEngineInfo} from '../maho_settings.mojom-webui.js';
import type {MahoSettingsStore} from './store.js';
import {SectionCard} from './domain_panes.js';

type LoadState =
    | {status: 'loading'}
    | {status: 'error'; message: string}
    | {status: 'loaded'; engines: readonly SearchEngineInfo[]};

export function SearchEngineSettings({store}: {store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const [state, setState] = useState<LoadState>({status: 'loading'});
  const [savingKeyword, setSavingKeyword] = useState<string | null>(null);
  const [saveError, setSaveError] = useState<string | null>(null);

  const load = useCallback(async () => {
    setState({status: 'loading'});
    try {
      const {engines} = await handler.getSearchEngines();
      setState({status: 'loaded', engines});
    } catch (error) {
      setState({
        status: 'error',
        message: error instanceof Error ? error.message : 'Failed to load search engines.',
      });
    }
  }, [handler]);

  useEffect(() => {
    void load();
  }, [load]);

  return (
    <div className="mb-6">
      <SectionCard
        title="Default search engine"
        description="Choose the search engine used for searches from the address bar.">
        {state.status === 'loading' ? (
          <p className="px-6 py-5 text-sm text-muted-foreground">Loading search engines…</p>
        ) : null}
        {state.status === 'error' ? (
          <button
            className="mx-6 my-5 text-sm font-medium text-primary hover:underline"
            type="button"
            onClick={() => void load()}>
            {state.message} Try again
          </button>
        ) : null}
        {state.status === 'loaded' ? (
          <div className="border-t border-border px-6 py-5">
            {saveError ? (
              <p className="mb-3 text-sm text-destructive">{saveError}</p>
            ) : null}
            <div className="grid grid-cols-2 gap-3 sm:grid-cols-3">
              {state.engines.map(engine => (
              <button
                key={engine.keyword}
                aria-pressed={engine.isDefault}
                className={cn(
                    'relative flex min-h-24 flex-col items-center justify-center gap-2 rounded-xl border border-border bg-background px-3 py-4 text-sm font-medium text-foreground transition hover:-translate-y-0.5 hover:bg-muted/60 hover:shadow-md focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring',
                    engine.isDefault && 'border-primary bg-primary/5 ring-2 ring-primary/20')}
                disabled={savingKeyword !== null}
                type="button"
                onClick={async () => {
                  if (engine.isDefault || savingKeyword !== null) {
                    return;
                  }
                  setSaveError(null);
                  setSavingKeyword(engine.keyword);
                  try {
                    const {success} = await handler.setDefaultSearchEngine(engine.keyword);
                    if (!success) {
                      setSaveError('Could not change the default search engine.');
                      return;
                    }
                    setState({
                      status: 'loaded',
                      engines: state.engines.map(candidate => ({
                        ...candidate,
                        isDefault: candidate.keyword === engine.keyword,
                      })),
                    });
                  } catch (error) {
                    setSaveError(error instanceof Error ? error.message : 'Could not change the default search engine.');
                  } finally {
                    setSavingKeyword(null);
                  }
                }}>
                {engine.isDefault ? (
                  <span className="absolute right-2 top-2 flex size-5 items-center justify-center rounded-full bg-primary text-primary-foreground">
                    <Check aria-hidden="true" className="size-3" />
                  </span>
                ) : null}
                {engine.iconUrl ? (
                  <img alt="" className="size-8 rounded-md" src={engine.iconUrl} />
                ) : (
                  <Search aria-hidden="true" className="size-8 text-muted-foreground" />
                )}
                <span className="max-w-full truncate">{engine.name}</span>
              </button>
              ))}
            </div>
          </div>
        ) : null}
      </SectionCard>
    </div>
  );
}
