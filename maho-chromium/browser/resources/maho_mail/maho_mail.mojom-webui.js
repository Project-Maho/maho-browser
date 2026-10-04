// Mock Mojo bindings for Vitest unit test environment
export class PageHandlerRemote {
  constructor() {
    this.$ = {
      bindNewPipeAndPassReceiver: () => {}
    };
  }
  getAppSetting() { return Promise.resolve({ ok: true, resultJson: "null" }); }
  setAppSetting() { return Promise.resolve({ ok: true, resultJson: "null" }); }
  getPendingMutationCount() { return Promise.resolve({ ok: true, resultJson: "0" }); }
  getDownloadDir() { return Promise.resolve({ path: "/Downloads" }); }
}

export class PageCallbackRouter {
  constructor() {
    const makeMockEvent = () => ({ addListener: () => 1 });
    this.onAccountsChanged = makeMockEvent();
    this.onAuthRequired = makeMockEvent();
    this.onAuthRefreshSucceeded = makeMockEvent();
    this.onSyncEvent = makeMockEvent();
    this.onBackfillEvent = makeMockEvent();
    this.onStatusChanged = makeMockEvent();
    this.onNewMail = makeMockEvent();
    this.onMutation = makeMockEvent();
    this.onOutbox = makeMockEvent();
    this.onScheduler = makeMockEvent();
    this.onAgentStream = makeMockEvent();
    this.onCalendar = makeMockEvent();
    this.onImport = makeMockEvent();
    this.onBrowserUiPrefsChanged = makeMockEvent();
    this.removeListener = () => {};
    this.$ = {
      bindNewPipeAndPassRemote: () => {}
    };
  }
}

export class PageHandlerFactory {
  static getRemote() {
    return {
      createPageHandler: () => {}
    };
  }
}
