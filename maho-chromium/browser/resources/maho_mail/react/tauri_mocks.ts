// Copyright 2026 Maho Browser. All rights reserved.

/**
 * Tauri Mocks for Maho WebUI Browser Environment.
 * Allows components and hooks to compile and run without Tauri.
 */

import { listen } from './events.js';
import { handler } from './mojo_client.js';

const isTest = typeof globalThis !== 'undefined' && 'vi' in globalThis;

// @tauri-apps/api/event
export { listen };
export const emit = isTest
  ? (globalThis as any).vi.fn().mockResolvedValue(undefined)
  : async function emit(eventName: string, payload?: any): Promise<void> {};

export const emitTo = isTest
  ? (globalThis as any).vi.fn().mockResolvedValue(undefined)
  : async function emitTo(target: string | { kind: string }, eventName: string, payload?: any): Promise<void> {};

// @tauri-apps/api/core (invoke)
export const invoke = isTest
  ? (globalThis as any).vi.fn().mockResolvedValue(null)
  : async function invoke<T>(cmd: string, args?: any): Promise<T> {
      return Promise.resolve(null as unknown as T);
    };

// @tauri-apps/api/webviewWindow
export class WebviewWindow {
  label: string;
  constructor(label: string, options?: any) {
    this.label = label;
  }
  static getByLabel = isTest
    ? (globalThis as any).vi.fn().mockResolvedValue(null)
    : (label: string) => new WebviewWindow(label);

  once = isTest
    ? (globalThis as any).vi.fn((event: string, callback: () => void) => {
        if (event === "tauri://created") {
          callback();
        }
        return Promise.resolve(() => {});
      })
    : (event: string, handler: (e: any) => void) => {
        if (event === 'tauri://created') {
          setTimeout(handler, 10);
        }
      };

  emit = isTest ? (globalThis as any).vi.fn() : (event: string, payload?: any) => {};
  // Resolves to an unlisten function in both branches: callers hand the result
  // straight to a useEffect cleanup, and a bare vi.fn() would resolve undefined
  // and make React throw on unmount.
  listen = isTest
    ? (globalThis as any).vi.fn().mockResolvedValue(() => {})
    : (event: string, handler: (e: any) => void) => {
        return Promise.resolve(() => {});
      };
  close = isTest ? (globalThis as any).vi.fn() : () => {};
  setFocus = isTest ? (globalThis as any).vi.fn() : (): Promise<void> => {
    return Promise.resolve();
  };
}

export const getCurrentWebviewWindow = isTest
  ? (globalThis as any).vi.fn(() => ({
      label: "main",
      setFocus: (globalThis as any).vi.fn().mockResolvedValue(undefined),
      close: (globalThis as any).vi.fn().mockResolvedValue(undefined),
      emit: (globalThis as any).vi.fn().mockResolvedValue(undefined),
      listen: (globalThis as any).vi.fn().mockResolvedValue(() => {}),
    }))
  : () => ({
      label: 'main',
      emit: (event: string, payload?: any) => {},
      listen: (event: string, handler: (e: any) => void) => Promise.resolve(() => {}),
      close: () => {},
    });

// @tauri-apps/api/path
export async function downloadDir(): Promise<string> {
  try {
    const result = await handler.getDownloadDir();
    return result.path;
  } catch (e) {
    return '/Downloads';
  }
}

// @tauri-apps/api/core
export function convertFileSrc(filePath: string): string {
  if (!filePath) return '';
  if (filePath.startsWith('file://')) {
    return filePath;
  }
  return 'file://' + filePath;
}

// @tauri-apps/plugin-opener
export const openUrl = isTest
  ? (globalThis as any).vi.fn().mockResolvedValue(undefined)
  : (url: string): Promise<void> => {
      window.open(url, '_blank');
      return Promise.resolve();
    };

// @tauri-apps/plugin-deep-link
export const onOpenUrl = isTest
  ? (globalThis as any).vi.fn().mockResolvedValue(() => {})
  : (handler: (urls: string[]) => void): Promise<() => void> => {
      return Promise.resolve(() => {});
    };

// @tauri-apps/plugin-notification
export const isPermissionGranted = isTest
  ? (globalThis as any).vi.fn().mockResolvedValue(true)
  : (): Promise<boolean> => {
      return Promise.resolve(true);
    };

export const requestPermission = isTest
  ? (globalThis as any).vi.fn().mockResolvedValue('granted')
  : (): Promise<'granted' | 'denied' | 'default'> => {
      return Promise.resolve('granted');
    };

export function sendNotification(options: string | { title: string; body?: string }): void {
  const title = typeof options === 'string' ? options : options.title;
  const body = typeof options === 'string' ? undefined : options.body;
  if (typeof Notification !== 'undefined') {
    new Notification(title, { body });
  }
}
