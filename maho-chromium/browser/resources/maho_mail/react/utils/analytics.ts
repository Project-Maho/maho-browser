// Analytics is disabled in the Maho browser WebUI. The Tauri desktop app used
// Plausible telemetry gated on Vite build-time env vars (`import.meta.env.*`),
// which do not exist in the Chromium WebUI/esbuild bundle and threw at module
// load, blanking the app. It is also inappropriate for the closed-source
// browser to emit external telemetry. These are intentional no-ops that
// preserve the call sites (SettingsPanel, OAuthStep, CredentialsStep).

let enabled = false;

export function initAnalytics(_consent: boolean): void {
  enabled = false;
}

export function trackEvent(
  _name: string,
  _props?: Record<string, string | number | boolean>,
): void {
  // no-op: browser WebUI does not emit analytics.
}

export function isAnalyticsEnabled(): boolean {
  return enabled;
}
