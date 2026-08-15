import {describe, expect, it} from 'vitest';

import {
  classifyMailSettingsFailure,
  createMailSettingsContentState,
} from './mail_settings_state.js';

describe('Mail settings state contract', () => {
  it('distinguishes ready content from an authoritative empty result', () => {
    expect(createMailSettingsContentState(['one'])).toEqual({
      status: 'ready',
      data: ['one'],
    });
    expect(createMailSettingsContentState([])).toEqual({
      status: 'empty',
      data: [],
    });
  });

  it('classifies service availability separately from bounded request errors', () => {
    expect(classifyMailSettingsFailure(
        'Mail service unavailable', 'Could not load mail settings')).toEqual({
      status: 'unavailable',
      message: 'Mail service unavailable',
    });
    expect(classifyMailSettingsFailure('', 'Could not load mail settings')).toEqual({
      status: 'error',
      message: 'Could not load mail settings',
    });
  });
});
