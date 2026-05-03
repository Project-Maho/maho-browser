export function escapeHtml(text: string): string {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

export function truncateUrl(url: string, maxLength: number = 50): string {
  if (url.length <= maxLength) return url;
  try {
    const parsed = new URL(url);
    const base = parsed.hostname + parsed.pathname;
    return base.length > maxLength ? base.substring(0, maxLength - 1) + '…' :
                                     base;
  } catch {
    return url.substring(0, maxLength - 1) + '…';
  }
}

export function formatTimestamp(timestamp: number): string {
  const date = new Date(timestamp);
  const now = new Date();
  const diff = now.getTime() - date.getTime();
  if (diff < 60000) return 'just now';
  if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`;
  if (diff < 86400000) return `${Math.floor(diff / 3600000)}h ago`;
  return date.toLocaleDateString();
}

export function debounce<T extends (...args: unknown[]) => void>(
    fn: T, delay: number): (...args: Parameters<T>) => void {
  let timer: ReturnType<typeof setTimeout>;
  return (...args: Parameters<T>) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), delay);
  };
}
