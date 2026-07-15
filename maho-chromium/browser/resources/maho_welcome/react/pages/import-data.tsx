// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {ArrowRight, CheckCircle2, Globe2} from 'lucide-react';

import {Checkbox} from '@ui/checkbox';
import {Label} from '@ui/label';
import type {MahoWelcomeStore} from '../store.js';
import type {ImportStage} from '../types.js';

const ASSET_ORIGIN = 'chrome://maho-' + 'welcome';
const MAHO_LOGO_SRC = `${ASSET_ORIGIN}/icons/onboarding/maho-logo.png`;

interface Props {
  store: MahoWelcomeStore;
  importStage: ImportStage;
  setAsDefaultRequested: boolean;
  onSetAsDefaultChange: (value: boolean) => void;
}

export function ImportDataSidebar({store, importStage}: {store: MahoWelcomeStore; importStage: ImportStage}) {
  if (importStage === 'B') {
    return (
      <>
        <h1>{store.getString('IDS_MAHO_WELCOME_DEFAULT_BROWSER_TITLE')}</h1>
        <p>{store.getString('IDS_MAHO_WELCOME_DEFAULT_BROWSER_BODY')}</p>
      </>
    );
  }

  return (
    <>
      <h1>{store.getString('IDS_MAHO_WELCOME_IMPORT_TITLE')}</h1>
      <p>{store.getString('IDS_MAHO_WELCOME_IMPORT_BODY1')}</p>
      <p>{store.getString('IDS_MAHO_WELCOME_IMPORT_BODY2')}</p>
    </>
  );
}

export function ImportDataContent(props: Props) {
  const {store, importStage, setAsDefaultRequested, onSetAsDefaultChange} = props;

  if (importStage === 'B') {
    return <DefaultBrowserChoice
      store={store}
      setAsDefaultRequested={setAsDefaultRequested}
      onSetAsDefaultChange={onSetAsDefaultChange}
    />;
  }

  return (
    <div className="flex h-full w-full items-center justify-center">
      <div className="relative flex w-full max-w-lg items-center justify-center gap-6 rounded-xl border border-border bg-background/50 p-8 shadow-sm">
        <div aria-hidden="true" className="absolute inset-0 rounded-xl bg-gradient-to-br from-muted/40 via-transparent to-primary/5" />
        <div className="relative z-10 flex items-center gap-6">
          <div className="flex size-32 items-center justify-center rounded-full border border-border bg-muted/60 shadow-xl shadow-black/20 backdrop-blur-md">
            <Globe2 className="size-12 text-muted-foreground" aria-hidden="true" />
          </div>
          <div className="flex size-12 items-center justify-center rounded-full border border-border bg-card text-muted-foreground shadow-sm">
            <ArrowRight className="size-6" aria-hidden="true" />
          </div>
          <div className="flex size-32 items-center justify-center rounded-full border border-primary/30 bg-primary/10 shadow-xl shadow-primary/10 backdrop-blur-md">
            <img src={MAHO_LOGO_SRC} alt="" width="72" height="72" className="size-20" />
          </div>
        </div>
      </div>
    </div>
  );
}

function DefaultBrowserChoice({store, setAsDefaultRequested, onSetAsDefaultChange}: {
  store: MahoWelcomeStore;
  setAsDefaultRequested: boolean;
  onSetAsDefaultChange: (value: boolean) => void;
}) {
  return (
    <div className="flex h-full w-full flex-col items-center justify-center gap-6 rounded-xl border border-border bg-background/50 p-8 text-center shadow-sm">
      <div className="relative flex size-52 items-center justify-center rounded-3xl border border-border bg-card shadow-2xl shadow-black/20" aria-hidden="true">
        <div className="absolute inset-0 rounded-3xl bg-gradient-to-br from-primary/10 via-transparent to-muted/40" />
        <img src={MAHO_LOGO_SRC} alt="" width="176" height="176" className="relative size-44 rounded-[2rem] drop-shadow-2xl" />
      </div>
      <div className="flex max-w-sm items-center gap-3 rounded-xl border border-border bg-card/80 px-4 py-3 text-left shadow-sm transition hover:bg-muted/50">
        <Checkbox
          id="default-browser-choice"
          checked={setAsDefaultRequested}
          onCheckedChange={checked => onSetAsDefaultChange(checked === true)}
          aria-label={store.getString('IDS_MAHO_WELCOME_DEFAULT_BROWSER_YES')}
          className="mt-0.5"
        />
        <span className="flex flex-col gap-1">
          <Label htmlFor="default-browser-choice" className="cursor-pointer text-sm font-medium text-foreground">
            {store.getString('IDS_MAHO_WELCOME_DEFAULT_BROWSER_YES')}
          </Label>
          <span className="text-xs leading-5 text-muted-foreground">
            <CheckCircle2 className="mr-1 inline size-3.5" aria-hidden="true" />
            You can change this later in browser settings.
          </span>
        </span>
      </div>
    </div>
  );
}
