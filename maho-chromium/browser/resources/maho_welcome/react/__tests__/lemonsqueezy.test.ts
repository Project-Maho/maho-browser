// Copyright 2026 Maho Browser. All rights reserved.

import {afterEach, describe, expect, it, vi} from 'vitest';

import {openCheckoutOverlay} from '../../../maho_common/react/lemonsqueezy.js';

afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

describe('openCheckoutOverlay', () => {
  it('opens a popup window when LemonSqueezy cannot load', async () => {
    const open = vi.fn();
    vi.stubGlobal('window', {open});
    vi.stubGlobal('document', {
      createElement: () => ({}),
      head: {
        appendChild: (script: {onerror?: (event: Event) => void}) => {
          script.onerror?.(new Event('error'));
        },
      },
    });

    await openCheckoutOverlay({
      checkoutUrl: 'https://maho.lemonsqueezy.com/buy/example',
    });

    expect(open).toHaveBeenCalledWith(
        'https://maho.lemonsqueezy.com/buy/example',
        'maho-checkout',
        'popup,width=960,height=760,noopener,noreferrer');
  });
});
