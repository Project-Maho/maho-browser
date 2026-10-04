import type { MahoBridge } from '../../bridge/types';
import type { AiProvider } from './ai-settings-state';

export async function persistApiKey(
  bridge: MahoBridge,
  provider: AiProvider,
  key: string,
): Promise<void> {
  switch (provider) {
    case 'openai':
    case 'anthropic': {
      const saved = await bridge.byokSetKey(provider, key);
      if (!saved) throw new Error(`Failed to save the ${getProviderName(provider)} API key.`);
      return;
    }
    case 'openai-compatible':
      await bridge.setAiApiKey(key);
      return;
    case '':
    case 'maho-managed':
      return;
    default:
      return assertNever(provider);
  }
}

export async function removeApiKey(
  bridge: MahoBridge,
  provider: AiProvider,
): Promise<void> {
  switch (provider) {
    case 'openai':
    case 'anthropic': {
      const cleared = await bridge.byokDeleteKey(provider);
      if (!cleared) throw new Error(`Failed to clear the ${getProviderName(provider)} API key.`);
      return;
    }
    case 'openai-compatible':
      await bridge.setAiApiKey('');
      return;
    case '':
    case 'maho-managed':
      return;
    default:
      return assertNever(provider);
  }
}

export function getProviderName(provider: AiProvider): string {
  switch (provider) {
    case 'openai':
      return 'OpenAI';
    case 'anthropic':
      return 'Anthropic';
    case 'openai-compatible':
      return 'OpenAI-compatible';
    case '':
    case 'maho-managed':
      return 'Maho Managed';
    default:
      return assertNever(provider);
  }
}

function assertNever(value: never): never {
  throw new Error(`Unhandled AI provider: ${JSON.stringify(value)}`);
}
