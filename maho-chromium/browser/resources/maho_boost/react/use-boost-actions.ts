import * as React from 'react';
import {toast} from 'sonner';

import type {BoostSelectionProps} from './boost-state.js';
import type {StatusFeedbackState} from './components/status-feedback.js';
import type {
  TitleActionAvailability,
  TitleStripProps,
} from './components/title-strip.js';
import type {BoostController} from './use-boost-controller.js';

export type PendingAction =
  | 'close'
  | 'code'
  | 'delete'
  | 'export'
  | 'import'
  | 'inspector'
  | 'picker'
  | 'rename'
  | 'reset'
  | 'select'
  | 'shuffle'
  | 'site-activation'
  | 'zap'
  | null;

export interface ActionRequest {
  readonly action: () => Promise<void>;
  readonly loadingMessage: string;
  readonly pending: Exclude<PendingAction, null>;
  readonly successMessage?: string;
}

export interface BoostActions {
  readonly busy: boolean;
  readonly handleImport: () => Promise<void>;
  readonly importInputRef: React.RefObject<HTMLInputElement | null>;
  readonly pendingAction: PendingAction;
  readonly runAction: (request: ActionRequest) => Promise<void>;
  readonly status: StatusFeedbackState;
  readonly titleProps: TitleStripProps & BoostSelectionProps;
}

class BoostActionError extends Error {
  readonly name = 'BoostActionError';
}

function safeFileName(value: string): string {
  const fileName = value.trim().replace(/[^a-z0-9-_]+/gi, '-').replace(/^-+|-+$/g, '');
  return fileName || 'boost';
}

function parseJsonObject(json: string, invalidMessage: string): string {
  try {
    const parsed: unknown = JSON.parse(json);
    if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
      throw new BoostActionError(invalidMessage);
    }
  } catch (error: unknown) {
    if (error instanceof SyntaxError) {
      throw new BoostActionError(invalidMessage);
    }
    throw error;
  }
  return json;
}

export function useBoostActions(controller: BoostController): BoostActions {
  const {state} = controller;
  const boost = state.boost;
  const [pendingAction, setPendingAction] = React.useState<PendingAction>(null);
  const [status, setStatus] = React.useState<StatusFeedbackState>({kind: 'idle'});
  const importInputRef = React.useRef<HTMLInputElement | null>(null);
  const pendingActionRef = React.useRef<Exclude<PendingAction, null> | null>(null);
  const busy = state.isLoading || pendingAction !== null;

  const runAction = React.useCallback(async (request: ActionRequest): Promise<void> => {
    if (pendingActionRef.current !== null) {
      return;
    }
    pendingActionRef.current = request.pending;
    setPendingAction(request.pending);
    setStatus({kind: 'loading', message: request.loadingMessage});
    try {
      await request.action();
      setStatus({kind: 'idle'});
      if (request.successMessage) {
        toast.success(request.successMessage);
      }
    } catch (error: unknown) {
      setStatus({kind: 'error', message: error instanceof Error ? error.message : 'The Boost action failed.'});
    } finally {
      pendingActionRef.current = null;
      setPendingAction(null);
    }
  }, []);

  const handleImport = React.useCallback(async (): Promise<void> => {
    const input = importInputRef.current;
    const file = input?.files?.[0];
    const pageHandler = controller.pageHandler;
    if (!file) {
      return;
    }
    if (!pageHandler) {
      input.value = '';
      setStatus({kind: 'error', message: 'Boost actions are unavailable.'});
      return;
    }
    await runAction({
      action: async () => {
        const json = parseJsonObject(
            await file.text(), 'The selected file did not contain a valid Boost.');
        await controller.importBoost(json);
      },
      loadingMessage: 'Importing Boost',
      pending: 'import',
      successMessage: 'Boost imported',
    });
    if (input) {
      input.value = '';
    }
  }, [controller, runAction]);

  const pageHandler = controller.pageHandler;
  const availability: TitleActionAvailability = {
    delete: boost !== null && pageHandler !== null,
    export: boost !== null && pageHandler !== null,
    import: pageHandler !== null,
    rename: boost !== null && pageHandler !== null,
    reset: boost?.changeWasMade === true && pageHandler !== null,
    shuffle: boost !== null && pageHandler !== null,
  };

  const titleProps: TitleStripProps & BoostSelectionProps = {
    activeBoostId: state.activeBoostId,
    availability,
    boostName: boost?.name ?? 'Boost',
    boosts: state.boosts.map(item => ({id: item.id, name: item.name})),
    busy,
    onClose: () => void runAction({
      action: controller.close,
      loadingMessage: 'Closing Boost editor',
      pending: 'close',
    }),
    onDelete: () => {
      void runAction({
        action: async () => {
          if (!await controller.deleteBoost()) {
            throw new BoostActionError('Boost could not be deleted.');
          }
        },
        loadingMessage: 'Deleting Boost',
        pending: 'delete',
      });
    },
    onExport: () => {
      const pageHandler = controller.pageHandler;
      if (!boost || !pageHandler) {
        return;
      }
      void runAction({
        action: async () => {
          const {json} = await pageHandler.exportBoost(boost.id);
          parseJsonObject(json, 'Boost could not be exported.');
          const url = URL.createObjectURL(new Blob([json], {type: 'application/json'}));
          const anchor = document.createElement('a');
          anchor.href = url;
          anchor.download = `boost-${safeFileName(boost.name)}.json`;
          anchor.hidden = true;
          document.body.append(anchor);
          try {
            anchor.click();
          } finally {
            anchor.remove();
            URL.revokeObjectURL(url);
          }
        },
        loadingMessage: 'Exporting Boost',
        pending: 'export',
        successMessage: 'Boost exported',
      });
    },
    onImport: () => {
      if (importInputRef.current) {
        importInputRef.current.value = '';
        importInputRef.current.click();
      }
    },
    onRename: name => {
      if (!boost) {
        return;
      }
      void runAction({
        action: () => controller.renameBoost(name),
        loadingMessage: 'Renaming Boost',
        pending: 'rename',
        successMessage: 'Boost renamed',
      });
    },
    onReset: () => {
      if (!boost) {
        return;
      }
      void runAction({
        action: controller.resetBoost,
        loadingMessage: 'Resetting Boost',
        pending: 'reset',
        successMessage: 'Boost reset',
      });
    },
    onShuffle: () => {
      if (!boost) {
        return;
      }
      void runAction({
        action: controller.shuffleBoost,
        loadingMessage: 'Shuffling Boost',
        pending: 'shuffle',
        successMessage: 'Boost shuffled',
      });
    },
    onSelectBoost: boostId => {
      void runAction({
        action: () => controller.selectBoost(boostId),
        loadingMessage: 'Selecting Boost',
        pending: 'select',
      });
    },
    onSiteBoostEnabledChange: enabled => {
      void runAction({
        action: () => controller.setSiteBoostEnabled(enabled),
        loadingMessage: enabled ? 'Turning on Boost' : 'Turning off Boost',
        pending: 'site-activation',
      });
    },
    selectedBoostId: state.selectedBoostId,
  };

  return {
    busy,
    handleImport,
    importInputRef,
    pendingAction,
    runAction,
    status,
    titleProps,
  };
}
