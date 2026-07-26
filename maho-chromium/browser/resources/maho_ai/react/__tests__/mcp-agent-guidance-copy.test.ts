import {describe, expect, it} from 'vitest';

import {
  MCP_AGENT_GUIDANCE_CLIENTS,
  MCP_AGENT_GUIDANCE_COMMAND,
  MCP_AGENT_GUIDANCE_CTA_LABEL,
  MCP_AGENT_GUIDANCE_VISIBLE_TEXT,
} from '../../../maho_common/react/mcp-agent-guidance-copy.js';

describe('MCP agent guidance copy', () => {
  it('copies the exact neutral local MCP command', () => {
    expect(MCP_AGENT_GUIDANCE_COMMAND).toBe('maho mcp');
  });

  it('names every supported MCP-capable agent in one concise visible line', () => {
    expect(MCP_AGENT_GUIDANCE_CLIENTS).toEqual(['Codex', 'Claude', 'OpenCode', 'Pi']);
    expect(MCP_AGENT_GUIDANCE_VISIBLE_TEXT).toBe(
        'Use Maho MCP with Codex, Claude, OpenCode, or Pi.');
    expect(MCP_AGENT_GUIDANCE_CTA_LABEL).toBe('Connect');
  });

  it('omits inline TOML, Codex config paths, and restart instructions', () => {
    const visibleCopy = [
      MCP_AGENT_GUIDANCE_VISIBLE_TEXT,
      MCP_AGENT_GUIDANCE_COMMAND,
      MCP_AGENT_GUIDANCE_CTA_LABEL,
      ...MCP_AGENT_GUIDANCE_CLIENTS,
    ].join('\n');

    expect(visibleCopy).not.toContain('[mcp_servers.maho]');
    expect(visibleCopy).not.toContain('~/.codex/config.toml');
    expect(visibleCopy).not.toContain('config.toml');
    expect(visibleCopy).not.toContain('restart Codex');
  });
});
