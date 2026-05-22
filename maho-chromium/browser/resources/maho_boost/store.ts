import {BoostEntry, PageCallbackRouter, PageHandlerRemote} from './maho_boost.mojom-webui.js';
import {BoostEditorTab, CurrentPageContext, State, StatusTone, cloneState, createInitialState} from './types.js';

type Listener = (state: State) => void;

function getSuggestedPattern(context: CurrentPageContext|null): string {
  return context?.suggestedPattern ?? '';
}

function parseCurrentPageContext(): CurrentPageContext|null {
  const rawUrl = new URL(window.location.href).searchParams.get('url');
  if (!rawUrl) {
    return null;
  }

  try {
    const parsed = new URL(rawUrl);
    if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') {
      return null;
    }

    return {
      url: parsed.toString(),
      siteLabel: parsed.hostname.replace(/^www\./, '') || parsed.host,
      suggestedPattern: `${parsed.origin}/*`,
    };
  } catch {
    return null;
  }
}

function isSuccessfulResult(result: unknown): boolean {
  if (typeof result === 'boolean') {
    return result;
  }

  if (typeof result === 'object' && result !== null && 'success' in result) {
    return Boolean((result as {success?: unknown}).success);
  }

  return result === undefined;
}

export class MahoBoostStore {
  private state = createInitialState();
  private listeners = new Set<Listener>();

  constructor(
      readonly pageHandler: PageHandlerRemote,
      private readonly callbackRouter: PageCallbackRouter) {
    this.bindCallbacks();
  }

  getSnapshot(): State {
    return cloneState(this.state);
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    listener(this.getSnapshot());
    return () => this.listeners.delete(listener);
  }

  async bootstrap(): Promise<void> {
    const context = parseCurrentPageContext();
    this.patch(state => {
      state.context = context;
      if (context) {
        const suggestedPattern = getSuggestedPattern(context);
        state.createPattern = suggestedPattern;
        state.templatePattern = suggestedPattern;
      }
    });
    await this.loadData();
  }

  setCreatePattern(pattern: string) {
    this.patch(state => {
      state.createPattern = pattern;
    });
  }

  setTemplatePattern(pattern: string) {
    this.patch(state => {
      state.templatePattern = pattern;
    });
  }

  setSelectedTemplateId(templateId: string) {
    this.patch(state => {
      state.selectedTemplateId = templateId;
    });
  }

  setSelectedBoost(boostId: string) {
    this.patch(state => {
      state.selection.selectedBoostId = boostId;
    });
  }

  setSelectedEditorTab(tab: BoostEditorTab) {
    this.patch(state => {
      state.selection.activeEditorTab = tab;
    });
  }

  updateBoostPatternDraft(boostId: string, pattern: string) {
    this.updateBoostDraft(boostId, boost => {
      boost.urlPattern = pattern;
    });
  }

  updateBoostCssDraft(boostId: string, css: string) {
    this.updateBoostDraft(boostId, boost => {
      boost.customCss = css;
    });
  }

  updateBoostJsDraft(boostId: string, js: string) {
    this.updateBoostDraft(boostId, boost => {
      boost.customJs = js;
    });
  }

  updateBoostColorThemeDraft(boostId: string, colorThemeJson: string) {
    this.updateBoostDraft(boostId, boost => {
      boost.colorThemeJson = colorThemeJson;
    });
  }

  updateBoostFontOverrideDraft(boostId: string, fontOverrideJson: string) {
    this.updateBoostDraft(boostId, boost => {
      boost.fontOverrideJson = fontOverrideJson;
    });
  }

  async createBlankBoost(): Promise<void> {
    const pattern = this.state.createPattern.trim();
    if (!pattern) {
      this.setStatus('Enter a URL pattern.', 'error');
      return;
    }

    await this.withBusy(async () => {
      const {success} = await this.pageHandler.createBoostForPattern(pattern);
      if (!success) {
        this.setStatus('Could not create boost.', 'error');
        return;
      }

      this.patch(state => {
        state.createPattern = getSuggestedPattern(state.context);
      });
      await this.loadData('Boost created.', 'success');
    });
  }

  async createBoostFromTemplate(): Promise<void> {
    const pattern = this.state.templatePattern.trim();
    const templateId = this.state.selectedTemplateId;

    if (!templateId) {
      this.setStatus('Select a template.', 'error');
      return;
    }

    if (!pattern) {
      this.setStatus('Enter a URL pattern for the template boost.', 'error');
      return;
    }

    await this.withBusy(async () => {
      const {success} = await this.pageHandler.createBoostFromTemplate(templateId, pattern);
      if (!success) {
        this.setStatus('Could not create boost from template.', 'error');
        return;
      }

      this.patch(state => {
        state.templatePattern = getSuggestedPattern(state.context);
      });
      await this.loadData('Template boost created.', 'success');
    });
  }

  async saveBoostPattern(boostId: string): Promise<void> {
    const boost = this.getBoost(boostId);
    if (!boost) {
      return;
    }

    const pattern = boost.urlPattern.trim();
    if (!pattern) {
      this.setStatus('Enter a URL pattern.', 'error');
      return;
    }

    await this.withBusy(async () => {
      const {success} = await this.pageHandler.updateBoostPattern(boostId, pattern);
      if (!success) {
        this.setStatus('Could not save URL pattern.', 'error');
        return;
      }

      await this.loadData('URL pattern saved.', 'success');
    });
  }

  async saveBoostCss(boostId: string): Promise<void> {
    const boost = this.getBoost(boostId);
    if (!boost) {
      return;
    }

    await this.withBusy(async () => {
      const {success} = await this.pageHandler.updateBoostCss(boostId, boost.customCss);
      if (!success) {
        this.setStatus('Could not save CSS.', 'error');
        return;
      }

      await this.loadData('CSS saved.', 'success');
    });
  }

  async saveBoostJs(boostId: string): Promise<void> {
    const boost = this.getBoost(boostId);
    if (!boost) {
      return;
    }

    await this.withBusy(async () => {
      const {success} = await this.pageHandler.updateBoostJs(boostId, boost.customJs);
      if (!success) {
        this.setStatus('Could not save JS.', 'error');
        return;
      }

      await this.loadData('JS saved.', 'success');
    });
  }

  async saveBoostColorTheme(boostId: string): Promise<void> {
    const boost = this.getBoost(boostId);
    if (!boost) {
      return;
    }

    await this.withBusy(async () => {
      const {success} = await this.pageHandler.updateBoostColorTheme(
          boostId, boost.colorThemeJson);
      if (!success) {
        this.setStatus('Could not save color theme.', 'error');
        return;
      }

      await this.loadData('Color theme saved.', 'success');
    });
  }

  async saveBoostFontOverride(boostId: string): Promise<void> {
    const boost = this.getBoost(boostId);
    if (!boost) {
      return;
    }

    await this.withBusy(async () => {
      const {success} = await this.pageHandler.updateBoostFontOverride(
          boostId, boost.fontOverrideJson);
      if (!success) {
        this.setStatus('Could not save font override.', 'error');
        return;
      }

      await this.loadData('Font override saved.', 'success');
    });
  }

  async toggleBoost(boostId: string, enabled: boolean): Promise<void> {
    await this.withBusy(async () => {
      const {success} = await this.pageHandler.toggleBoost(boostId, enabled);
      if (!success) {
        this.setStatus('Could not update boost.', 'error');
        return;
      }

      await this.loadData(enabled ? 'Boost enabled.' : 'Boost disabled.', 'success');
    });
  }

  async deleteBoost(boostId: string): Promise<void> {
    await this.withBusy(async () => {
      const {success} = await this.pageHandler.deleteBoost(boostId);
      if (!success) {
        this.setStatus('Could not delete boost.', 'error');
        return;
      }

      await this.loadData('Boost deleted.', 'success');
    });
  }

  async enterZapMode(boostId: string): Promise<void> {
    if (!this.state.context) {
      this.setStatus(
          'Open Boost manager from the current page to zap live elements.',
          'error');
      return;
    }

    this.patch(state => {
      state.selection.selectedBoostId = boostId;
      state.selection.activeEditorTab = 'zap';
      state.zapModeBoostId = boostId;
      state.status = `Zap mode is live for ${state.context?.siteLabel ?? 'this site'}. Click an element on the page to save its selector.`;
      state.statusTone = 'neutral';
    });

    try {
      const result = await this.pageHandler.enterZapMode(boostId);
      if (!isSuccessfulResult(result)) {
        this.patch(state => {
          state.zapModeBoostId = '';
          state.status = 'Could not enter zap mode.';
          state.statusTone = 'error';
        });
      }
    } catch {
      this.patch(state => {
        state.zapModeBoostId = '';
        state.status = 'Zap mode is not available yet.';
        state.statusTone = 'error';
      });
    }
  }

  async removeZapSelector(boostId: string, selector: string): Promise<void> {
    await this.withBusy(async () => {
      const {success} = await this.pageHandler.removeZapSelector(boostId, selector);
      if (!success) {
        this.setStatus('Could not remove zap selector.', 'error');
        return;
      }

      await this.loadData('Zap selector removed.', 'success');
    });
  }

  private bindCallbacks() {
    this.callbackRouter.onZapSelectorPicked.addListener((_boostId, selector) => {
      if (!selector.trim()) {
        this.patch(state => {
          state.zapModeBoostId = '';
          state.status = 'Zap mode finished without saving a selector.';
          state.statusTone = 'neutral';
        });
        return;
      }

      this.patch(state => {
        state.zapModeBoostId = '';
      });
      void this.loadData('Zap selector saved.', 'success');
    });
  }

  private getBoost(boostId: string): BoostEntry|undefined {
    return this.state.boosts.find(candidate => candidate.id === boostId);
  }

  private updateBoostDraft(
      boostId: string,
      updater: (boost: BoostEntry) => void) {
    this.patch(state => {
      const boost = state.boosts.find(candidate => candidate.id === boostId);
      if (!boost) {
        return;
      }
      state.selection.selectedBoostId = boostId;
      updater(boost);
    });
  }

  private async loadData(
      statusMessage = '',
      tone: StatusTone = 'neutral'): Promise<void> {
    this.patch(state => {
      state.loading = true;
      if (statusMessage) {
        state.status = statusMessage;
        state.statusTone = tone;
      }
    });

    try {
      const boostsPromise = this.state.context ?
          this.pageHandler.getBoostsForUrl(this.state.context.url) :
          this.pageHandler.getBoosts();
      const [{boosts}, {templates}] = await Promise.all([
        boostsPromise,
        this.pageHandler.getTemplates(),
      ]);

      this.patch(state => {
        state.boosts = boosts.map(boost => ({
          ...boost,
          zapSelectors: [...(boost.zapSelectors ?? [])],
        }));
        state.templates = templates.map(template => ({...template}));

        if (!templates.length) {
          state.selectedTemplateId = '';
        } else if (!state.selectedTemplateId ||
                   !templates.some(template => template.id === state.selectedTemplateId)) {
          state.selectedTemplateId = templates[0]!.id;
        }

        if (!boosts.length) {
          state.selection.selectedBoostId = '';
        } else if (!state.selection.selectedBoostId ||
                   !boosts.some(boost => boost.id === state.selection.selectedBoostId)) {
          state.selection.selectedBoostId = boosts[0]!.id;
        }
      });
    } catch {
      this.setStatus('Failed to load boosts.', 'error');
    } finally {
      this.patch(state => {
        state.loading = false;
      });
    }
  }

  private setStatus(message: string, tone: StatusTone = 'neutral') {
    this.patch(state => {
      state.status = message;
      state.statusTone = tone;
    });
  }

  private async withBusy(action: () => Promise<void>): Promise<void> {
    this.patch(state => {
      state.busy = true;
    });
    try {
      await action();
    } finally {
      this.patch(state => {
        state.busy = false;
      });
    }
  }

  private patch(mutator: (state: State) => void) {
    const draft = cloneState(this.state);
    mutator(draft);
    this.state = draft;
    const snapshot = this.getSnapshot();
    for (const listener of this.listeners) {
      listener(snapshot);
    }
  }
}
