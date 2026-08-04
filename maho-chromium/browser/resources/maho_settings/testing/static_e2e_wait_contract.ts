#!/usr/bin/env bun

import {waitForFrameUrl} from './cdp_harness';

const testingDir = import.meta.dir;
const chromiumSettingsPath = `${testingDir}/e2e_pane_chromium_settings.ts`;
const profilesPath = `${testingDir}/e2e_pane_profiles.ts`;
const harnessPath = `${testingDir}/cdp_harness.ts`;

const [chromiumSettings, profiles, harness] = await Promise.all([
  Bun.file(chromiumSettingsPath).text(),
  Bun.file(profilesPath).text(),
  Bun.file(harnessPath).text(),
]);

function check(condition: boolean, message: string): void {
  if (!condition) {
    throw new Error(`E2E wait contract failed: ${message}`);
  }
  console.log(`PASS ${message}`);
}

const fixedWaitPattern = /(?:setTimeout\s*\(|Bun\.sleep\s*\(|waitForTimeout\s*\()/;
check(!fixedWaitPattern.test(chromiumSettings), 'Chromium Settings E2E has no fixed wait');
check(!fixedWaitPattern.test(profiles), 'Profiles E2E has no fixed wait');
check(
  /await cdp\.navigate[\s\S]*waitForPageTargetUrl\([\s\S]*chrome:\/\/settings[\s\S]*clickElementByText/.test(chromiumSettings),
  'Chromium Settings E2E observes the browser-owned chrome://settings target around the typed action',
);
check(!chromiumSettings.includes('waitForFrameUrl'), 'Chromium Settings E2E does not wait for an unsupported child frame');
check(
  /export async function waitForPageTargetUrl/.test(harness),
  'CDP harness exposes an event-driven top-level page-target wait',
);
check(
  /export function waitForFrameUrl/.test(harness) &&
    /cdp\.send\("Page\.getFrameTree"\)/.test(harness) &&
    /cdp\.on\("Page\.frameNavigated"/.test(harness),
  'CDP harness resolves frame readiness from current frame state or the frameNavigated event',
);
check(
  /Timed out waiting for frame URL prefix/.test(harness),
  'frame readiness timeout rejects with a specific error',
);
const frameWaitBody = harness.match(
  /export function waitForFrameUrl[\s\S]*?(?=export async function waitForCondition)/,
)?.[0] ?? '';
check(
  frameWaitBody.length > 0 && !/Bun\.sleep|while\s*\(/.test(frameWaitBody),
  'frame readiness does not use a sleep-based polling loop',
);
check(
  /Number\.isFinite\(timeoutMs\)[\s\S]*timeoutMs <= 0/.test(frameWaitBody),
  'frame readiness rejects malformed timeout values',
);
check(
  /response\?\.error[\s\S]*Page\.getFrameTree failed/.test(frameWaitBody),
  'frame readiness surfaces CDP frame-tree errors',
);
check(
  /for \(const child of frameTree\.childFrames \?\? \[\]\)/.test(harness) &&
    !/function findChildFrameUrl[\s\S]*?frameTree\.frame\.url\.startsWith/.test(harness),
  'initial frame-tree scan starts below the root frame',
);
check(
  /typeof frame\?\.parentId === "string"/.test(frameWaitBody),
  'frameNavigated acceptance requires a parentId',
);

type Listener = (params: unknown) => void;

class FakeCDP {
  readonly calls: string[] = [];
  readonly listeners = new Map<string, Set<Listener>>();
  constructor(private readonly frameTreeResponse: Promise<unknown>) {}
  on(method: string, listener: Listener): void {
    this.calls.push(`on:${method}`);
    const listeners = this.listeners.get(method) ?? new Set<Listener>();
    listeners.add(listener);
    this.listeners.set(method, listeners);
  }
  off(method: string, listener: Listener): void {
    this.calls.push(`off:${method}`);
    this.listeners.get(method)?.delete(listener);
  }
  send(method: string): Promise<unknown> {
    this.calls.push(`send:${method}`);
    return this.frameTreeResponse;
  }
  emit(method: string, params: unknown): void {
    for (const listener of this.listeners.get(method) ?? []) listener(params);
  }
  listenerCount(method: string): number {
    return this.listeners.get(method)?.size ?? 0;
  }
}

async function expectTimeout(promise: Promise<string>, label: string): Promise<void> {
  try {
    await promise;
    check(false, label);
  } catch (error) {
    check(error instanceof Error && error.message.includes('Timed out waiting for frame URL prefix'), label);
  }
}

const rootOnlyTree = new FakeCDP(Promise.resolve({
  result: {frameTree: {frame: {url: 'chrome://settings/'}}},
}));
await expectTimeout(
  waitForFrameUrl(rootOnlyTree as never, 'chrome://settings', 20),
  'root-only pre-existing frame tree cannot satisfy the child-frame wait',
);
check(rootOnlyTree.listenerCount('Page.frameNavigated') === 0, 'tree timeout removes the frame listener');

let resolveEventTree!: (value: unknown) => void;
const rootOnlyEvent = new FakeCDP(new Promise(resolve => { resolveEventTree = resolve; }));
const rootEventWait = waitForFrameUrl(rootOnlyEvent as never, 'chrome://settings', 20);
check(
  rootOnlyEvent.calls.slice(0, 2).join(',') === 'on:Page.frameNavigated,send:Page.getFrameTree',
  'frame listener is subscribed before the initial frame-tree query',
);
rootOnlyEvent.emit('Page.frameNavigated', {frame: {url: 'chrome://settings/'}});
await expectTimeout(rootEventWait, 'root frameNavigated event cannot satisfy the child-frame wait');
resolveEventTree({result: {frameTree: {frame: {url: 'chrome://maho-settings/'}}}});
check(rootOnlyEvent.listenerCount('Page.frameNavigated') === 0, 'event timeout removes the frame listener');

const childTree = new FakeCDP(Promise.resolve({
  result: {
    frameTree: {
      frame: {url: 'chrome://maho-settings/'},
      childFrames: [{frame: {url: 'chrome://settings/', parentId: 'root'}}],
    },
  },
}));
check(
  await waitForFrameUrl(childTree as never, 'chrome://settings', 100) === 'chrome://settings/',
  'pre-existing child frame satisfies the wait',
);
check(childTree.listenerCount('Page.frameNavigated') === 0, 'tree success removes the frame listener');

let resolveChildEventTree!: (value: unknown) => void;
const childEvent = new FakeCDP(new Promise(resolve => { resolveChildEventTree = resolve; }));
const childEventWait = waitForFrameUrl(childEvent as never, 'chrome://settings', 100);
childEvent.emit('Page.frameNavigated', {
  frame: {url: 'chrome://settings/privacy', parentId: 'root'},
});
check(
  await childEventWait === 'chrome://settings/privacy',
  'child frameNavigated event satisfies the wait',
);
resolveChildEventTree({result: {frameTree: {frame: {url: 'chrome://maho-settings/'}}}});
check(childEvent.listenerCount('Page.frameNavigated') === 0, 'event success removes the frame listener');
