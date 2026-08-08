import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {ArtifactCard} from '../components/artifact-card.js';
import type {ArtifactInfo} from '../../maho_ai.mojom-webui.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function artifact(overrides: Partial<ArtifactInfo> = {}): ArtifactInfo {
  return {
    artifactId: 'a1',
    sessionId: 's1',
    displayName: 'plan.html',
    mimeType: 'text/html',
    sizeBytes: 2048n,
    createdAt: 1,
    ...overrides,
  };
}

function button(container: HTMLElement, label: string): HTMLButtonElement | null {
  return container.querySelector<HTMLButtonElement>(`button[aria-label="${label}"]`);
}

function openMenu(container: HTMLElement, displayName = 'plan.html'): void {
  const trigger = container.querySelector<HTMLElement>(`[aria-label="Actions for ${displayName}"]`)!;
  act(() => {
    trigger.dispatchEvent(new MouseEvent('contextmenu', {
      bubbles: true,
      button: 2,
      clientX: 20,
      clientY: 20,
    }));
  });
}

function menuItem(label: string): HTMLElement {
  return Array.from(document.querySelectorAll<HTMLElement>('[role="menuitem"]'))
      .find(item => item.textContent?.trim() === label)!;
}

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(next => {
    resolve = next;
  });
  return {promise, resolve};
}

describe('ArtifactCard', () => {
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
  });

  it('renders the name, size and action-menu trigger', () => {
    act(() => root.render(
        <ArtifactCard artifact={artifact()} onOpenPreview={vi.fn()} onRename={vi.fn()} onDelete={vi.fn()} />));
    expect(container.textContent).toContain('plan.html');
    expect(container.textContent).toContain('2 KB');
    expect(container.querySelector('[aria-label="Actions for plan.html"]')).not.toBeNull();
    expect(button(container, 'Delete')).toBeNull();
    expect(button(container, 'Rename')).toBeNull();
    expect(button(container, 'Open preview')).toBeNull();
  });

  it('opens an accessible action menu from the keyboard', () => {
    act(() => root.render(
        <ArtifactCard artifact={artifact()} onOpenPreview={vi.fn()} onRename={vi.fn()} onDelete={vi.fn()} />));
    const trigger = container.querySelector<HTMLElement>('[aria-label="Actions for plan.html"]');
    expect(trigger).not.toBeNull();
    expect(trigger!.tabIndex).toBe(0);
    expect(container.querySelector('button[aria-label="Open preview"]')).toBeNull();
    expect(container.querySelector('button[aria-label="Rename"]')).toBeNull();
    expect(container.querySelector('button[aria-label="Delete"]')).toBeNull();

    act(() => {
      trigger!.dispatchEvent(new KeyboardEvent('keydown', {
        bubbles: true,
        key: 'F10',
        shiftKey: true,
      }));
    });

    expect(document.querySelector('[role="menu"]')).not.toBeNull();
    expect(Array.from(document.querySelectorAll('[role="menuitem"]')).map(item => item.textContent))
        .toEqual(['Open preview', 'Rename', 'Delete']);
  });

  it('triggers the delete callback and restores focus to the trigger', async () => {
    const onDelete = vi.fn().mockResolvedValue(true);
    act(() => root.render(<ArtifactCard artifact={artifact()} onDelete={onDelete} />));
    const trigger = container.querySelector<HTMLElement>('[aria-label="Actions for plan.html"]')!;
    trigger.focus();
    openMenu(container);
    await act(async () => {
      menuItem('Delete').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    expect(onDelete).toHaveBeenCalledTimes(1);
    expect(document.activeElement).toBe(trigger);
  });

  it('focuses the mounted inline metadata input on rename and restores focus on Escape', async () => {
    act(() => root.render(<ArtifactCard artifact={artifact()} onRename={vi.fn()} />));
    const trigger = container.querySelector<HTMLElement>('[aria-label="Actions for plan.html"]')!;
    trigger.focus();
    expect(container.querySelector('input[aria-label="Artifact name"]')).toBeNull();
    openMenu(container);
    await act(async () => {
      menuItem('Rename').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    const input = container.querySelector<HTMLInputElement>('input[aria-label="Artifact name"]');
    expect(input).not.toBeNull();
    expect(input!.value).toBe('plan.html');
    expect(document.activeElement).toBe(input);
    expect(document.activeElement).not.toBe(document.body);
    expect(Array.from(container.querySelectorAll('button'))
        .some(candidate => candidate.textContent === 'Save')).toBe(false);

    await act(async () => {
      input!.dispatchEvent(new KeyboardEvent('keydown', {bubbles: true, key: 'Escape'}));
    });
    expect(container.querySelector('input[aria-label="Artifact name"]')).toBeNull();
    expect(document.activeElement).toBe(trigger);
  });

  it('restores focus after preview selection and menu Escape', async () => {
    const onOpenPreview = vi.fn();
    act(() => root.render(<ArtifactCard artifact={artifact()} onOpenPreview={onOpenPreview} />));
    const trigger = container.querySelector<HTMLElement>('[aria-label="Actions for plan.html"]')!;
    trigger.focus();
    openMenu(container);
    await act(async () => {
      menuItem('Open preview').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    expect(onOpenPreview).toHaveBeenCalledTimes(1);
    expect(document.activeElement).toBe(trigger);

    openMenu(container);
    await act(async () => {
      document.querySelector<HTMLElement>('[role="menu"]')!.dispatchEvent(
          new KeyboardEvent('keydown', {bubbles: true, key: 'Escape'}));
    });
    expect(document.querySelector('[role="menu"]')).toBeNull();
    expect(document.activeElement).toBe(trigger);
  });

  it('gates Delete/Delete, Delete/Rename, and Delete/preview while deletion is pending', async () => {
    const deletion = deferred<boolean>();
    const onDelete = vi.fn(() => deletion.promise);
    const onRename = vi.fn();
    const onOpenPreview = vi.fn();
    act(() => root.render(
        <ArtifactCard
          artifact={artifact()}
          onDelete={onDelete}
          onRename={onRename}
          onOpenPreview={onOpenPreview}
        />));
    const trigger = container.querySelector<HTMLElement>('[aria-label="Actions for plan.html"]')!;
    openMenu(container);
    act(() => {
      menuItem('Delete').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    expect(onDelete).toHaveBeenCalledTimes(1);
    expect(trigger.getAttribute('aria-disabled')).toBe('true');

    openMenu(container);
    expect(document.querySelector('[role="menu"]')).toBeNull();
    expect(onDelete).toHaveBeenCalledTimes(1);
    expect(onRename).not.toHaveBeenCalled();
    expect(onOpenPreview).not.toHaveBeenCalled();

    await act(async () => deletion.resolve(false));
    expect(trigger.getAttribute('aria-disabled')).not.toBe('true');
    expect(onDelete).toHaveBeenCalledTimes(1);

    openMenu(container);
    await act(async () => {
      menuItem('Rename').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    expect(container.querySelector('input[aria-label="Artifact name"]')).not.toBeNull();
    expect(onRename).not.toHaveBeenCalled();

    act(() => {
      container.querySelector<HTMLInputElement>('input[aria-label="Artifact name"]')!
          .dispatchEvent(new KeyboardEvent('keydown', {bubbles: true, key: 'Escape'}));
    });
    openMenu(container);
    act(() => {
      menuItem('Open preview').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    expect(onOpenPreview).toHaveBeenCalledTimes(1);
  });

  it('retains the card and exposes an accessible error when deletion fails', async () => {
    const onDelete = vi.fn().mockResolvedValue(false);
    act(() => root.render(<ArtifactCard artifact={artifact()} onDelete={onDelete} />));
    openMenu(container);
    await act(async () => {
      menuItem('Delete').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    expect(container.textContent).toContain('plan.html');
    expect(container.querySelector('[role="alert"]')?.textContent).toBe('Delete failed');
  });

  it('surfaces a rename error and keeps editing', async () => {
    const onRename = vi.fn().mockResolvedValue('unsafe name');
    act(() => root.render(<ArtifactCard artifact={artifact()} onRename={onRename} />));
    openMenu(container);
    act(() => {
      menuItem('Rename').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    const input = container.querySelector<HTMLInputElement>('input[aria-label="Artifact name"]')!;
    act(() => {
      const setter = Object.getOwnPropertyDescriptor(
          window.HTMLInputElement.prototype, 'value')!.set!;
      setter.call(input, '../x');
      input.dispatchEvent(new Event('input', {bubbles: true}));
    });
    await act(async () => {
      input.dispatchEvent(new KeyboardEvent('keydown', {bubbles: true, key: 'Enter'}));
    });
    expect(onRename).toHaveBeenCalledWith('../x');
    expect(container.querySelector('[role="alert"]')?.textContent).toBe('unsafe name');
    expect(container.querySelector('input[aria-label="Artifact name"]')).not.toBeNull();
  });

  it('renders a generic icon for an unknown mime without crashing', () => {
    act(() => root.render(<ArtifactCard artifact={artifact({mimeType: 'application/x-weird'})} />));
    expect(container.textContent).toContain('plan.html');
  });

  it('expands an inline sandboxed preview iframe', async () => {
    const getPreviewUrl = vi.fn().mockResolvedValue(
        'chrome-untrusted://maho-ai-artifact-preview/cap');
    act(() => root.render(<ArtifactCard artifact={artifact()} getPreviewUrl={getPreviewUrl} />));
    expect(container.querySelector('iframe')).toBeNull();
    await act(async () => {
      button(container, 'Toggle preview')!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    const iframe = container.querySelector('iframe')!;
    expect(iframe).not.toBeNull();
    expect(iframe.getAttribute('sandbox')).toBe('');
    expect(iframe.getAttribute('referrerpolicy')).toBe('no-referrer');
    expect(iframe.getAttribute('src')).toBe('chrome-untrusted://maho-ai-artifact-preview/cap');
  });

  it('shows an error state when the preview url is null', async () => {
    const getPreviewUrl = vi.fn().mockResolvedValue(null);
    act(() => root.render(<ArtifactCard artifact={artifact()} getPreviewUrl={getPreviewUrl} />));
    await act(async () => {
      button(container, 'Toggle preview')!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    expect(container.querySelector('iframe')).toBeNull();
    expect(container.querySelector('[role="alert"]')?.textContent)
        .toContain('Preview unavailable');
  });

  it('refetches the preview url on each expand', async () => {
    const getPreviewUrl = vi.fn().mockResolvedValue(
        'chrome-untrusted://maho-ai-artifact-preview/cap');
    act(() => root.render(<ArtifactCard artifact={artifact()} getPreviewUrl={getPreviewUrl} />));
    const toggle = () => act(async () => {
      button(container, 'Toggle preview')!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    await toggle();
    await toggle();
    await toggle();
    expect(getPreviewUrl).toHaveBeenCalledTimes(2);
  });

  it('sets DownloadURL data on dragstart when an export url is available', async () => {
    const getExportUrl = vi.fn().mockResolvedValue(
        'chrome-untrusted://maho-ai-artifact-export/cap');
    await act(async () => {
      root.render(<ArtifactCard artifact={artifact()} getExportUrl={getExportUrl} />);
    });
    const article = container.querySelector('article')!;
    expect(article.getAttribute('draggable')).toBe('true');
    const setData = vi.fn();
    const dataTransfer = {setData, effectAllowed: ''};
    const event = new Event('dragstart', {bubbles: true});
    Object.defineProperty(event, 'dataTransfer', {value: dataTransfer});
    act(() => {
      article.dispatchEvent(event);
    });
    expect(setData).toHaveBeenCalledWith(
        'DownloadURL',
        'text/html:plan.html:chrome-untrusted://maho-ai-artifact-export/cap');
    expect(dataTransfer.effectAllowed).toBe('copy');
  });

  it('is not draggable when no export url is available', async () => {
    const getExportUrl = vi.fn().mockResolvedValue(null);
    await act(async () => {
      root.render(<ArtifactCard artifact={artifact()} getExportUrl={getExportUrl} />);
    });
    expect(container.querySelector('article')!.getAttribute('draggable')).toBe('false');
  });
});
