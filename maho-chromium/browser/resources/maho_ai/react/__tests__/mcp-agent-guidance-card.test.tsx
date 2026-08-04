import {readFileSync} from 'node:fs';
import path from 'node:path';
import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {McpAgentGuidanceCard} from '../../../maho_common/react/mcp-agent-guidance-card.js';
import {DismissibleMcpGuidanceCard} from '../features/dismissible-mcp-guidance-card.js';

const sourceContractPaths = [
  '../maho_common/react/ui/mcp-agent-guidance-card.tsx',
  'react/features/compact/compact-stage.tsx',
  'react/features/developer/developer-workspace.tsx',
  '../maho_settings/react/mcp_agent_guidance_settings_section.tsx',
] as const;

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function requireElement<T extends Element>(element: T | null, description: string): T {
  if (element) {
    return element;
  }

  throw new Error(`Missing ${description}`);
}

function readSourceContract(): string {
  return sourceContractPaths
      .map(sourcePath => readFileSync(path.join(process.cwd(), sourcePath), 'utf8'))
      .join('\n');
}

function readSource(sourcePath: string): string {
  return readFileSync(path.join(process.cwd(), sourcePath), 'utf8');
}

describe('McpAgentGuidanceCard', () => {
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

  it('renders the compact horizontal MCP CTA contract', () => {
    act(() => root.render(<McpAgentGuidanceCard />));

    const note = requireElement(container.querySelector('[role="note"]'), 'note');
    const button = requireElement(note.querySelector('button'), 'CTA button');
    const tiles = note.querySelectorAll('[data-mcp-agent-tile]');

    expect(note.textContent).toContain('Use Maho MCP with Codex, Claude, OpenCode, or Pi.');
    expect(button.textContent).toBe('Connect');
    expect(button.getAttribute('aria-label')).toBe('Open Maho MCP setup guide');
    expect(tiles).toHaveLength(4);
    expect(note.querySelector('pre')).toBeNull();
    expect(note.querySelector('ol')).toBeNull();
    expect(note.textContent).not.toContain('config.toml');
    expect(note.textContent).not.toContain('[mcp_servers.maho]');
    expect(note.textContent).not.toContain('restart Codex');
  });

  it('calls the connect handler without copying the command', async () => {
    const onConnect = vi.fn<() => void>();
    const writeText = vi.fn<(text: string) => Promise<void>>().mockResolvedValue(undefined);
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {writeText},
    });

    act(() => root.render(<McpAgentGuidanceCard onConnect={onConnect} />));

    const button = requireElement(container.querySelector('button'), 'CTA button');

    await act(async () => {
      button.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(onConnect).toHaveBeenCalledOnce();
    expect(writeText).not.toHaveBeenCalled();
    expect(button.textContent).toBe('Connect');
  });

  it('renders a dismiss control only when onDismiss is provided', async () => {
    const onDismiss = vi.fn<() => void>();
    const onConnect = vi.fn<() => void>();

    act(() => root.render(<McpAgentGuidanceCard onConnect={onConnect} onDismiss={onDismiss} />));

    const note = requireElement(container.querySelector('[role="note"]'), 'note');
    const dismiss = requireElement(
        note.querySelector('button[aria-label="Dismiss"]'), 'dismiss button');
    const firstButton = requireElement(note.querySelector('button'), 'first button');

    expect(firstButton.textContent).toBe('Connect');

    await act(async () => {
      dismiss.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(onDismiss).toHaveBeenCalledOnce();
    expect(onConnect).not.toHaveBeenCalled();
  });

  it('omits the dismiss control without onDismiss', () => {
    act(() => root.render(<McpAgentGuidanceCard />));

    const note = requireElement(container.querySelector('[role="note"]'), 'note');
    expect(note.querySelector('button[aria-label="Dismiss"]')).toBeNull();
  });

  it('keeps the compact card capped but lets the settings surface go full width', () => {
    const cardSource = readSource('../maho_common/react/ui/mcp-agent-guidance-card.tsx');
    const settingsSection =
        readSource('../maho_settings/react/mcp_agent_guidance_settings_section.tsx');

    expect(cardSource).toContain('max-w-[23.5rem]');
    expect(settingsSection).toContain('max-w-none');
  });

  it('keeps the source contract free of the old guide-panel affordances', () => {
    const sourceContract = readSourceContract();

    expect(sourceContract).not.toContain('<pre');
    expect(sourceContract).not.toContain('<ol');
    expect(sourceContract).not.toContain('config.toml');
    expect(sourceContract).not.toContain('[mcp_servers.maho]');
    expect(sourceContract).not.toContain('restart Codex');
    expect(sourceContract).not.toContain('<code');
    expect(sourceContract).not.toContain('max-w-md');
    expect(sourceContract).not.toContain('sm:grid-cols');
    expect(sourceContract).not.toContain('max-sm:ml');
  });

  it('registers a setup-only Developers settings pane', () => {
    const paneSchema = readSource('../maho_settings/schema/panes.ts');
    const paneSwitch = readSource('../maho_settings/react/domain_panes.tsx');
    const developersPane = readSource('../maho_settings/react/ai_developers.tsx');
    const settingsBuild = readSource('../maho_settings/BUILD.gn');

    expect(paneSchema).toContain("key: 'maho-ai-developers'");
    expect(paneSchema).toContain("navTitle: 'Developers'");
    expect(paneSchema).toContain("contentKind: 'ai-developers'");
    expect(paneSwitch).toContain("case 'ai-developers':");
    expect(settingsBuild).toContain('react/ai_developers.tsx');
    expect(developersPane).toContain('maho run "Open localhost:3000 and run smoke test"');
    expect(developersPane).toContain('maho mcp');
    expect(developersPane).toContain('maho browser repl');
    expect(developersPane).toContain('Maho MCP server');
    expect(developersPane).toContain('Add this to agents that support MCP servers.');
    expect(developersPane).not.toContain('github.com');
    expect(developersPane).not.toContain('open source');
    expect(developersPane).toContain('curl -fsSL https://mahobrowser.com/install-cli.sh | sh');
  });
});

describe('DismissibleMcpGuidanceCard', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    localStorage.clear();
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    localStorage.clear();
    vi.restoreAllMocks();
  });

  it('shows the card until dismissed, then persists the dismissal', async () => {
    act(() => root.render(<DismissibleMcpGuidanceCard />));

    const dismiss = requireElement(
        container.querySelector('button[aria-label="Dismiss"]'), 'dismiss button');

    await act(async () => {
      dismiss.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(container.querySelector('[role="note"]')).toBeNull();
    expect(localStorage.getItem('maho_ai_mcp_guidance_dismissed')).toBe('true');
  });

  it('stays hidden on remount when previously dismissed', () => {
    localStorage.setItem('maho_ai_mcp_guidance_dismissed', 'true');

    act(() => root.render(<DismissibleMcpGuidanceCard />));

    expect(container.querySelector('[role="note"]')).toBeNull();
  });
});
