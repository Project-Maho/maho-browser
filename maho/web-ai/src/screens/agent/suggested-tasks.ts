// Idle suggested-task cards for the mobile agent SPA (plan row 14; parity of
// the desktop panel row 12). Pure data — no DOM — mirroring the backend
// suggestion contract in maho/crates/maho-agent/src/suggestions.rs
// (Suggestion{title, prompt}, MAX_PASSIVE_SUGGESTIONS = 2) and its
// SuggestionSettings gate: when suggestions are disabled the catalog is empty
// and no cards render. The strings mirror the desktop
// maho_ai/views/suggestions.ts catalog one-for-one so the surfaces cannot
// drift. Enforcement of the runtime flags lives exclusively in the
// CapabilityBroker; this module only names what the UI shows and dispatches.
import type { AgentRuntimeConfig } from '../../bridge/types';

export interface SuggestedTask {
  readonly title: string;
  readonly prompt: string;
}

export interface SuggestionSettingsState {
  readonly enabled: boolean;
}

// Idle catalog: mirrors the backend engine's deterministic generic-fallback
// templates. English-only strings, single-sourced so the cards and their
// vitest boundary cannot drift.
export const SUGGESTED_TASKS: readonly SuggestedTask[] = [
  {
    title: 'Follow up on this topic',
    prompt:
      'What are the recommended next steps or related actions based on this outcome?',
  },
  {
    title: 'Turn into routine',
    prompt: 'Convert this workflow into a reusable routine with regular scheduling.',
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
