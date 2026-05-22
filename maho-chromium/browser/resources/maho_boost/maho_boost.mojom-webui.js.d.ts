export interface SiteRule {
  id: string;
  domain: string;
  cssRules: string;
  jsEnabled: boolean;
  adsBlocked: boolean;
}

export class PageCallbackRouter {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
  rulesChanged: {
    addListener(listener: (rule: SiteRule) => void): void;
  };
}

export class PageHandlerRemote {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  getRulesForSite(domain: string): Promise<{rule: SiteRule}>;
  setCssRules(domain: string, css: string): Promise<{success: boolean}>;
  toggleJs(domain: string, enabled: boolean): Promise<{success: boolean}>;
  toggleAds(domain: string, blocked: boolean): Promise<{success: boolean}>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
