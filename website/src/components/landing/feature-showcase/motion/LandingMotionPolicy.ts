export type MotionPolicy = { readonly reducedMotion: boolean; readonly saveData: boolean };

export function isMotionSuppressed(frozen: boolean, policy: MotionPolicy): boolean {
  return frozen || policy.reducedMotion || policy.saveData;
}

export function readSaveData(): boolean {
  if (typeof navigator === "undefined" || !("connection" in navigator)) return false;
  const connection = navigator.connection;
  return typeof connection === "object" && connection !== null && "saveData" in connection && connection.saveData === true;
}

export function readConnectionTarget(): EventTarget | null {
  if (typeof navigator === "undefined" || !("connection" in navigator)) return null;
  return navigator.connection instanceof EventTarget ? navigator.connection : null;
}
