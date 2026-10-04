import type {DomainDefinition, DomainId} from '../models.js';

export const DOMAIN_DEFINITIONS: DomainDefinition[] = [
  {id: 'browsing', label: 'Browsing', order: 0},
  {id: 'look-and-feel', label: 'Look & feel', order: 1},
  {id: 'identity', label: 'Identity', order: 2},
  {id: 'productivity', label: 'Productivity', order: 3},
  {id: 'protection', label: 'Protection', order: 4},
  // Top-level "Mail" owns the enable toggle; this group also holds the
  // per-account detail panes revealed once Mail is enabled.
  {id: 'mail', label: 'Mail', order: 5},
];

export const DOMAIN_MAP: Record<DomainId, DomainDefinition> =
    Object.fromEntries(DOMAIN_DEFINITIONS.map(d => [d.id, d])) as
        Record<DomainId, DomainDefinition>;
