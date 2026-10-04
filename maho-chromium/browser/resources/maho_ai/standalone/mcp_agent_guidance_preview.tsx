import React from 'react';
import {createRoot} from 'react-dom/client';

import {McpAgentGuidanceCard} from '../../maho_common/react/mcp-agent-guidance-card.js';

const rootElement = document.getElementById('preview-root');

if (!rootElement) {
  throw new Error('Missing #preview-root for MCP agent guidance preview.');
}

createRoot(rootElement).render(
    <React.StrictMode>
      <McpAgentGuidanceCard />
    </React.StrictMode>);
