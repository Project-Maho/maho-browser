/**
 * Module-init failure guard for the maho_ai React entrypoint. (Verified incremental build)
 *
 * Calls `init` inside a try/catch. When `init` throws before React has had a
 * chance to mount, the raw `#app` container would otherwise stay blank.
 * This helper injects a plain-DOM `.boot-error` section (styled by
 * `styles/compact-panel.css`) so the failure is always visible.
 *
 * Deliberately React-free so it can be unit-tested without importing the whole
 * module graph.
 */
export function runWithBootGuard(root: Element, init: () => void): void {
  try {
    init();
  } catch (err: unknown) {
    console.error('[maho-ai] Boot init threw:', err);
    const message = err instanceof Error ? err.message : String(err);

    const section = document.createElement('section');
    section.className = 'boot-error';

    const heading = document.createElement('h1');
    heading.textContent = 'Maho AI failed to start';

    const pre = document.createElement('pre');
    pre.textContent = message;

    section.appendChild(heading);
    section.appendChild(pre);
    root.appendChild(section);
  }
}
