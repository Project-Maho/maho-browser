export type Platform = 'ios' | 'android' | 'desktop' | 'unknown';

/**
 * Detect the hosting platform at runtime.
 * Called once on startup; result is stable for the lifetime of the page.
 */
export function detectPlatform(): Platform {
  // iOS: WKWebView injects window.webkit.messageHandlers
  if (
    typeof window !== 'undefined' &&
    (window as unknown as { webkit?: { messageHandlers?: { mahoBridge?: unknown } } }).webkit
      ?.messageHandlers?.mahoBridge
  ) {
    return 'ios';
  }

  // Android: @JavascriptInterface is attached as window.MahoBridgeAndroid
  if (
    typeof window !== 'undefined' &&
    typeof (window as unknown as { MahoBridgeAndroid?: unknown }).MahoBridgeAndroid !== 'undefined'
  ) {
    return 'android';
  }

  // Desktop: Mojo page handler registered under __mahoChromePageHandler
  if (
    typeof window !== 'undefined' &&
    typeof (window as unknown as { __mahoChromePageHandler?: unknown })
      .__mahoChromePageHandler !== 'undefined'
  ) {
    return 'desktop';
  }

  return 'unknown';
}

export const platform: Platform = detectPlatform();
