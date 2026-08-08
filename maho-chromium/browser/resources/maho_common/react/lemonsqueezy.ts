/**
 * LemonSqueezy overlay checkout utility.
 *
 * Loads `lemon.js` from the LemonSqueezy CDN on demand and opens a checkout
 * overlay for a given URL. Falls back to a separate checkout window when the
 * CDN script cannot be loaded (offline, CSP mismatch, or LS outage).
 *
 * Docs: https://docs.lemonsqueezy.com/help/lemonjs
 */

declare global {
  interface Window {
    LemonSqueezy?: {
      Setup: (config: { eventHandler: (event: LemonSqueezyEvent) => void }) => void;
      Url: { Open: (url: string) => void; Close: () => void };
    };
    createLemonSqueezy?: () => void;
  }
}

export type LemonSqueezyEvent =
  | {
      event: 'Checkout.Success';
      data: {
        type: 'orders';
        id: string;
        attributes: Record<string, unknown>;
      };
    }
  | { event: 'Checkout.Close' }
  | { event: string; data?: unknown };

const LEMON_JS_URL = 'https://app.lemonsqueezy.com/js/lemon.js';

let loadPromise: Promise<void> | null = null;

export function loadLemonJS(): Promise<void> {
  if (window.LemonSqueezy) {
    return Promise.resolve();
  }
  if (loadPromise) {
    return loadPromise;
  }

  loadPromise = new Promise<void>((resolve, reject) => {
    const script = document.createElement('script');
    script.src = LEMON_JS_URL;
    script.defer = true;
    script.onload = () => {
      window.createLemonSqueezy?.();
      resolve();
    };
    script.onerror = () => {
      loadPromise = null;
      reject(new Error('Failed to load lemon.js from LemonSqueezy CDN'));
    };
    document.head.appendChild(script);
  });

  return loadPromise;
}

export interface OverlayOpenOptions {
  checkoutUrl: string;
  onSuccess?: (orderId: string) => void;
  onClose?: () => void;
  onError?: (error: Error) => void;
}

function openCheckoutWindow(checkoutUrl: string): void {
  window.open(
      checkoutUrl,
      'maho-checkout',
      'popup,width=960,height=760,noopener,noreferrer');
}

/**
 * Open a checkout URL as a LemonSqueezy overlay iframe.
 *
 * On any failure (CDN blocked, LemonSqueezy not initialised, load timeout),
 * falls back to a separate checkout window. This keeps the purchase flow
 * robust when the WebUI is offline from `app.lemonsqueezy.com`.
 */
export async function openCheckoutOverlay(opts: OverlayOpenOptions): Promise<void> {
  const overlayUrl = opts.checkoutUrl.includes('?')
    ? `${opts.checkoutUrl}&embed=1`
    : `${opts.checkoutUrl}?embed=1`;

  try {
    await loadLemonJS();
  } catch (err) {
    opts.onError?.(err instanceof Error ? err : new Error(String(err)));
    openCheckoutWindow(opts.checkoutUrl);
    return;
  }

  if (!window.LemonSqueezy) {
    opts.onError?.(new Error('LemonSqueezy global not available after script load'));
    openCheckoutWindow(opts.checkoutUrl);
    return;
  }

  window.LemonSqueezy.Setup({
    eventHandler: (event) => {
      if (event.event === 'Checkout.Success') {
        const successEvent = event as Extract<LemonSqueezyEvent, {event: 'Checkout.Success'}>;
        opts.onSuccess?.(successEvent.data.id);
        window.LemonSqueezy?.Url.Close();
      } else if (event.event === 'Checkout.Close') {
        opts.onClose?.();
      }
    },
  });

  window.LemonSqueezy.Url.Open(overlayUrl);
}
