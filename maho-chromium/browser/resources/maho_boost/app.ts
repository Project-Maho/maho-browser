// Copyright 2026 Maho Browser. All rights reserved.

const trustedTypes = window.trustedTypes;
if (!trustedTypes || typeof trustedTypes.createPolicy !== 'function') {
  throw new Error('Trusted Types API is unavailable for maho-boost loader.');
}

trustedTypes.createPolicy(
    'default',
    {
      createHTML: (input: string): string => {
        if (input.includes('<')) {
          throw new Error('Unexpected maho-boost HTML assignment.');
        }
        return input;
      },
    });

const policy = trustedTypes.createPolicy(
    'static-types',
    {
      createScriptURL: (input: string): string => {
        if (input !== 'app_bundle.js') {
          throw new Error('Unexpected maho-boost script URL.');
        }
        return input;
      },
    });

const platform = navigator.platform.toLowerCase();
document.documentElement.dataset['platform'] = platform.includes('mac') ?
  'macos' : platform.includes('win') ? 'windows' : 'linux';
if (!document.documentElement.hasAttribute('editor')) {
  document.documentElement.setAttribute('editor', 'boost');
}

const bundleScript = document.createElement('script');
bundleScript.type = 'module';
bundleScript.src = policy.createScriptURL('app_bundle.js');
document.head.appendChild(bundleScript);

export {};
