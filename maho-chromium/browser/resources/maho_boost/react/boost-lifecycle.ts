import type {BoostInfo, PageHandlerRemote} from '../maho_boost.mojom-webui.js';

export type BoostOwnership = 'persisted' | 'temporary';

export interface CloseBoostLifecycleOptions {
  readonly closeDialog: boolean;
  readonly flushPendingUpdates: () => Promise<void>;
  readonly getBoost: () => BoostInfo | null;
  readonly getOwnership: () => BoostOwnership;
  readonly markClosed: () => void;
  readonly pageHandler: PageHandlerRemote;
  readonly settleUpdates: () => Promise<void>;
}

export interface DeleteBoostLifecycleOptions {
  readonly cancelPendingUpdates: () => void;
  readonly closeDialog: boolean;
  readonly getBoost: () => BoostInfo | null;
  readonly pageHandler: PageHandlerRemote;
  readonly settleUpdates: () => Promise<void>;
}

async function exitEditModes(pageHandler: PageHandlerRemote): Promise<void> {
  await Promise.all([pageHandler.exitZapMode(), pageHandler.exitPickerMode()]);
}

export async function closeBoostLifecycle(options: CloseBoostLifecycleOptions): Promise<void> {
  await exitEditModes(options.pageHandler);
  await options.flushPendingUpdates();
  await options.settleUpdates();
  options.markClosed();

  const boost = options.getBoost();
  if (boost?.changeWasMade) {
    await options.pageHandler.commitBoost(boost.id);
  } else if (boost && options.getOwnership() === 'temporary') {
    await options.pageHandler.discardBoost(boost.id);
  }

  if (options.closeDialog) {
    await options.pageHandler.closeDialog();
  }
}

export async function deleteBoostLifecycle(options: DeleteBoostLifecycleOptions): Promise<boolean> {
  options.cancelPendingUpdates();
  await options.settleUpdates();
  await exitEditModes(options.pageHandler);

  const boost = options.getBoost();
  if (!boost) {
    return false;
  }
  const {success} = await options.pageHandler.deleteBoost(boost.id);
  if (success && options.closeDialog) {
    await options.pageHandler.closeDialog();
  }
  return success;
}
