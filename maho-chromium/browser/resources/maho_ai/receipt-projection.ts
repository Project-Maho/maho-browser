export const RECEIPT_SCHEMA_VERSION = 1;
export const RECEIPT_RESULT_VERSION = 1;
export const REDACTED_VALUE = '[REDACTED]';

export type ReceiptOutcomeStatus =
    'succeeded'|'failed'|'denied'|'disconnected'|'unknown';

export interface ReceiptProjection {
  schemaVersion: number;
  resultVersion: number;
  receiptId: string;
  capabilityId: string;
  controller: {
    id: string|null;
    name: string;
    type: string;
    plane: string;
  };
  target: {
    tabId: number|null;
    origin: string|null;
  };
  category: string;
  sensitivity: string;
  approval: string;
  outcome: {
    status: ReceiptOutcomeStatus;
    code: string|null;
  };
  timestamps: {
    startedAt: number|null;
    completedAt: number|null;
  };
}

type JsonRecord = Record<string, unknown>;

const SECRET_KEY_FRAGMENTS = [
  'authorization', 'cookie', 'credential', 'password', 'passcode', 'secret',
  'token', 'otp', 'api_key', 'apikey', 'private_key',
];
const PAGE_PAYLOAD_KEY_FRAGMENTS = [
  'arguments', 'content', 'dom', 'html', 'pagepayload', 'pagetext',
  'page_payload', 'page_text', 'screenshot', 'selector', 'snapshot',
];

function record(value: unknown): JsonRecord|null {
  return value !== null && typeof value === 'object' && !Array.isArray(value) ?
      value as JsonRecord : null;
}

function field(source: JsonRecord|null, ...keys: string[]): unknown {
  for (const key of keys) {
    if (source && source[key] !== undefined && source[key] !== null) {
      return source[key];
    }
  }
  return undefined;
}

function text(value: unknown, fallback = ''): string {
  return typeof value === 'string' && value.trim() ? value.trim() : fallback;
}

function number(value: unknown): number|null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function outcomeStatus(value: unknown): ReceiptOutcomeStatus {
  const normalized = text(value).toLowerCase().replaceAll('-', '_');
  if (['success', 'succeeded', 'completed', 'done'].includes(normalized)) {
    return 'succeeded';
  }
  if (['failure', 'failed', 'error'].includes(normalized)) {
    return 'failed';
  }
  if (['denied', 'rejected', 'approval_denied'].includes(normalized)) {
    return 'denied';
  }
  if (['disconnected', 'cancelled', 'canceled', 'timeout'].includes(normalized)) {
    return 'disconnected';
  }
  return 'unknown';
}

// Projects an agent interaction request lifecycle state (maho-agent
// interaction::InteractionState, plan row 8) onto a receipt outcome status so
// terminal approvals speak the same language as execution receipts. Pending
// and unrecognized states project to 'unknown': nothing has happened yet.
export function interactionStateOutcome(state?: string|null):
    ReceiptOutcomeStatus {
  switch ((state ?? '').trim().toLowerCase()) {
    case 'resolved':
      return 'succeeded';
    case 'denied':
      return 'denied';
    case 'expired':
    case 'cancelled':
      return 'disconnected';
    default:
      return 'unknown';
  }
}

export function redactReceiptValue(value: unknown): unknown {
  if (Array.isArray(value)) {
    return value.map(redactReceiptValue);
  }
  const source = record(value);
  if (!source) {
    return value;
  }
  const result: JsonRecord = {};
  for (const key of Object.keys(source).sort()) {
    const normalized = key.toLowerCase();
    const shouldRedact = [...SECRET_KEY_FRAGMENTS, ...PAGE_PAYLOAD_KEY_FRAGMENTS]
        .some(fragment => normalized.includes(fragment));
    result[key] = shouldRedact ? REDACTED_VALUE : redactReceiptValue(source[key]);
  }
  return result;
}

export function projectReceipt(value: unknown): ReceiptProjection|null {
  const root = record(value);
  const wire = record(field(root, 'receipt')) || root;
  if (!wire) {
    return null;
  }
  const metadata = record(field(wire, 'metadata')) || wire;
  const controller = record(field(metadata, 'controller'));
  const target = record(field(metadata, 'target'));
  const outcome = record(field(metadata, 'outcome', 'result', 'failure'));
  const timestamps = record(field(metadata, 'timestamps'));
  const receiptId = text(field(
      wire, 'receiptId', 'receipt_id', 'executionId', 'execution_id'));
  const capabilityId = text(field(
      wire, 'capabilityId', 'capability_id', 'capability'));
  if (!receiptId && !capabilityId && !field(metadata, 'state', 'status', 'outcome')) {
    return null;
  }

  const approvalValue = field(
      metadata, 'approval', 'approvalOutcome', 'approval_outcome');
  const statusValue = field(
      outcome, 'status', 'state') ?? field(metadata, 'state', 'status');
  return {
    schemaVersion: RECEIPT_SCHEMA_VERSION,
    resultVersion: RECEIPT_RESULT_VERSION,
    receiptId: receiptId || 'unknown',
    capabilityId: capabilityId || 'unknown',
    controller: {
      id: text(field(controller, 'id', 'sessionId', 'session_id')) || null,
      name: text(field(controller, 'name', 'displayName', 'display_name'), 'Unknown controller'),
      type: text(field(controller, 'type', 'kind'), 'unknown'),
      plane: text(field(controller, 'plane', 'controlPlane', 'control_plane'), 'unknown'),
    },
    target: {
      tabId: number(field(target, 'tabId', 'tab_id')),
      origin: text(field(target, 'origin')) || null,
    },
    category: text(field(metadata, 'category'), 'unknown'),
    sensitivity: text(field(metadata, 'sensitivity'), 'unknown'),
    approval: text(approvalValue, 'not_requested'),
    outcome: {
      status: outcomeStatus(statusValue),
      code: text(field(outcome, 'code') ?? field(metadata, 'code')) || null,
    },
    timestamps: {
      startedAt: number(field(
          timestamps, 'startedAt', 'started_at') ??
          field(metadata, 'startedAt', 'started_at', 'createdAt', 'created_at')),
      completedAt: number(field(
          timestamps, 'completedAt', 'completed_at') ??
          field(metadata, 'completedAt', 'completed_at', 'updatedAt', 'updated_at')),
    },
  };
}

export function receiptFromToolOutput(output?: string|null): ReceiptProjection|null {
  if (!output?.trim()) {
    return null;
  }
  try {
    return projectReceipt(JSON.parse(output));
  } catch {
    return null;
  }
}

export function receiptSummary(receipt: ReceiptProjection): string {
  const action = receipt.category === 'unknown' ? 'Browser work' : receipt.category;
  const target = receipt.target.origin ? ` on ${receipt.target.origin}` : '';
  return `${action}${target} ${receipt.outcome.status}`;
}
