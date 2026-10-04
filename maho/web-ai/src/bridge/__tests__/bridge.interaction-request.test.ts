/**
 * Bridge contract tests for the agent interaction_request event (FFI kind 8).
 *
 * The native kernel emits `{ "type": "interaction_request", "data": <object> }`
 * envelopes; these tests pin the normalization into the typed AgentEvent
 * variant and the JSON round-trip of the parsed result. Payload shapes covered:
 *   - FFI live wire:  { request_id, kind: "question"|"confirmation", args: "<json>" }
 *   - Kernel serde:   { id, kind: { kind, question|effect_description, options }, state }
 *   - Flat journal:   { id, question|effect_description, options }
 * Unsupported or malformed payloads must be safely discarded (null).
 */
import { describe, expect, it } from 'vitest';
import { normalizeAgentEvent } from '../../hooks/use-agent-session';
import type { AgentEvent } from '../types';

// Compile-time: the interaction_request variant must exist on the union with
// the typed payload (id, interactionKind, question, options, artifactRef).
type InteractionEvent = Extract<AgentEvent, { kind: 'interaction_request' }>;
const _variantCheck: InteractionEvent extends {
  id: string;
  interactionKind: 'question' | 'confirmation';
  question: string;
  options: { id: string; label: string; description?: string }[];
} ? true
  : never = true;
void _variantCheck;

describe('bridge interaction_request event (FFI kind 8)', () => {
  it('parses the FFI live wire shape (request_id + args JSON string)', () => {
    const wire = JSON.stringify({
      type: 'interaction_request',
      data: {
        request_id: 'interaction-req-3',
        kind: 'question',
        args: JSON.stringify({
          question: 'Which deployment target should we use?',
          options: [
            { id: 'opt_prod', label: 'Production' },
            { id: 'opt_staging', label: 'Staging', description: 'Staging cluster' },
          ],
        }),
      },
    });

    expect(normalizeAgentEvent(wire)).toEqual({
      kind: 'interaction_request',
      id: 'interaction-req-3',
      interactionKind: 'question',
      question: 'Which deployment target should we use?',
      options: [
        { id: 'opt_prod', label: 'Production' },
        { id: 'opt_staging', label: 'Staging', description: 'Staging cluster' },
      ],
    });
  });

  it('parses the kernel serde InteractionRequest shape with typed kind dict', () => {
    const wire = JSON.stringify({
      type: 'interaction_request',
      data: {
        id: 'interaction-req-7',
        kind: {
          kind: 'confirmation',
          effect_description: 'Drop database production_db_v2',
        },
        state: 'pending',
      },
    });

    expect(normalizeAgentEvent(wire)).toEqual({
      kind: 'interaction_request',
      id: 'interaction-req-7',
      interactionKind: 'confirmation',
      question: 'Drop database production_db_v2',
      options: [],
      state: 'pending',
    });
  });

  it('parses the flat shape and preserves a review artifact reference', () => {
    const wire = JSON.stringify({
      type: 'interaction_request',
      data: {
        id: 'interaction-req-9',
        kind: 'confirmation',
        effect_description: 'Type credentials into the payment form',
        artifact_ref: 'artifact-review-1',
      },
    });

    expect(normalizeAgentEvent(wire)).toEqual({
      kind: 'interaction_request',
      id: 'interaction-req-9',
      interactionKind: 'confirmation',
      question: 'Type credentials into the payment form',
      options: [],
      artifactRef: 'artifact-review-1',
    });
  });

  it('parsed events survive a JSON serialize round-trip losslessly', () => {
    const wire = JSON.stringify({
      type: 'interaction_request',
      data: {
        request_id: 'interaction-req-3',
        kind: 'question',
        args: '{"question":"Pick one","options":[{"id":"a","label":"A"}]}',
      },
    });
    const parsed = normalizeAgentEvent(wire);
    expect(parsed).not.toBeNull();
    expect(JSON.parse(JSON.stringify(parsed))).toEqual(parsed);
  });

  it('safely discards unsupported or malformed payloads (returns null)', () => {
    expect(normalizeAgentEvent('{"type":"interaction_request","data":"raw string"}')).toBeNull();
    expect(normalizeAgentEvent('{"type":"interaction_request","data":{}}')).toBeNull();
    expect(
      normalizeAgentEvent('{"type":"interaction_request","data":{"kind":"question"}}'),
    ).toBeNull();
    expect(normalizeAgentEvent('{"type":"interaction_request"}')).toBeNull();
  });
});
