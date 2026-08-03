// Copyright 2026 Maho Browser. All rights reserved.

import * as React from 'react';
import {createRoot} from 'react-dom/client';

import {WindowMode} from '../maho_boost.mojom-webui.js';
import {createBoostUpdate} from './boost-state.js';
import {BoostEditorRoute} from './components/boost-editor-route.js';
import {CodeRoute} from './components/code-route.js';
import {
  BoostToaster,
  StatusFeedback,
  type StatusFeedbackState,
} from './components/status-feedback.js';
import {useBoostActions} from './use-boost-actions.js';
import {useBoostController} from './use-boost-controller.js';

const APP_ID = 'app';

function App(): React.JSX.Element {
  const controller = useBoostController();
  const actions = useBoostActions(controller);
  const {state} = controller;
  const boost = state.boost;
  const boostActive = state.mode === WindowMode.kBoost;
  const surfaceDisabled = actions.busy || state.isLoading || !boost;
  const visibleStatus: StatusFeedbackState = state.error ?
    {kind: 'error', message: state.error} : state.isLoading ?
      {kind: 'loading', message: 'Loading Boost'} : actions.status;

  React.useEffect(() => {
    const closeOpenSurface = (event: KeyboardEvent): void => {
      if (event.key !== 'Escape' || event.defaultPrevented) {
        return;
      }

      const renameInput = document.querySelector<HTMLInputElement>(
          '#zen-boost-name-container[aria-label="Rename Boost"]');
      if (renameInput && document.activeElement === renameInput) {
        return;
      }

      const menuTrigger = document.querySelector<HTMLButtonElement>(
          '#zen-boost-name-container[aria-expanded="true"]');
      const popoverTrigger = document.querySelector<HTMLButtonElement>(
          '#zen-boost-controls[aria-expanded="true"]');
      const trigger = menuTrigger ?? popoverTrigger;
      if (!trigger) {
        return;
      }

      event.preventDefault();
      event.stopPropagation();
      trigger.click();
      window.requestAnimationFrame(() => trigger.focus());
    };

    document.addEventListener('keydown', closeOpenSurface, {capture: true});
    return () => document.removeEventListener('keydown', closeOpenSurface, true);
  }, []);

  return (
    <div
      aria-busy={visibleStatus.kind === 'loading'}
      className="boost-shell relative overflow-hidden bg-background text-foreground"
      data-active-boost-id={state.activeBoostId ?? ''}
      data-mode={boostActive ? 'boost' : 'code'}
      data-selected-boost-id={state.selectedBoostId ?? ''}
      data-site-boost-enabled={state.activeBoostId !== null ? 'true' : 'false'}>
      {boostActive ? (
        <BoostEditorRoute
          actions={actions}
          active
          controller={controller}
          disabled={surfaceDisabled}
        />
      ) : (
        <CodeRoute
          active
          css={boost?.customCss ?? ''}
          disabled={surfaceDisabled}
          editorFocusRequest={controller.editorFocusRequest}
          inspectorPending={actions.pendingAction === 'inspector'}
          pickerActive={state.pickerModeEnabled}
          onBack={() => void actions.runAction({
            action: () => controller.setMode(WindowMode.kBoost),
            loadingMessage: 'Returning to Boost controls',
            pending: 'code',
          })}
          onCssChange={customCss => controller.applyUpdate(
              'custom-css', createBoostUpdate({customCss}))}
          onOpenInspector={() => void actions.runAction({
            action: controller.openInspector,
            loadingMessage: 'Opening Inspector',
            pending: 'inspector',
          })}
          onPickerActiveChange={() => void actions.runAction({
            action: controller.togglePicker,
            loadingMessage: state.pickerModeEnabled ? 'Exiting picker mode' : 'Entering picker mode',
            pending: 'picker',
          })}
        />
      )}
      <div className="boost-status-layer">
        <StatusFeedback state={visibleStatus} />
      </div>
      <BoostToaster />
      <input
        ref={actions.importInputRef}
        accept="application/json,.json"
        aria-hidden="true"
        className="hidden"
        tabIndex={-1}
        type="file"
        onChange={() => void actions.handleImport()}
      />
    </div>
  );
}

function mount(): void {
  const existingRoot = document.getElementById(APP_ID);
  const root = existingRoot ?? document.body.appendChild(document.createElement('div'));
  if (!existingRoot) {
    root.id = APP_ID;
  }
  if (!window.MahoCodeMirror) {
    createRoot(root).render(
        <div
          aria-busy={false}
          className="boost-shell relative overflow-hidden bg-background text-foreground"
          data-mode="boost">
          <main className="boost-route" aria-label="Boost editor unavailable">
            <div className="boost-status-layer">
              <StatusFeedback state={{kind: 'error', message: 'CodeMirror failed to load.'}} />
            </div>
          </main>
        </div>);
    return;
  }
  createRoot(root).render(<App />);
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', mount, {once: true});
} else {
  mount();
}
