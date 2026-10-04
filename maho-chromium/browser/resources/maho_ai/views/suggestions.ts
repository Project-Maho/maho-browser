// Copyright 2026 Maho Browser. All rights reserved.

// Presentation descriptors for the panel idle suggested-task cards (plan
// row 12). Pure data — no DOM, no store imports — mirroring the backend
// suggestion contract in maho/crates/maho-agent/src/suggestions.rs
// (Suggestion{title, prompt}) and its SuggestionSettings gate: when
// suggestions are disabled the catalog is empty and no cards render.
// Enforcement of the runtime flags lives exclusively in the CapabilityBroker;
// this module only names what the UI shows and dispatches.
import type {RuntimeConfigInfo} from '../types.js';

export interface SuggestedTask {
  readonly title: string;
  readonly prompt: string;
}

export interface SuggestionSettingsState {
  readonly enabled: boolean;
}

// Idle catalog: mirrors the backend engine's deterministic generic-fallback
// templates, which apply when no run journal triggers match (idle panel).
// English-only strings, single-sourced so the cards and their vitest
// boundary cannot drift.
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
// proactive_mode=true, final_confirm=true, guard permission tier.
// Single-sourced so the card click and its vitest boundary cannot drift.
export const SUGGESTED_TASK_RUNTIME_CONFIG: RuntimeConfigInfo = {
  permissionTier: 'guard',
  finalConfirm: true,
  proactiveMode: true,
  // Suggested tasks never widen Mail access; consent stays with the SSOT pref.
  mailReadAllowed: false,
};

// SuggestionSettings::disabled parity — a disabled backend setting yields
// zero cards.
export function getSuggestedTaskCards(settings: SuggestionSettingsState):
    readonly SuggestedTask[] {
  return settings.enabled ? SUGGESTED_TASKS : [];
}
