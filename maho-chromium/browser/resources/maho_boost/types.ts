import {BoostEntry, BoostTemplate} from './maho_boost.mojom-webui.js';

export type StatusTone = 'neutral'|'success'|'error';

export type BoostEditorTab = 'css'|'js'|'theme'|'font'|'zap';

export interface CurrentPageContext {
  url: string;
  siteLabel: string;
  suggestedPattern: string;
}

export interface BoostSelectionState {
  selectedBoostId: string;
  activeEditorTab: BoostEditorTab;
}

export interface State {
  boosts: BoostEntry[];
  templates: BoostTemplate[];
  createPattern: string;
  templatePattern: string;
  selectedTemplateId: string;
  loading: boolean;
  busy: boolean;
  zapModeBoostId: string;
  status: string;
  statusTone: StatusTone;
  context: CurrentPageContext|null;
  selection: BoostSelectionState;
}

export function createInitialState(): State {
  return {
    boosts: [],
    templates: [],
    createPattern: '',
    templatePattern: '',
    selectedTemplateId: '',
    loading: true,
    busy: false,
    zapModeBoostId: '',
    status: '',
    statusTone: 'neutral',
    context: null,
    selection: {
      selectedBoostId: '',
      activeEditorTab: 'css',
    },
  };
}

export function cloneBoostEntry(boost: BoostEntry): BoostEntry {
  return {
    ...boost,
    zapSelectors: [...(boost.zapSelectors ?? [])],
  };
}

export function cloneState(state: State): State {
  return {
    boosts: state.boosts.map(cloneBoostEntry),
    templates: state.templates.map(template => ({...template})),
    createPattern: state.createPattern,
    templatePattern: state.templatePattern,
    selectedTemplateId: state.selectedTemplateId,
    loading: state.loading,
    busy: state.busy,
    zapModeBoostId: state.zapModeBoostId,
    status: state.status,
    statusTone: state.statusTone,
    context: state.context ? {...state.context} : null,
    selection: {...state.selection},
  };
}
