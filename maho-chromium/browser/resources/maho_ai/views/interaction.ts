// Copyright 2026 Maho Browser. All rights reserved.

// Presentation helpers for agent interaction approval cards (plan row 8).
// Pure display logic over the normalized InteractionRequestRecord; the kernel
// InteractionBroker owns timeout/cancel/resume semantics.

import {interactionStateOutcome} from '../receipt-projection.js';
import type {InteractionRequestRecord} from '../types.js';

export function getInteractionTitle(record: InteractionRequestRecord): string {
  return record.kind === 'confirmation' ? 'Confirm this action' :
      'The agent has a question';
}

export function isInteractionActionable(record: InteractionRequestRecord):
    boolean {
  return record.state === 'pending';
}

// Status line shown once the request leaves the pending state. Terminal
// wording is single-sourced with the receipt outcome projection so card and
// receipt never disagree about what happened.
export function getInteractionStatusLabel(record: InteractionRequestRecord):
    string {
  if (record.state === 'pending') {
    return 'Waiting for your answer';
  }
  switch (interactionStateOutcome(record.state)) {
    case 'succeeded':
      return 'Answered';
    case 'denied':
      return 'Denied';
    case 'disconnected':
      return record.state === 'cancelled' ? 'Cancelled' :
          'Expired without an answer';
    default:
      return 'Waiting for your answer';
  }
}
