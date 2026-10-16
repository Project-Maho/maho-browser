import React, {useCallback, useEffect, useState} from 'react';

import type {PageHandlerRemote} from '../maho_settings.mojom-webui.js';

function ActionButton({label, onClick, ariaLabel}: {
  label: string;
  onClick: () => void;
  ariaLabel?: string;
}) {
  return <button type="button" aria-label={ariaLabel} onClick={onClick}>{label}</button>;
}

function SectionCard({title, description, children}: React.PropsWithChildren<{
  title: string;
  description: string;
}>) {
  return <section className="mt-6"><h3>{title}</h3><p>{description}</p>{children}</section>;
}

function EmptyDescription({message}: {message: string}) {
  return <p>{message}</p>;
}

function ManagedSettingRow({title, description, children}: React.PropsWithChildren<{
  title: string;
  description: string;
}>) {
  return <div className="flex items-center justify-between px-4 py-3">
    <div><h4>{title}</h4><p>{description}</p></div><div>{children}</div>
  </div>;
}

function TextFieldShell({ariaLabel, placeholder, value, onChange}: {
  ariaLabel: string;
  placeholder: string;
  value: string;
  onChange: (value: string) => void;
}) {
  return <input aria-label={ariaLabel} placeholder={placeholder} value={value}
    onChange={event => onChange(event.currentTarget.value)} />;
}

export interface SiteSessionSettingsHandler {
  getSiteSessionOptedOrigins(): Promise<{origins: string[]}>;
  setSiteSessionOptedOrigins(origins: string[]): Promise<{success: boolean; errorMessage: string}>;
}

export function normalizeSiteSessionOrigin(input: string): string | null {
  let url: URL;
  try {
    url = new URL(input.trim());
  } catch {
    return null;
  }
  if (url.protocol !== 'https:' || url.username || url.password ||
      url.pathname !== '/' || url.search || url.hash ||
      /^\d+(?:\.\d+){3}$/.test(url.hostname) || url.hostname.includes(':'))
    return null;
  return url.origin;
}

export function SiteSessionsSection({handler}: {handler: PageHandlerRemote}) {
  const [origins, setOrigins] = useState<string[]>([]);
  const [draft, setDraft] = useState('');
  const [error, setError] = useState('');

  const load = useCallback(async () => {
    try {
      const result = await handler.getSiteSessionOptedOrigins();
      setOrigins(result.origins);
      setError('');
    } catch {
      setError('Could not load opted-in sites.');
    }
  }, [handler]);

  useEffect(() => { void load(); }, [load]);

  const save = async (next: string[]) => {
    const result = await handler.setSiteSessionOptedOrigins(next);
    if (!result.success) {
      setError(result.errorMessage || 'Could not update opted-in sites.');
      return;
    }
    setOrigins(next);
    setError('');
  };

  const add = async () => {
    const normalized = normalizeSiteSessionOrigin(draft);
    if (!normalized) {
      setError('Enter a valid HTTPS origin using a DNS hostname.');
      return;
    }
    if (origins.includes(normalized)) {
      setError('This origin is already enabled.');
      return;
    }
    await save([...origins, normalized].sort());
    setDraft('');
  };

  return (
    <SectionCard title="Keep me signed in on my devices"
      description="Selected sites sync cookies and local storage using end-to-end encryption.">
      {origins.length === 0 ? <EmptyDescription message="No sites are opted in." /> : null}
      {origins.map(origin => (
        <ManagedSettingRow key={origin} title={origin} description="Session data is end-to-end encrypted.">
          <ActionButton ariaLabel={`Remove ${origin}`} label="Remove"
            onClick={() => void save(origins.filter(item => item !== origin))} />
        </ManagedSettingRow>
      ))}
      <ManagedSettingRow title="Add site"
        description="Enter an HTTPS origin. Paths and IP addresses are not accepted.">
        <TextFieldShell ariaLabel="HTTPS origin" placeholder="https://example.com"
          value={draft} onChange={setDraft} />
        <ActionButton label="Add site" onClick={() => void add()} />
      </ManagedSettingRow>
      {error ? <p role="alert">{error}</p> : null}
    </SectionCard>
  );
}
