// Presentation descriptors for the session runtime-config controls (plan
// row 4). Pure data — no DOM, no store imports — so the card component and
// tests stay trivially separable. Enforcement of these flags lives in the
// CapabilityBroker; this module only names what the UI shows.
import type {PermissionTierValue} from '../types.js';

export interface RuntimeTierOption {
  readonly value: PermissionTierValue;
  readonly label: string;
  readonly description: string;
}

// Ordered from most to least restrictive; the UI renders them in this order.
export const RUNTIME_TIER_OPTIONS: readonly RuntimeTierOption[] = [
  {
    value: 'read_only',
    label: 'Read-only',
    description: 'Browse and read only; no writes',
  },
  {
    value: 'guard',
    label: 'Guard',
    description: 'Ask before risky actions',
  },
  {
    value: 'full_access',
    label: 'Full access',
    description: 'Act without asking',
  },
] as const;

export function getRuntimeTierOption(value?: string|null): RuntimeTierOption {
  const match = RUNTIME_TIER_OPTIONS.find(option => option.value === value);
  return match ?? RUNTIME_TIER_OPTIONS[1]!;
}
