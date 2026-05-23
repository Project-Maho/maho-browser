import {
  RuntimeEventKind,
  ToolCallStatus,
} from '../maho_ai.mojom-webui.js';
import type {
  ApprovalResultInfo,
  ToolCallInfo,
  ToolResultInfo,
} from '../maho_ai.mojom-webui.js';
import {
  getEventText,
  TimelineEntry,
} from '../types.js';

type ConversationTone = 'neutral'|'accent'|'success'|'warning'|'danger';

interface ConversationChip {
  text: string;
  tone: ConversationTone;
}

export interface ConversationItem {
  approval?: {
    approvalId: string;
    reason?: string;
    status: 'approved'|'denied'|'pending';
  };
  chips?: ConversationChip[];
  kind?: 'activity'|'approval'|'thinking';
  key: string;
  markdown: boolean;
  note?: string;
  role: 'assistant'|'system'|'user';
  text: string;
  timestamp: number;
  tone?: ConversationTone;
}

function summarizeInlineText(text: string, maxLength = 64): string {
  const normalized = text.replace(/\s+/g, ' ').trim();
  if (normalized.length <= maxLength) {
    return normalized;
  }

  return `${normalized.slice(0, maxLength - 1).trimEnd()}…`;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

function parseJsonRecord(text?: string): Record<string, unknown>|null {
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

function summarizePath(value?: string): string|undefined {
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
  return 'Thinking…';
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
  const description = getStringField(args, 'description');
  const normalizedName = normalizeToolName(toolCall.toolName);

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

  return getToolProgressLabel(toolCall.toolName).replace(/…$/, '');
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
  return `${entry.event.sequence?.toString() || `t${entry.event.timestamp}`}-${entry.event.kind}-${index}`;
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

export function collectConversationItems(entries: TimelineEntry[]): ConversationItem[] {
  const items: ConversationItem[] = [];
  const toolCallsById = new Map<string, ToolCallInfo>();
  const toolItemIndexByCallId = new Map<string, number>();
  const approvalItemIndexById = new Map<string, number>();
  let replaceableAssistantIndex: number|null = null;
  let assistantText = '';
  let committedAssistantLength = 0;
  let replaceableThinkingIndex: number|null = null;
  let thinkingText = '';

  const clearAssistantState = () => {
    replaceableAssistantIndex = null;
    assistantText = '';
    committedAssistantLength = 0;
    replaceableThinkingIndex = null;
    thinkingText = '';
  };

  const splitAssistantState = () => {
    if (replaceableAssistantIndex === null) {
      return;
    }
    committedAssistantLength = assistantText.length;
    replaceableAssistantIndex = null;
  };

  for (const [index, entry] of entries.entries()) {
    const {event} = entry;
    const text = getEventText(event).trim();
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
      case RuntimeEventKind.kAssistantToken:
        if (!text) {
          break;
        }
        replaceableThinkingIndex = null;
        assistantText += text;
        const assistantSegmentText = assistantText.slice(committedAssistantLength);
        if (!assistantSegmentText) {
          break;
        }
        if (replaceableAssistantIndex === null) {
          replaceableAssistantIndex = items.length;
          items.push({
            key,
            markdown: true,
            role: 'assistant',
            text: assistantSegmentText,
            timestamp: event.timestamp,
          });
          break;
        }
        items[replaceableAssistantIndex] = {
          key,
          markdown: true,
          role: 'assistant',
          text: assistantSegmentText,
          timestamp: event.timestamp,
        };
        break;
      case RuntimeEventKind.kAssistantThinking:
        if (!text) {
          break;
        }
        if (replaceableThinkingIndex === null) {
          replaceableThinkingIndex = items.length;
          thinkingText = text;
          items.push({
            kind: 'thinking',
            key,
            markdown: false,
            role: 'system',
            text: thinkingText,
            timestamp: event.timestamp,
          });
          break;
        }
        thinkingText += text;
        items[replaceableThinkingIndex] = {
          kind: 'thinking',
          key,
          markdown: false,
          role: 'system',
          text: thinkingText,
          timestamp: event.timestamp,
        };
        break;
      case RuntimeEventKind.kTurnComplete:
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
            key,
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
        const resultText = humanizeToolResult(toolCall, event.toolResult);
        const resultTone = event.toolResult?.success ? 'success' : 'danger';
        const resultNote = !event.toolResult?.success ?
            summarizeInlineText(event.toolResult?.errorMessage || event.toolResult?.output || '', 72) :
            (event.toolResult?.output ? summarizeInlineText(event.toolResult.output, 120) : undefined);
        if (existingToolIndex !== undefined && items[existingToolIndex]) {
          items[existingToolIndex] = {
            ...items[existingToolIndex],
            chips: [getToolStatusChip(event.toolResult?.success ? ToolCallStatus.kCompleted : ToolCallStatus.kFailed)],
            text: resultText,
            note: resultNote,
            tone: resultTone,
            timestamp: event.timestamp,
          };
        } else {
          items.push({
            kind: 'activity',
            key,
            markdown: false,
            note: resultNote,
            role: 'system',
            text: resultText,
            timestamp: event.timestamp,
            tone: resultTone,
          });
        }
        break;
      }
      case RuntimeEventKind.kApprovalRequest:
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
          } : undefined,
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
                  approvalId: event.approvalResult.approvalId,
                  reason: event.approvalResult.reason || undefined,
                  status: event.approvalResult.approved ? 'approved' : 'denied',
                },
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
        items.push({
          kind: 'activity',
          key,
          markdown: false,
          role: 'system',
          text: text ? `Something went wrong — ${summarizeInlineText(text, 68)}` :
              'The runtime reported an error.',
          timestamp: event.timestamp,
          tone: 'danger',
        });
        break;
      case RuntimeEventKind.kSessionStatus:
      case RuntimeEventKind.kConnectionStateChanged:
        break;
    }
  }

  return items;
}
