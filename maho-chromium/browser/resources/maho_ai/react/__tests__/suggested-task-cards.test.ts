import {describe, expect, it} from 'vitest';

import {
  SUGGESTED_TASKS,
  SUGGESTED_TASK_RUNTIME_CONFIG,
  getSuggestedTaskCards,
} from '../../views/suggestions.js';

describe('suggested task presentation helpers', () => {
  it('mirrors the backend suggestion catalog (title + prompt pairs)', () => {
    expect(SUGGESTED_TASKS.length).toBeGreaterThan(0);
    for (const task of SUGGESTED_TASKS) {
      expect(task.title.trim().length).toBeGreaterThan(0);
      expect(task.prompt.trim().length).toBeGreaterThan(0);
    }
  });

  it('returns the full catalog when suggestion settings are enabled', () => {
    expect(getSuggestedTaskCards({enabled: true})).toEqual(SUGGESTED_TASKS);
  });

  it('returns zero suggestions when suggestions are disabled', () => {
    // SuggestionSettings::disabled parity — nothing may surface.
    expect(getSuggestedTaskCards({enabled: false})).toEqual([]);
  });

  it('carries the guard/final-confirm/proactive flag set for new sessions', () => {
    expect(SUGGESTED_TASK_RUNTIME_CONFIG).toEqual({
      permissionTier: 'guard',
      mailReadAllowed: false,
      finalConfirm: true,
      proactiveMode: true,
    });
  });
});
