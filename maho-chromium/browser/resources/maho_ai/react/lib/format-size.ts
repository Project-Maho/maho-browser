const UNITS = ['B', 'KB', 'MB', 'GB', 'TB'];

export function formatByteSize(bytes: bigint | number): string {
  let size = typeof bytes === 'bigint' ? Number(bytes) : bytes;
  if (!Number.isFinite(size) || size < 0) {
    return '0 B';
  }
  let unitIndex = 0;
  while (size >= 1024 && unitIndex < UNITS.length - 1) {
    size /= 1024;
    unitIndex += 1;
  }
  const rounded = unitIndex === 0 ? Math.round(size) : Math.round(size * 10) / 10;
  return `${rounded} ${UNITS[unitIndex]}`;
}
