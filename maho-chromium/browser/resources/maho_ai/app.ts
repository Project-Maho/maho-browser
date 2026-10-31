const MOUNT_FLAG = '__mahoAiMounted';
const MOUNT_TIMEOUT_MS = 6000;

function showBootError(title: string, detail: string | null): void {
  if ((window as unknown as Record<string, unknown>)[MOUNT_FLAG]) {
    return;
  }
  const app = document.getElementById('app');
  if (!app || app.childElementCount > 0) {
    return;
  }

  const section = document.createElement('section');
  section.className = 'boot-error';

  const h1 = document.createElement('h1');
  h1.textContent = title;
  section.appendChild(h1);

  if (detail !== null) {
    const pre = document.createElement('pre');
    pre.textContent = detail;
    section.appendChild(pre);
  }

  app.appendChild(section);
}

window.addEventListener('error', (event: ErrorEvent) => {
  const detail = (event.error instanceof Error && event.error.stack)
    ? event.error.stack
    : (event.message || 'Unknown error');
  showBootError('Maho AI failed to start', detail);
});

window.addEventListener('unhandledrejection', (event: PromiseRejectionEvent) => {
  const reason: unknown = event.reason;
  const detail = (reason instanceof Error && reason.stack)
    ? reason.stack
    : (reason !== null && reason !== undefined ? String(reason) : 'Unhandled promise rejection');
  showBootError('Maho AI failed to start', detail);
});

const ttPolicy = window.trustedTypes?.createPolicy(
  'static-types',
  {createScriptURL: (input: string): string => input}
);

if (!ttPolicy) {
  throw new Error('Trusted Types API is unavailable for maho-ai loader.');
}

const bundleScript = document.createElement('script');
bundleScript.type = 'module';
bundleScript.src = ttPolicy.createScriptURL('app_bundle.js') as unknown as string;
bundleScript.addEventListener('error', () => {
  showBootError(
    'Maho AI failed to start',
    'app_bundle.js failed to load. Check DevTools console for details.'
  );
});
document.body.appendChild(bundleScript);

setTimeout(() => {
  if (!(window as unknown as Record<string, unknown>)[MOUNT_FLAG]) {
    showBootError(
      'Maho AI failed to start',
      'Module graph loaded but React did not mount within ' +
        String(MOUNT_TIMEOUT_MS / 1000) + 's. ' +
        'Check DevTools console for details.'
    );
  }
}, MOUNT_TIMEOUT_MS);

export {};
