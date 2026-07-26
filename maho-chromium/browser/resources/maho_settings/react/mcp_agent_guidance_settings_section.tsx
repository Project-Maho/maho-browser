import {McpAgentGuidanceCard} from '../../maho_common/react/mcp-agent-guidance-card.js';
import type {MahoSettingsStore} from './store.js';

export function McpAgentGuidanceSettingsSection(
    {store}: {readonly store: MahoSettingsStore}) {
  return (
    <div className="mb-4 px-1">
      <McpAgentGuidanceCard
        className="max-w-none"
        onConnect={() => store.selectPane('maho-ai-developers')} />
    </div>
  );
}
