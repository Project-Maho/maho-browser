// Idle suggested-task cards for the mobile agent SPA (plan row 14; parity of
// the desktop panel row 12). Pure data — no DOM — mirroring the backend
// suggestion contract in maho/crates/maho-agent/src/suggestions.rs
// (Suggestion{title, prompt}, MAX_PASSIVE_SUGGESTIONS = 2) and its
// SuggestionSettings gate: when suggestions are disabled the catalog is empty
// and no cards render. Enforcement of the runtime flags lives exclusively in the
// CapabilityBroker; this module only names what the UI shows and dispatches.
import type { AgentRuntimeConfig } from '../../bridge/types';

export interface SuggestedTask {
  readonly title: string;
  readonly prompt: string;
}

export interface SuggestionSettingsState {
  readonly enabled: boolean;
}

// Idle catalog: shown on an empty session, so the prompts must stand on their
// own — they never refer to a prior outcome or workflow that does not exist.
// English-only strings, single-sourced so the cards and their vitest boundary
// cannot drift.
export const SUGGESTED_TASKS: readonly SuggestedTask[] = [
  {
    title: 'Research a topic',
    prompt:
      'Help me research a topic: ask what I want to learn, then gather the key facts and summarize them with sources.',
  },
  {
    title: 'Set up a routine',
    prompt: 'Help me set up a recurring routine: ask what I want done and how often, then propose a schedule.',
  },
] as const;

// Flag set carried by every session created from a suggested-task card click:
// proactive_mode=true, final_confirm=true, guard permission tier. Mirrors the
// desktop SUGGESTED_TASK_RUNTIME_CONFIG and the FFI setter contract.
export const SUGGESTED_TASK_RUNTIME_CONFIG: AgentRuntimeConfig = {
  permissionTier: 'guard',
  finalConfirm: true,
  proactiveMode: true,
};

// SuggestionSettings::disabled parity — a disabled backend setting yields
// zero cards.
export function getSuggestedTaskCards(settings: SuggestionSettingsState): readonly SuggestedTask[] {
  return settings.enabled ? SUGGESTED_TASKS : [];
}
