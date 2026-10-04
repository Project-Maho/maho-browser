// Copyright 2026 Maho Browser. All rights reserved.

import {describe, expect, it, vi} from 'vitest';

import {PLANS, PlanPicker} from '../../../maho_common/react/ui/plan-picker.js';

interface ElementNode {
  props?: {
    children?: unknown;
    disabled?: boolean;
    onClick?: () => void;
  };
}

function childNodes(node: unknown): ElementNode[] {
  if (Array.isArray(node)) {
    return node.flatMap(childNodes);
  }
  if (node && typeof node === 'object' && 'props' in node) {
    return [node as ElementNode];
  }
  return [];
}

function findButton(node: ElementNode): ElementNode {
  const children = childNodes(node.props?.children);
  const button = children.find(child => typeof child.props?.onClick === 'function');
  if (!button) {
    throw new Error('Plan card does not contain a button');
  }
  return button;
}

describe('PlanPicker', () => {
  it('renders all tiers, marks the current tier, and emits selection', () => {
    const onSelect = vi.fn();
    const picker = PlanPicker({currentTier: 'pro', onSelect}) as ElementNode;
    const cards = childNodes(picker.props?.children);

    expect(PLANS.map(plan => [plan.tier, plan.priceUsd])).toEqual([
      ['free', 0],
      ['pro', 5],
      ['max', 20],
    ]);
    expect(cards).toHaveLength(3);

    const [freeButton, proButton, maxButton] = cards.map(findButton);
    expect(freeButton.props?.disabled).toBe(false);
    expect(proButton.props?.disabled).toBe(true);
    expect(maxButton.props?.disabled).toBe(false);

    freeButton.props?.onClick?.();
    maxButton.props?.onClick?.();
    expect(onSelect).toHaveBeenNthCalledWith(1, 'free');
    expect(onSelect).toHaveBeenNthCalledWith(2, 'max');
  });

  it('allows the current Free tier to continue during onboarding', () => {
    const onSelect = vi.fn();
    const picker = PlanPicker({
      allowCurrentSelection: true,
      freeLabel: 'Continue with Free',
      onSelect,
    }) as ElementNode;
    const [freeCard] = childNodes(picker.props?.children);
    const freeButton = findButton(freeCard);

    expect(freeButton.props?.disabled).toBe(false);
    expect(freeButton.props?.children).toBe('Continue with Free');

    freeButton.props?.onClick?.();
    expect(onSelect).toHaveBeenCalledWith('free');
  });
});
