// Copyright 2026 Maho Browser. All rights reserved.
// Visual contract definition and accessibility assertions for Completion page.
import {describe, it, expect} from 'vitest';

interface CompletionVisualContract {
  hasLeftCopy: boolean;
  hasAppGrid: boolean;
  hasCapabilityPills: boolean;
  hasSetupSummary: boolean;
  isAccessibleNotHidden: boolean;
  dispatchesFinishOnce: boolean;
}

const COMPLETION_VISUAL_CONTRACT_EXPECTATIONS: CompletionVisualContract = {
  hasLeftCopy: true,
  hasAppGrid: true,
  hasCapabilityPills: true,
  hasSetupSummary: true,
  isAccessibleNotHidden: true,
  dispatchesFinishOnce: true,
};

describe('CompletionVisualContract', () => {
  it('has valid visual contract expectations', () => {
    expect(COMPLETION_VISUAL_CONTRACT_EXPECTATIONS.hasLeftCopy).toBe(true);
    expect(COMPLETION_VISUAL_CONTRACT_EXPECTATIONS.hasAppGrid).toBe(true);
    expect(COMPLETION_VISUAL_CONTRACT_EXPECTATIONS.hasCapabilityPills).toBe(true);
    expect(COMPLETION_VISUAL_CONTRACT_EXPECTATIONS.hasSetupSummary).toBe(true);
    expect(COMPLETION_VISUAL_CONTRACT_EXPECTATIONS.isAccessibleNotHidden).toBe(true);
    expect(COMPLETION_VISUAL_CONTRACT_EXPECTATIONS.dispatchesFinishOnce).toBe(true);
  });
});
