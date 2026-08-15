// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {AiMailReadConsentSetting} from '../domain_panes.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

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
  vi.clearAllMocks();
});

function renderSetting(
    allowed: boolean,
    save: (allowed: boolean) => Promise<boolean>) {
  act(() => {
    root.render(
        <AiMailReadConsentSetting allowed={allowed} onSave={save} />);
  });
}

function consentSwitch(): HTMLButtonElement {
  const control = container.querySelector<HTMLButtonElement>(
      '[role="switch"][aria-label="Allow AI to read Mail"]');
  if (!control) throw new Error('missing AI Mail consent switch');
  return control;
}

describe('AI Mail read consent', () => {
  it('renders only the authoritative loaded value without an enabled-state flash', () => {
    renderSetting(false, vi.fn());
    expect(consentSwitch().getAttribute('aria-checked')).toBe('false');
    expect(container.textContent).toContain(
        'accounts, folders, message metadata, and message bodies');
  });

  it('is keyboard operable and exposes its description to assistive technology', async () => {
    const save = vi.fn().mockResolvedValue(true);
    renderSetting(false, save);
    const control = consentSwitch();
    const descriptionId = control.getAttribute('aria-describedby');
    expect(descriptionId).toBeTruthy();
    expect(document.getElementById(descriptionId!)?.textContent).toContain(
        'message bodies');

    await act(async () => {
      control.focus();
      control.click();
    });
    expect(save).toHaveBeenCalledWith(true);
  });

  it('rolls back to the authoritative value and announces a failed save', async () => {
    const save = vi.fn().mockResolvedValue(false);
    renderSetting(false, save);

    await act(async () => {
      consentSwitch().click();
    });

    expect(save).toHaveBeenCalledWith(true);
    expect(consentSwitch().getAttribute('aria-checked')).toBe('false');
    const alert = container.querySelector('[role="alert"]');
    expect(alert?.textContent).toContain('Could not save Mail access');
  });
});
