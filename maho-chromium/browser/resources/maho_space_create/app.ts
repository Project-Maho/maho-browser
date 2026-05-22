const policy = window.trustedTypes!.createPolicy(
    'maho-space-create-app-bundle',
    {createScriptURL: (input: string): string => input});

const bundleScript = document.createElement('script');
bundleScript.type = 'module';
bundleScript.src =
    policy.createScriptURL('app_bundle.js') as unknown as string;
document.head.appendChild(bundleScript);
