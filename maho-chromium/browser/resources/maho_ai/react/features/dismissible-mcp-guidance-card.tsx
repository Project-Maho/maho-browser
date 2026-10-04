import React, {useState} from 'react';

import {McpAgentGuidanceCard} from '../../../maho_common/react/mcp-agent-guidance-card.js';

/**
 * localStorage key persisting the user's dismissal of the MCP guidance card on
 * the Maho AI surface. Scoped to the chrome://maho-ai origin, so the compact and
 * developer surfaces share the same dismissal state, while the settings surface
 * (a different origin) is unaffected and always shows the card.
 */
const MCP_GUIDANCE_DISMISSED_KEY = 'maho_ai_mcp_guidance_dismissed';

function readDismissed(): boolean {
  try {
    return localStorage.getItem(MCP_GUIDANCE_DISMISSED_KEY) === 'true';
  } catch {
    return false;
  }
}

function writeDismissed(): void {
  try {
    localStorage.setItem(MCP_GUIDANCE_DISMISSED_KEY, 'true');
  } catch {
    // Ignore storage failures (e.g. disabled storage); dismissal is best-effort.
  }
}

interface DismissibleMcpGuidanceCardProps {
  readonly className?: string;
  readonly onConnect?: () => void;
}

export function DismissibleMcpGuidanceCard({className, onConnect}: DismissibleMcpGuidanceCardProps) {
  const [dismissed, setDismissed] = useState<boolean>(readDismissed);

  if (dismissed) {
    return null;
  }

  return (
    <McpAgentGuidanceCard
      className={className}
      onConnect={onConnect}
      onDismiss={() => {
        writeDismissed();
        setDismissed(true);
      }}
    />
  );
}
