// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import type {MahoWelcomeStore} from '../store.js';
import type {ImportStage} from '../types.js';

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
    <div className="welcome-import-stage-a">
      <div className="welcome-import-graphic">
        <div className="welcome-import-graphic__icon-group">
          <div className="welcome-import-graphic__circle welcome-import-graphic__circle--source"></div>
          <div className="welcome-import-graphic__arrow">➔</div>
          <div className="welcome-import-graphic__circle welcome-import-graphic__circle--maho"></div>
        </div>
      </div>
    </div>
  );
}

function DefaultBrowserChoice({store}: {
  store: MahoWelcomeStore;
  setAsDefaultRequested: boolean;
  onSetAsDefaultChange: (value: boolean) => void;
}) {
  void store;
  return (
    <div className="welcome-default-browser">
      <div className="welcome-default-browser__icon" aria-hidden="true">
        <svg width="120" height="120" viewBox="0 0 24 24" fill="none"
             stroke="currentColor" strokeWidth="1.4"
             strokeLinecap="round" strokeLinejoin="round">
          <circle cx="12" cy="12" r="10"/>
          <path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/>
          <path d="M2 12h20"/>
        </svg>
      </div>
    </div>
  );
}
