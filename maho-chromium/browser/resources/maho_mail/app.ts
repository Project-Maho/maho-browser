const policy = window.trustedTypes!.createPolicy(
    'static-types',
    {createScriptURL: (input: string): string => input});

const bundleScript = document.createElement('script');
bundleScript.type = 'module';
bundleScript.src =
    policy.createScriptURL('app_bundle.js') as unknown as string;
document.head.appendChild(bundleScript);
