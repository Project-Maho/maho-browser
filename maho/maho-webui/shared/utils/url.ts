export function extractDomain(url: string): string {
  try {
    return new URL(url).hostname;
  } catch {
    return url;
  }
}

export function isInternalUrl(url: string): boolean {
  return /^(chrome|maho|about):/.test(url);
}

export function prettifyUrl(url: string): string {
  return url.replace(/^https?:\/\//, '').replace(/\/$/, '');
}
