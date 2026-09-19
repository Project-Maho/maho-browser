import {
  ApprovalPolicy,
  ApprovalSensitivity,
  RuntimeEventKind,
  ToolCallStatus,
} from '../maho_ai.mojom-webui.js';
import type {
  ApprovalResultInfo,
  ArtifactInfo,
  ToolCallInfo,
  ToolResultInfo,
} from '../maho_ai.mojom-webui.js';
import {
  getApprovalPolicyLabel,
  getApprovalDecisionLabel,
  getApprovalSensitivityLabel,
  getApprovalStateLabel,
  getEventText,
  TimelineEntry,
} from '../types.js';
import {
  getCredentialErrorPresentation,
  type CredentialErrorPresentation,
} from './credential_error.js';
import {receiptFromToolOutput, receiptSummary} from '../receipt-projection.js';

type ConversationTone = 'neutral'|'accent'|'success'|'warning'|'danger';

interface ConversationChip {
  text: string;
  tone: ConversationTone;
}

export interface ConversationItem {
  approval?: {
    approvalId: string;
    decision?: string;
    approvalPolicy?: string;
    pageDerivedJustification?: boolean;
    reason?: string;
    sensitivity?: string;
    state?: string;
    status: 'approved'|'denied'|'pending';
    toolAction?: string;
    toolName?: string;
  };
  chips?: ConversationChip[];
  credentialError?: CredentialErrorPresentation;
  errorDetail?: string;
  kind?: 'activity'|'approval'|'artifact'|'thinking';
  artifact?: ArtifactInfo;
  key: string;
  markdown: boolean;
  note?: string;
  role: 'assistant'|'system'|'user';
  text: string;
  timestamp: number;
  tone?: ConversationTone;
  toolName?: string;
}

export function summarizeInlineText(text: string, maxLength = 64): string {
  const normalized = text.replace(/\s+/g, ' ').trim();
  if (normalized.length <= maxLength) {
    return normalized;
  }

  return `${normalized.slice(0, maxLength - 1).trimEnd()}…`;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

export function parseJsonRecord(text?: string): Record<string, unknown>|null {
  if (!text) {
    return null;
  }

  try {
    const parsed = JSON.parse(text) as unknown;
    return isRecord(parsed) ? parsed : null;
  } catch {
    return null;
  }
}

function getStringField(record: Record<string, unknown>|null, key: string): string|undefined {
  const value = record?.[key];
  return typeof value === 'string' && value.trim() ? value.trim() : undefined;
}

export function summarizePath(value?: string): string|undefined {
  if (!value) {
    return undefined;
  }

  const trimmed = value.trim();
  if (!trimmed) {
    return undefined;
  }

  const segments = trimmed.split('/').filter(Boolean);
  if (!segments.length) {
    return trimmed;
  }

  return segments.slice(-2).join('/');
}

function normalizeToolName(toolName?: string): string {
  if (!toolName) {
    return '';
  }

  return toolName.split('.').at(-1)?.toLowerCase() || toolName.toLowerCase();
}

function getToolDisplayName(toolName?: string): string|undefined {
  const trimmed = toolName?.trim();
  return trimmed ? trimmed : undefined;
}

export function getToolProgressLabel(toolName?: string): string {
  const normalizedName = normalizeToolName(toolName);
  if (normalizedName.includes('read')) {
    return 'Reading…';
  }
  if (normalizedName.includes('grep') || normalizedName.includes('search')) {
    return 'Searching…';
  }
  if (normalizedName.includes('glob')) {
    return 'Looking…';
  }
  if (normalizedName.includes('write') || normalizedName.includes('edit') ||
      normalizedName.includes('patch') || normalizedName.includes('replace')) {
    return 'Updating…';
  }
  if (normalizedName.includes('diagnostic') || normalizedName.includes('typecheck')) {
    return 'Checking…';
  }
  if (normalizedName.includes('fetch') || normalizedName.includes('crawl')) {
    return 'Fetching…';
  }
  if (normalizedName.includes('bash') || normalizedName.includes('command')) {
    return 'Running…';
  }
  return 'Working…';
}

export function getPendingThreadLabel(
    entries: TimelineEntry[], pending: boolean): string|null {
  if (!pending) {
    return null;
  }

  let latestToolName: string|undefined = undefined;
  for (let index = entries.length - 1; index >= 0; index--) {
    const event = entries[index]?.event;
    if (!event) {
      continue;
    }

    if (event.kind === RuntimeEventKind.kAssistantToken ||
        event.kind === RuntimeEventKind.kAssistantThinking ||
        event.kind === RuntimeEventKind.kTurnComplete) {
      return null;
    }

    if (!latestToolName && event.kind === RuntimeEventKind.kToolRequest) {
      latestToolName = event.toolCall?.toolName || undefined;
    }

    if (event.kind === RuntimeEventKind.kUserPrompt) {
      break;
    }
  }

  return getToolProgressLabel(latestToolName);
}

function humanizeToolRequest(toolCall?: ToolCallInfo|null): string {
  if (!toolCall) {
    return 'Working with a tool…';
  }

  const args = parseJsonRecord(toolCall.argumentsJson);
  const filePath = summarizePath(
      getStringField(args, 'filePath') || getStringField(args, 'path'));
  const pattern = getStringField(args, 'pattern');
  const query = getStringField(args, 'query');
  const libraryId = getStringField(args, 'libraryId');
  const description = getStringField(args, 'description') ||
      getStringField(args, 'tool_summary') ||
      getStringField(args, 'summary') ||
      getStringField(args, 'title');
  const normalizedName = normalizeToolName(toolCall.toolName);

  if (description) {
    return summarizeInlineText(description, 60);
  }

  if (normalizedName === 'read_current_page') {
    return 'Reading current page';
  }
  if (normalizedName === 'get_selected_text') {
    return 'Reading selected text';
  }
  if (normalizedName === 'get_active_tab') {
    return 'Checking active tab';
  }
  if (normalizedName === 'search_in_page') {
    const detail = query || pattern;
    return detail ? `Searching page for “${summarizeInlineText(detail, 40)}”` : 'Searching current page';
  }
  if (normalizedName === 'extract_structured_page_context') {
    return 'Extracting page structure';
  }

  if (normalizedName.includes('read')) {
    return filePath ? `Reading ${filePath}` : 'Reading a file';
  }
  if (normalizedName.includes('grep') || normalizedName.includes('search')) {
    const detail = pattern || query;
    return detail ? `Searching for “${summarizeInlineText(detail, 40)}”` : 'Searching the workspace';
  }
  if (normalizedName.includes('glob')) {
    return pattern ? `Looking for ${summarizeInlineText(pattern, 40)}` : 'Looking through files';
  }
  if (normalizedName.includes('diagnostic')) {
    return filePath ? `Checking ${filePath}` : 'Checking diagnostics';
  }
  if (normalizedName.includes('write') || normalizedName.includes('edit') ||
      normalizedName.includes('patch') || normalizedName.includes('replace')) {
    return filePath ? `Updating ${filePath}` : 'Updating files';
  }
  if (normalizedName.includes('fetch')) {
    return libraryId ? `Fetching ${libraryId}` : 'Fetching a page';
  }
  if (normalizedName.includes('bash') || normalizedName.includes('command')) {
    return description ? `Running ${summarizeInlineText(description, 40)}` : 'Running a command';
  }

  const display = getToolDisplayName(toolCall.toolName);
  if (display) {
    return `Calling ${display}`;
  }
  // No tool name reached the renderer. Never fabricate a reasoning-flavored
  // label ("Thinking") for a committed tool activity — that collides with the
  // real assistant-reasoning block. Fall back to an honest, neutral label.
  return 'Working with a tool…';
}

function countOutputLines(text?: string): number {
  if (!text) {
    return 0;
  }

  return text.split(/\r?\n/).map(line => line.trim()).filter(Boolean).length;
}

function humanizeToolResult(toolCall: ToolCallInfo|undefined, toolResult?: ToolResultInfo|null): string {
  if (!toolResult) {
    return 'Tool finished';
  }

  const normalizedName = normalizeToolName(toolCall?.toolName);
  const outputLineCount = countOutputLines(toolResult.output);
  const outputSummary = summarizeInlineText(toolResult.errorMessage || toolResult.output || '', 56);

  if (!toolResult.success) {
    if (normalizedName.includes('read')) {
      return outputSummary ? `Couldn’t read it — ${outputSummary}` : 'Couldn’t read it';
    }
    if (normalizedName.includes('grep') || normalizedName.includes('search')) {
      return outputSummary ? `Search failed — ${outputSummary}` : 'Search failed';
    }
    return outputSummary ? `That step failed — ${outputSummary}` : 'That step failed';
  }

  if (normalizedName.includes('glob')) {
    return outputLineCount ? `Found ${outputLineCount} matching ${outputLineCount === 1 ? 'file' : 'files'}` : 'Found matching files';
  }
  if (normalizedName.includes('grep') || normalizedName.includes('search')) {
    return outputLineCount ? `Found ${outputLineCount} matching ${outputLineCount === 1 ? 'result' : 'results'}` : 'Search finished';
  }
  if (normalizedName.includes('read')) {
    return 'Finished reading';
  }
  if (normalizedName.includes('diagnostic')) {
    return outputLineCount ? `Checked diagnostics in ${outputLineCount} ${outputLineCount === 1 ? 'place' : 'places'}` : 'Diagnostics look clean';
  }
  if (normalizedName.includes('write') || normalizedName.includes('edit') ||
      normalizedName.includes('patch') || normalizedName.includes('replace')) {
    return 'Updated the files';
  }
  if (normalizedName.includes('fetch')) {
    return 'Finished fetching';
  }
  if (normalizedName.includes('bash') || normalizedName.includes('command')) {
    return 'Command finished';
  }

  return 'Step finished';
}

function humanizeApprovalResult(result: ApprovalResultInfo): string {
  return result.approved ? 'Approved' : 'Declined';
}

function createEventKey(entry: TimelineEntry, index: number): string {
  const seq = entry.event.sequenceStart?.toString() || entry.event.sequence?.toString() || `t${entry.event.timestamp}`;
  return `${seq}-${entry.event.kind}-${index}`;
}

function getToolStatusChip(status?: ToolCallStatus): ConversationChip {
  switch (status) {
    case ToolCallStatus.kRunning:
      return {text: 'Running', tone: 'accent'};
    case ToolCallStatus.kCompleted:
      return {text: 'Completed', tone: 'success'};
    case ToolCallStatus.kFailed:
      return {text: 'Failed', tone: 'danger'};
    case ToolCallStatus.kCancelled:
      return {text: 'Cancelled', tone: 'warning'};
    case ToolCallStatus.kPending:
    default:
      return {text: 'Pending', tone: 'warning'};
  }
}

function buildApprovalChips(
    policy?: ApprovalPolicy|null,
    sensitivity?: ApprovalSensitivity|null,
    pageDerivedJustification?: boolean): ConversationChip[]|undefined {
  const chips: ConversationChip[] = [
    {text: getApprovalPolicyLabel(policy ?? ApprovalPolicy.kPrompt), tone: 'warning'},
    {text: getApprovalSensitivityLabel(sensitivity ?? ApprovalSensitivity.kSensitive), tone: 'danger'},
  ];
  if (pageDerivedJustification) {
    chips.push({text: 'Page-derived', tone: 'accent'});
  }
  return chips;
}

function getApprovalToolAction(toolName?: string): string|undefined {
  const display = getToolDisplayName(toolName);
  return display ? `Calling ${display}` : undefined;
}

function isConnectionErrorMessage(text: string): boolean {
  const lower = text.toLowerCase();
  // Never misclassify HTTP client errors (auth/quota/rate limit) as transport unreachable
  if (lower.includes('401') || lower.includes('unauthorized') ||
      lower.includes('403') || lower.includes('forbidden') ||
      lower.includes('429') || lower.includes('quota') ||
      lower.includes('rate limit')) {
    return false;
  }
  return lower.includes('connection refused') ||
         lower.includes('failed to connect') ||
         lower.includes('error sending request') ||
         lower.includes('connect error') ||
         lower.includes('network error') ||
         lower.includes('dns error') ||
         lower.includes('connection timed out') ||
         lower.includes('connect timeout') ||
         lower.includes('502 bad gateway') ||
         lower.includes('503 service unavailable') ||
         lower.includes('504 gateway timeout');
}

export function collectConversationItems(entries: TimelineEntry[]): ConversationItem[] {
  const items: ConversationItem[] = [];
  const toolCallsById = new Map<string, ToolCallInfo>();
  const toolItemIndexByCallId = new Map<string, number>();
  const approvalItemIndexById = new Map<string, number>();
  let replaceableAssistantIndex: number|null = null;
  let replaceableAssistantKey: string|null = null;
  let assistantText = '';
  let committedAssistantLength = 0;
  let replaceableThinkingIndex: number|null = null;
  let replaceableThinkingKey: string|null = null;
  let thinkingText = '';

  const clearAssistantState = () => {
    replaceableAssistantIndex = null;
    replaceableAssistantKey = null;
    assistantText = '';
    committedAssistantLength = 0;
    replaceableThinkingIndex = null;
    replaceableThinkingKey = null;
    thinkingText = '';
  };

  const splitAssistantState = () => {
    if (replaceableAssistantIndex !== null) {
      committedAssistantLength = assistantText.length;
      replaceableAssistantIndex = null;
      replaceableAssistantKey = null;
    }
    replaceableThinkingIndex = null;
    replaceableThinkingKey = null;
    thinkingText = '';
  };

  for (const [index, entry] of entries.entries()) {
    const {event} = entry;
    const credentialError = getCredentialErrorPresentation(event);
    const rawText = credentialError ? '' : getEventText(event);
    const text = rawText.trim();
    const key = createEventKey(entry, index);

      switch (event.kind) {
      case RuntimeEventKind.kUserPrompt:
        clearAssistantState();
        if (!text) {
          break;
        }
        items.push({
          key,
          markdown: false,
          role: 'user',
          text,
          timestamp: event.timestamp,
        });
        break;
      case RuntimeEventKind.kArtifactCreated:
        splitAssistantState();
        if (event.artifact) {
          items.push({
            artifact: event.artifact,
            kind: 'artifact',
            key,
            markdown: false,
            role: 'assistant',
            text: event.artifact.displayName,
            timestamp: event.timestamp,
          });
        }
        break;
      case RuntimeEventKind.kAssistantToken: {
        if (!text) {
          break;
        }
        replaceableThinkingIndex = null;
        replaceableThinkingKey = null;
        assistantText += text;
        const assistantSegmentText = assistantText.slice(committedAssistantLength);
        if (!assistantSegmentText) {
          break;
        }
        if (replaceableAssistantIndex === null) {
          replaceableAssistantIndex = items.length;
          replaceableAssistantKey = key;
          items.push({
            key: replaceableAssistantKey,
            markdown: true,
            role: 'assistant',
            text: assistantSegmentText,
            timestamp: event.timestamp,
          });
          break;
        }
        items[replaceableAssistantIndex] = {
          key: replaceableAssistantKey!,
          markdown: true,
          role: 'assistant',
          text: assistantSegmentText,
          timestamp: event.timestamp,
        };
        break;
      }
      case RuntimeEventKind.kAssistantThinking:
        if (!rawText.trim()) {
          break;
        }
        if (replaceableThinkingIndex === null) {
          replaceableThinkingIndex = items.length;
          replaceableThinkingKey = key;
          thinkingText = rawText;
          items.push({
            kind: 'thinking',
            key: replaceableThinkingKey,
            markdown: true,
            role: 'system',
            text: thinkingText,
            timestamp: event.timestamp,
          });
          break;
        }
        thinkingText += rawText;
        items[replaceableThinkingIndex] = {
          kind: 'thinking',
          key: replaceableThinkingKey!,
          markdown: true,
          role: 'system',
          text: thinkingText,
          timestamp: event.timestamp,
        };
        break;
      case RuntimeEventKind.kTurnComplete: {
        if (!text) {
          clearAssistantState();
          break;
        }
        assistantText = text;
        const finalAssistantSegmentText = assistantText.slice(committedAssistantLength);
        if (!finalAssistantSegmentText) {
          clearAssistantState();
          break;
        }
        if (replaceableAssistantIndex !== null) {
          items[replaceableAssistantIndex] = {
            key: replaceableAssistantKey ?? key,
            markdown: true,
            role: 'assistant',
            text: finalAssistantSegmentText,
            timestamp: event.timestamp,
          };
          clearAssistantState();
          break;
        }
        items.push({
          key,
          markdown: true,
          role: 'assistant',
          text: finalAssistantSegmentText,
          timestamp: event.timestamp,
        });
        clearAssistantState();
        break;
      }
      case RuntimeEventKind.kToolRequest:
        splitAssistantState();
        if (event.toolCall?.callId) {
          toolCallsById.set(event.toolCall.callId, event.toolCall);
          toolItemIndexByCallId.set(event.toolCall.callId, items.length);
        }
        items.push({
          chips: event.toolCall?.status === ToolCallStatus.kFailed ?
              [getToolStatusChip(event.toolCall.status)] :
              undefined,
          kind: 'activity',
          key,
          markdown: false,
          role: 'system',
          text: humanizeToolRequest(event.toolCall),
          timestamp: event.timestamp,
          tone: 'accent',
          toolName: getToolDisplayName(event.toolCall?.toolName),
        });
        break;
      case RuntimeEventKind.kToolResult: {
        splitAssistantState();
        const toolCall = event.toolResult?.callId ?
            toolCallsById.get(event.toolResult.callId) :
            undefined;
        const existingToolIndex = event.toolResult?.callId ?
            toolItemIndexByCallId.get(event.toolResult.callId) :
            undefined;
        const success = event.toolResult?.success === true;
        const requestText = toolCall ? humanizeToolRequest(toolCall) : undefined;
        const failureText = humanizeToolResult(toolCall, event.toolResult);
        const resultTone = success ? 'success' : 'danger';
        const receipt = receiptFromToolOutput(event.toolResult?.output);
        const resultNote = receipt ? receiptSummary(receipt) : !success ?
            summarizeInlineText(event.toolResult?.errorMessage || event.toolResult?.output || '', 72) :
            undefined;
        if (existingToolIndex !== undefined && items[existingToolIndex]) {
          const existingItem = items[existingToolIndex]!;
          items[existingToolIndex] = {
            ...existingItem,
            chips: [getToolStatusChip(success ? ToolCallStatus.kCompleted : ToolCallStatus.kFailed)],
            text: success ? existingItem.text : failureText,
            note: resultNote,
            tone: resultTone,
            timestamp: event.timestamp,
            toolName: existingItem.toolName || getToolDisplayName(toolCall?.toolName),
          };
        } else {
          items.push({
            kind: 'activity',
            key,
            markdown: false,
            note: resultNote,
            role: 'system',
            text: success ? (requestText || failureText) : failureText,
            timestamp: event.timestamp,
            tone: resultTone,
            toolName: getToolDisplayName(toolCall?.toolName),
          });
        }
        break;
      }
      case RuntimeEventKind.kApprovalRequest: {
        splitAssistantState();
        const approvalIndex = items.length;
        if (event.approvalRequest?.relatedToolCall?.callId) {
          toolCallsById.set(
              event.approvalRequest.relatedToolCall.callId,
              event.approvalRequest.relatedToolCall);
        }
        items.push({
          approval: event.approvalRequest?.approvalId ? {
             approvalId: event.approvalRequest.approvalId,
              status: 'pending',
              approvalPolicy: getApprovalPolicyLabel(
                  event.approvalRequest.approvalPolicy ?? ApprovalPolicy.kPrompt),
              pageDerivedJustification:
                  event.approvalRequest.pageDerivedJustification,
              sensitivity: getApprovalSensitivityLabel(
                  event.approvalRequest.sensitivity ??
                  ApprovalSensitivity.kSensitive),
              state: getApprovalStateLabel(event.approvalRequest.state),
              toolAction: getApprovalToolAction(
                  event.approvalRequest.relatedToolCall?.toolName),
              toolName: getToolDisplayName(event.approvalRequest.relatedToolCall?.toolName),
            } : undefined,
          chips: event.approvalRequest ? buildApprovalChips(
              event.approvalRequest.approvalPolicy,
              event.approvalRequest.sensitivity,
              event.approvalRequest.pageDerivedJustification) : undefined,
          kind: 'approval',
          key,
          markdown: false,
          role: 'system',
          text: event.approvalRequest?.description || 'The runtime is waiting for approval.',
          timestamp: event.timestamp,
          tone: 'warning',
        });
        if (event.approvalRequest?.approvalId) {
          approvalItemIndexById.set(event.approvalRequest.approvalId, approvalIndex);
        }
        break;
      }
      case RuntimeEventKind.kApprovalResult:
        splitAssistantState();
        if (event.approvalResult?.approvalId) {
          const existingApprovalIndex =
              approvalItemIndexById.get(event.approvalResult.approvalId);
          if (existingApprovalIndex !== undefined) {
            const approvalItem = items[existingApprovalIndex];
            if (approvalItem) {
              items[existingApprovalIndex] = {
                ...approvalItem,
                approval: {
                  ...(approvalItem.approval || {}),
                  approvalId: event.approvalResult.approvalId,
                  approvalPolicy: getApprovalPolicyLabel(
                      event.approvalResult.approvalPolicy),
                  decision: getApprovalDecisionLabel(event.approvalResult.decision),
                  pageDerivedJustification:
                      event.approvalResult.pageDerivedJustification,
                  reason: event.approvalResult.reason || undefined,
                  sensitivity: getApprovalSensitivityLabel(
                      event.approvalResult.sensitivity),
                  state: getApprovalStateLabel(event.approvalResult.state),
                  status: event.approvalResult.approved ? 'approved' : 'denied',
                },
                chips: buildApprovalChips(
                    event.approvalResult.approvalPolicy,
                    event.approvalResult.sensitivity,
                    event.approvalResult.pageDerivedJustification),
                note: event.approvalResult.reason || approvalItem.note,
                tone: event.approvalResult.approved ? 'success' : 'danger',
              };
              break;
            }
          }
        }
        items.push({
          kind: 'activity',
          key,
          markdown: false,
          note: event.approvalResult?.reason || undefined,
          role: 'system',
          text: event.approvalResult ?
              `${humanizeApprovalResult(event.approvalResult)} a request` :
              'Approval response recorded.',
          timestamp: event.timestamp,
          tone: event.approvalResult?.approved ? 'success' : 'danger',
        });
        break;
      case RuntimeEventKind.kError:
        splitAssistantState();
        if (credentialError) {
          items.push({
            credentialError,
            errorDetail: text || undefined,
            key,
            markdown: false,
            role: 'system',
            text: credentialError.title,
            timestamp: event.timestamp,
            tone: 'danger',
          });
          break;
        }
        {
          const isConn = text ? isConnectionErrorMessage(text) : false;
          items.push({
            errorDetail: text || undefined,
            key,
            markdown: false,
            role: 'system',
            text: isConn ?
                'Connection error — Unable to reach AI proxy or endpoint.' :
                (text ? 'Something went wrong' : 'The runtime reported an error.'),
            timestamp: event.timestamp,
            tone: 'danger',
          });
        }
        break;
      case RuntimeEventKind.kSessionStatus:
      case RuntimeEventKind.kConnectionStateChanged:
        break;
    }
  }

  return items;
}
