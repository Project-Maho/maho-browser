import {describe, it, expect, beforeEach, afterEach, vi} from 'vitest';

function setupAppRoot(): HTMLElement {
  document.body.innerHTML = '';
  const app = document.createElement('div');
  app.id = 'app';
  document.body.appendChild(app);
  return app;
}

function installTrustedTypesStub(): void {
  (window as unknown as Record<string, unknown>)['trustedTypes'] = {
    createPolicy: (_name: string, rules: {createScriptURL: (s: string) => string}) => ({
      createScriptURL: rules.createScriptURL,
    }),
  };
}

async function loadAppModule(): Promise<void> {
  await import('../../app.js');
}

function cleanUp(): void {
  vi.useRealTimers();
  vi.resetModules();
  document.body.innerHTML = '';
  (window as unknown as Record<string, unknown>)['__mahoAiMounted'] = undefined;
  (window as unknown as Record<string, unknown>)['trustedTypes'] = undefined;
}

describe('app.ts — script injection', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.resetModules();
    installTrustedTypesStub();
    setupAppRoot();
  });

  afterEach(cleanUp);

  it('appends a module script for app_bundle.js to document.body', async () => {
    await loadAppModule();

    const scripts = Array.from(document.body.querySelectorAll('script'));
    const bundleScript = scripts.find(
      (s) => s.getAttribute('src') === 'app_bundle.js'
    );
    expect(bundleScript).not.toBeUndefined();
    expect(bundleScript!.getAttribute('type')).toBe('module');
  });

  it('uses a Trusted Types policy to assign the script URL', async () => {
    const createPolicySpy = vi.fn(
      (_name: string, rules: {createScriptURL: (s: string) => string}) => ({
        createScriptURL: createScriptURLSpy.mockImplementation(rules.createScriptURL),
      })
    );
    const createScriptURLSpy = vi.fn((s: string) => s);
    (window as unknown as Record<string, unknown>)['trustedTypes'] = {
      createPolicy: createPolicySpy,
    };

    await loadAppModule();

    expect(createPolicySpy).toHaveBeenCalledWith(
      'static-types',
      expect.objectContaining({createScriptURL: expect.any(Function)})
    );
    expect(createScriptURLSpy).toHaveBeenCalledWith('app_bundle.js');
  });
});

describe('app.ts — script load failure shows .boot-error', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.resetModules();
    installTrustedTypesStub();
    setupAppRoot();
  });

  afterEach(cleanUp);

  it('shows .boot-error when the bundle script fires an error event', async () => {
    await loadAppModule();

    const app = document.getElementById('app')!;
    const bundleScript = Array.from(document.body.querySelectorAll('script')).find(
      (s) => s.getAttribute('src') === 'app_bundle.js'
    )!;
    bundleScript.dispatchEvent(new Event('error'));

    const bootError = app.querySelector('.boot-error');
    expect(bootError).not.toBeNull();
    expect(bootError!.querySelector('h1')!.textContent).toBe('Maho AI failed to start');
    expect(bootError!.querySelector('pre')!.textContent).toContain('app_bundle.js failed to load');
  });
});

describe('app.ts — mount timeout shows .boot-error when not mounted', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.resetModules();
    installTrustedTypesStub();
    setupAppRoot();
  });

  afterEach(cleanUp);

  it('shows .boot-error after 6s when window.__mahoAiMounted is not set', async () => {
    await loadAppModule();

    const app = document.getElementById('app')!;
    expect(app.querySelector('.boot-error')).toBeNull();

    vi.advanceTimersByTime(6001);

    const bootError = app.querySelector('.boot-error');
    expect(bootError).not.toBeNull();
    expect(bootError!.querySelector('h1')!.textContent).toBe('Maho AI failed to start');
    expect(bootError!.querySelector('pre')!.textContent).toContain('React did not mount');
  });
});

describe('app.ts — mount timeout does NOT show .boot-error once mounted', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.resetModules();
    installTrustedTypesStub();
    setupAppRoot();
  });

  afterEach(cleanUp);

  it('does not show .boot-error when window.__mahoAiMounted is truthy before timeout fires', async () => {
    await loadAppModule();

    (window as unknown as Record<string, unknown>)['__mahoAiMounted'] = true;

    vi.advanceTimersByTime(6001);

    const app = document.getElementById('app')!;
    expect(app.querySelector('.boot-error')).toBeNull();
  });
});
