/**
 * Type declarations for chrome://resources/js/ modules.
 *
 * These modules are provided by Chromium at runtime and must be treated as
 * external imports by the bundler. This file provides TypeScript type
 * information so that React WebUI code can import them with full type safety.
 */

// Chromium enforces Trusted Types at runtime: the DOM accepts TrustedHTML
// for innerHTML even though lib.dom.d.ts types it as string. Augment the
// interface here so WebUI code can assign TrustedHTML without type casts.
interface InnerHTML {
  innerHTML: string | TrustedHTML;
}

declare module 'chrome://resources/js/load_time_data.js' {
  interface LoadTimeData {
    getBoolean(id: string): boolean;
    getInteger(id: string): number;
    getString(id: string): string;
    getStringF(id: string, ...args: Array<string | number>): string;
    getValue(id: string): unknown;
    valueExists(id: string): boolean;
    data: Record<string, unknown>;
  }

  export const loadTimeData: LoadTimeData;
}

declare module 'chrome://resources/js/parse_html_subset.js' {
  interface SanitizeInnerHtmlOptions {
    attrs?: string[];
    tags?: string[];
  }

  export function sanitizeInnerHtml(
      rawHtml: string,
      options?: SanitizeInnerHtmlOptions): TrustedHTML;

  export function parseHtmlSubset(
      html: string,
      allowedTags?: string[],
      allowedAttrs?: string[]): DocumentFragment;
}
