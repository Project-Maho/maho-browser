// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {SettingValue} from '../../mojo.js';
import {ALL_PANE_DEFINITIONS} from '../../schema/panes.js';
import {SETTING_METADATA} from '../../schema/setting_schema.js';
import {AiDevelopersPane, MCP_ENABLED_SETTING_KEY} from '../ai_developers.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

const DEVELOPERS_PANE =
    ALL_PANE_DEFINITIONS.find(pane => pane.key === 'maho-ai-developers')!;
const SWITCH_LABEL = 'Allow local MCP connections';

let container: HTMLDivElement;
let root: Root;

beforeEach(() => {
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
});

afterEach(() => {
  act(() => root.unmount());
  container.remove();
  vi.restoreAllMocks();
});

function render(settings: SettingValue[]) {
  const commitSettingValue = vi.fn().mockResolvedValue(true);
  // The pane also renders AgentChannels, which lists channels on mount.
  const handler = {
    listAgentChannels: vi.fn().mockResolvedValue(
        {json: JSON.stringify({channels: [], locked: false})}),
  };
  const store = {commitSettingValue, getHandler: () => handler} as never;
  act(() => {
    root.render(
        <AiDevelopersPane pane={DEVELOPERS_PANE} settings={settings} store={store} />);
  });
  return {commitSettingValue};
}

function mcpSwitch(): HTMLButtonElement {
  const control = container.querySelector<HTMLButtonElement>(
      `[role="switch"][aria-label="${SWITCH_LABEL}"]`);
  if (!control) throw new Error('missing MCP switch');
  return control;
}

async function click(element: HTMLElement): Promise<void> {
  await act(async () => {
    element.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    await Promise.resolve();
  });
}

describe('MCP off switch', () => {
  it('is a profile-scoped toggle setting', () => {
    expect(MCP_ENABLED_SETTING_KEY).toBe('developers.mcp_enabled');
    expect(SETTING_METADATA[MCP_ENABLED_SETTING_KEY]).toMatchObject({
      control: 'toggle',
      scope: 'profile',
      selectedProfile: false,
    });
  });

  it('commits false when the user turns MCP off', async () => {
    const {commitSettingValue} = render(
        [{key: MCP_ENABLED_SETTING_KEY, value: 'true'} as SettingValue]);
    const control = mcpSwitch();
    expect(control.getAttribute('aria-checked')).toBe('true');

    await click(control);
    expect(commitSettingValue).toHaveBeenCalledWith(
        MCP_ENABLED_SETTING_KEY, 'false');
  });

  it('reflects a disabled pref and commits true when re-enabled', async () => {
    const {commitSettingValue} = render(
        [{key: MCP_ENABLED_SETTING_KEY, value: 'false'} as SettingValue]);
    const control = mcpSwitch();
    expect(control.getAttribute('aria-checked')).toBe('false');

    await click(control);
    expect(commitSettingValue).toHaveBeenCalledWith(
        MCP_ENABLED_SETTING_KEY, 'true');
  });
});
