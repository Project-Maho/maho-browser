import type { AgentHandle, MahoBridge } from '../../bridge/types';

const CLEANUP_WARNING =
  '[maho-agent] Native session cleanup failed after UI teardown; native owns final registry cleanup.';

export function freeSessionAfterTeardown(bridge: MahoBridge, handle: AgentHandle): void {
  void bridge.agentFreeSession(handle).catch(reportNonBlockingCleanupError);
}

export async function freeSessionForReset(bridge: MahoBridge, handle: AgentHandle): Promise<void> {
  try {
    await bridge.agentFreeSession(handle);
  } catch (error) {
    reportNonBlockingCleanupError(error);
  }
}

function reportNonBlockingCleanupError(error: unknown): void {
  if (error instanceof Error) {
    console.warn(CLEANUP_WARNING, error.message);
    return;
  }

  console.warn(CLEANUP_WARNING);
}
