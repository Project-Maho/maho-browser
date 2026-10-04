import {Check} from 'lucide-react';

import {cn} from '@lib/utils';
import type {MahoWelcomeStore} from '../store.js';
import type {SearchEngineInfo} from '../../maho_welcome.mojom-webui.js';

interface Props {
  engines: readonly SearchEngineInfo[];
  selectedEngine: string | null;
  onSelect: (keyword: string) => void;
}

export function SearchEngineSidebar({store}: {store: MahoWelcomeStore}) {
  return (
    <>
      <h1>{store.getString('IDS_MAHO_WELCOME_SEARCH_TITLE')}</h1>
      <p>{store.getString('IDS_MAHO_WELCOME_SEARCH_BODY')}</p>
    </>
  );
}

export function SearchEngineContent({engines, selectedEngine, onSelect}: Props) {
  if (engines.length === 0) {
    return (
      <div className="flex h-full w-full items-center justify-center rounded-xl border border-border bg-background/50 p-8 text-center">
        <p className="text-sm text-muted-foreground">Loading search engines…</p>
      </div>
    );
  }

  if (engines.length === 0) {
    return (
      <div className="flex h-full w-full items-center justify-center rounded-xl border border-border bg-background/50 p-8 text-center">
        <p className="text-sm text-muted-foreground">No search engines available.</p>
      </div>
    );
  }

  return (
    <div className="grid w-full grid-cols-2 gap-4 py-4 lg:grid-cols-6">
      {engines.map((engine, index) => {
        let engineName = engine.name ?? '';
        const keyword = engine.keyword?.toLowerCase() ?? '';
        const isSelected = engine.keyword === selectedEngine;

        if (keyword.includes('naver')) {
          engineName = 'Naver';
        } else if (keyword.includes('daum')) {
          engineName = 'Daum';
        }

        return (
          <button
            key={engine.keyword}
            type="button"
            className={cn(
                'relative col-span-1 flex aspect-[1/0.86] flex-col items-center justify-center gap-3 rounded-xl border border-border bg-background/60 p-5 text-foreground shadow-sm transition duration-150 hover:-translate-y-0.5 hover:bg-muted/50 hover:shadow-xl focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring lg:col-span-2',
                index === 3 && 'lg:col-start-2',
                index === 4 && 'lg:col-start-4',
                isSelected && 'scale-[1.04] border-primary bg-muted shadow-xl ring-2 ring-primary/20')}
            aria-pressed={isSelected}
            onClick={() => onSelect(engine.keyword)}
          >
            {isSelected ? (
              <span className="absolute right-2.5 top-2.5 flex size-6 items-center justify-center rounded-full bg-primary text-primary-foreground shadow-lg shadow-primary/20">
                <Check className="size-3.5" aria-hidden="true" />
              </span>
            ) : null}
            {engine.iconUrl ? (
              <img
                className="size-9 rounded-md object-contain"
                src={engine.iconUrl}
                alt=""
                width={32}
                height={32}
              />
            ) : (
              <div className="flex size-9 items-center justify-center rounded-md bg-muted text-base font-semibold text-muted-foreground">
                {engineName.charAt(0)}
              </div>
            )}
            <span className="text-center text-sm font-semibold">{engineName}</span>
          </button>
        );
      })}
    </div>
  );
}
