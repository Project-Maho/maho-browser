import React, {useEffect, useRef, useSyncExternalStore} from 'react';
import {createRoot} from 'react-dom/client';

import type {SiteRule} from '../maho_boost.mojom-webui.js';
import {joinClassNames} from '//resources/maho_common/react/primitives/join_class_names.js';
import {MahoBoostStore} from './store.js';
import type {BoostState} from './types.js';

function parseDomainFromUrl(): string {
  const rawUrl = new URL(window.location.href).searchParams.get('url');
  if (!rawUrl) {
    return 'example.com';
  }
  try {
    const parsed = new URL(rawUrl);
    return parsed.hostname || 'example.com';
  } catch {
    return 'example.com';
  }
}

function BoostApp({store, domain}: {store: MahoBoostStore; domain: string}) {
  const state = useSyncExternalStore(
      listener => store.subscribe(listener),
      () => store.getSnapshot());

  const bootstrapped = useRef(false);
  useEffect(() => {
    if (!bootstrapped.current) {
      bootstrapped.current = true;
      void store.bootstrap(domain);
    }
  }, [store, domain]);

  if (state.loading) {
    return (
      <div className="boost-status-screen">
        <p style={{color: '#888', fontSize: '14px', padding: '24px 32px'}}>
          Loading Boost\u2026
        </p>
      </div>
    );
  }

  if (!state.rule) {
    return (
      <div className="boost-status-screen">
        <p style={{color: '#e94560', fontSize: '14px', padding: '24px 32px'}}>
          {state.status || 'Failed to load Boost.'}
        </p>
      </div>
    );
  }

  return (
    <div style={{display: 'flex', flexDirection: 'column', minHeight: '100vh'}}>
      <SiteHeader rule={state.rule} />
      <BoostControls state={state} store={store} />
    </div>
  );
}

function SiteHeader({rule}: {rule: SiteRule}) {
  return (
    <div style={{padding: '24px 32px', borderBottom: '1px solid rgba(255,255,255,0.1)'}}>
      <h1 style={{margin: '0 0 8px 0', color: '#e94560', fontSize: '24px'}}>Boost</h1>
      <div style={{fontSize: '14px', color: '#888'}}>
        {'Site: '}<strong style={{color: '#e0e0e0'}}>{rule.domain}</strong>
      </div>
    </div>
  );
}

function BoostControls({state, store}: {state: BoostState; store: MahoBoostStore}) {
  const rule = state.rule!;
  return (
    <div style={{padding: '24px 32px'}}>
      <ToggleRow
        label="JavaScript Enabled"
        value={rule.jsEnabled}
        disabled={state.busy}
        onChange={v => void store.toggleJs(v)}
      />
      <ToggleRow
        label="Block Ads"
        value={rule.adsBlocked}
        disabled={state.busy}
        onChange={v => void store.toggleAds(v)}
      />
      <div style={{fontSize: '14px', fontWeight: 500, margin: '16px 0 8px 0'}}>
        Custom CSS Rules
      </div>
      <textarea
        value={state.cssDraft}
        placeholder="Enter custom CSS for this site..."
        disabled={state.busy}
        onChange={e => store.updateCssDraft(e.currentTarget.value)}
        style={{
          width: '100%',
          minHeight: '200px',
          background: 'rgba(255,255,255,0.05)',
          border: '1px solid rgba(255,255,255,0.1)',
          borderRadius: '8px',
          color: '#e0e0e0',
          padding: '12px',
          fontFamily: 'monospace',
          fontSize: '13px',
          resize: 'vertical',
          outline: 'none',
          boxSizing: 'border-box',
        }}
      />
      <button
        type="button"
        disabled={state.busy}
        onClick={() => void store.saveCss()}
        style={{
          marginTop: '8px',
          padding: '8px 20px',
          background: '#e94560',
          border: 'none',
          borderRadius: '6px',
          color: 'white',
          cursor: 'pointer',
          fontSize: '13px',
        }}>
        Save CSS
      </button>
      {state.status ? (
        <div className={joinClassNames('boost-status', `boost-status--${state.statusTone || 'info'}`)}>
          {state.status}
        </div>
      ) : null}
    </div>
  );
}

function ToggleRow(
    {label, value, disabled, onChange}: {
      label: string;
      value: boolean;
      disabled: boolean;
      onChange: (v: boolean) => void;
    }) {
  return (
    <div style={{
      display: 'flex',
      justifyContent: 'space-between',
      alignItems: 'center',
      padding: '12px 16px',
      background: 'rgba(255,255,255,0.05)',
      borderRadius: '8px',
      marginBottom: '8px',
    }}>
      <span style={{fontSize: '14px'}}>{label}</span>
      <button
        type="button"
        disabled={disabled}
        onClick={() => onChange(!value)}
        style={{
          padding: '6px 16px',
          border: 'none',
          borderRadius: '6px',
          cursor: 'pointer',
          fontSize: '13px',
          background: value ? '#e94560' : '#333',
          color: 'white',
        }}>
        {value ? 'ON' : 'OFF'}
      </button>
    </div>
  );
}

const rootElement = document.getElementById('app');

if (!rootElement) {
  throw new Error('Missing #app root for maho_boost.');
}

const store = new MahoBoostStore();
const domain = parseDomainFromUrl();
createRoot(rootElement).render(
    <React.StrictMode>
      <BoostApp store={store} domain={domain} />
    </React.StrictMode>);
