// Copyright 2026 Maho Browser. All rights reserved.

const makeMockFn = (resolvedValue?: any) => {
  if (typeof globalThis !== 'undefined' && 'vi' in globalThis) {
    const fn = (globalThis as any).vi.fn(() => 1);
    if (resolvedValue !== undefined) {
      fn.mockResolvedValue(resolvedValue);
    }
    return fn;
  }
  return () => resolvedValue;
};

const makeMockEvent = () => ({
  addListener: makeMockFn(),
});

export const callbackRouter = {
  onAccountsChanged: makeMockEvent(),
  onAuthRequired: makeMockEvent(),
  onAuthRefreshSucceeded: makeMockEvent(),
  onSyncEvent: makeMockEvent(),
  onBackfillEvent: makeMockEvent(),
  onStatusChanged: makeMockEvent(),
  onNewMail: makeMockEvent(),
  onMutation: makeMockEvent(),
  onOutbox: makeMockEvent(),
  onScheduler: makeMockEvent(),
  onAgentStream: makeMockEvent(),
  onCalendar: makeMockEvent(),
  onImport: makeMockEvent(),
  onBrowserUiPrefsChanged: makeMockEvent(),
  removeListener: makeMockFn(),
  $: {
    bindNewPipeAndPassRemote: makeMockFn(),
  }
};

export const handler = {
  getAppSetting: makeMockFn({ ok: true, resultJson: "null" }),
  setAppSetting: makeMockFn({ ok: true, resultJson: "null" }),
  getPendingMutationCount: makeMockFn({ ok: true, resultJson: "0" }),
  getDownloadDir: makeMockFn({ path: "/Downloads" }),
  $: {
    bindNewPipeAndPassReceiver: makeMockFn(),
  }
};
