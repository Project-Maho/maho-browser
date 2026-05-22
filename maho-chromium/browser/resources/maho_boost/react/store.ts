import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../maho_boost.mojom-webui.js';
import type {SiteRule} from '../maho_boost.mojom-webui.js';
import {notifyListeners} from '//resources/maho_common/react/store_utils.js';
import type {Listener} from '//resources/maho_common/react/store_utils.js';
import {cloneState, createInitialState} from './types.js';
import type {BoostState} from './types.js';

export class MahoBoostStore {
  private state: BoostState = createInitialState();
  private readonly listeners = new Set<Listener>();
  private readonly pageHandler: PageHandlerRemote;
  private readonly callbackRouter: PageCallbackRouter;

  constructor() {
    this.callbackRouter = new PageCallbackRouter();
    this.pageHandler = new PageHandlerRemote();
    const factory = PageHandlerFactory.getRemote();
    factory.createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        this.pageHandler.$.bindNewPipeAndPassReceiver());

    this.callbackRouter.rulesChanged.addListener((rule: SiteRule) => {
      this.patch(draft => {
        draft.rule = rule;
        draft.cssDraft = rule.cssRules;
      });
    });
  }

  getSnapshot(): BoostState {
    return cloneState(this.state);
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  async bootstrap(domain: string): Promise<void> {
    this.patch(draft => {
      draft.loading = true;
      draft.status = '';
    });
    try {
      const {rule} = await this.pageHandler.getRulesForSite(domain);
      this.patch(draft => {
        draft.rule = rule;
        draft.cssDraft = rule.cssRules;
        draft.loading = false;
      });
    } catch {
      this.patch(draft => {
        draft.loading = false;
        draft.status = 'Failed to load Boost.';
        draft.statusTone = 'error';
      });
    }
  }

  updateCssDraft(css: string): void {
    this.patch(draft => {
      draft.cssDraft = css;
    });
  }

  async saveCss(): Promise<void> {
    const {rule, cssDraft} = this.state;
    if (!rule) {
      return;
    }
    await this.withBusy(async () => {
      const {success} = await this.pageHandler.setCssRules(rule.domain, cssDraft);
      this.patch(draft => {
        draft.status = success ? 'CSS saved.' : 'Could not save CSS.';
        draft.statusTone = success ? 'success' : 'error';
        if (success && draft.rule) {
          draft.rule.cssRules = cssDraft;
        }
      });
    });
  }

  async toggleJs(enabled: boolean): Promise<void> {
    const {rule} = this.state;
    if (!rule) {
      return;
    }
    await this.withBusy(async () => {
      const {success} = await this.pageHandler.toggleJs(rule.domain, enabled);
      this.patch(draft => {
        draft.status = success ? (enabled ? 'JavaScript enabled.' : 'JavaScript disabled.') : 'Could not update JavaScript setting.';
        draft.statusTone = success ? 'success' : 'error';
        if (success && draft.rule) {
          draft.rule.jsEnabled = enabled;
        }
      });
    });
  }

  async toggleAds(blocked: boolean): Promise<void> {
    const {rule} = this.state;
    if (!rule) {
      return;
    }
    await this.withBusy(async () => {
      const {success} = await this.pageHandler.toggleAds(rule.domain, blocked);
      this.patch(draft => {
        draft.status = success ? (blocked ? 'Ads blocked.' : 'Ads allowed.') : 'Could not update ad setting.';
        draft.statusTone = success ? 'success' : 'error';
        if (success && draft.rule) {
          draft.rule.adsBlocked = blocked;
        }
      });
    });
  }

  private async withBusy(action: () => Promise<void>): Promise<void> {
    this.patch(draft => {
      draft.busy = true;
    });
    try {
      await action();
    } finally {
      this.patch(draft => {
        draft.busy = false;
      });
    }
  }

  private patch(mutator: (draft: BoostState) => void): void {
    const draft = cloneState(this.state);
    mutator(draft);
    this.state = draft;
    notifyListeners(this.listeners);
  }
}
