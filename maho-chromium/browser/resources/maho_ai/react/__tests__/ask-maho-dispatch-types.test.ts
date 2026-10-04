import {expectTypeOf, it} from 'vitest';
import {
  ChatIntent,
} from '../../maho_ai.mojom-webui.js';
import type {
  ApprovalDecision,
  ApprovalPolicy,
  ApprovalRequestInfo,
  ApprovalResultInfo,
  ApprovalSensitivity,
  ApprovalState,
  AskMahoContextIntent,
  AskMahoDispatch,
  AskMahoSource,
  ContextAttachment,
  InteractionMode,
  PageHandlerRemote,
  RuntimeEvent,
} from '../../maho_ai.mojom-webui.js';

it('exposes exact ChatIntent enum members declared by Mojo', () => {
  expectTypeOf<keyof typeof ChatIntent>()
      .toEqualTypeOf<'kFreeform' | 'kSummarizeCurrentPage' | 'kQuizCurrentPage'>();
});

it('enforces six-argument submitPrompt contract on PageHandlerRemote', () => {
  expectTypeOf<PageHandlerRemote['submitPrompt']>().toEqualTypeOf<(
      sessionId: string,
      prompt: string,
      attachBrowserContext: boolean,
      mode: InteractionMode,
      attachments: ContextAttachment[] | null,
      chatIntent: ChatIntent) => Promise<{accepted: boolean}>>();
});

it('exposes only the Ask Maho source declared by Mojo', () => {
  expectTypeOf<keyof typeof AskMahoSource>()
      .toEqualTypeOf<'kCommandPalette'>();
});

it('exposes only the Ask Maho context intents declared by Mojo', () => {
  expectTypeOf<keyof typeof AskMahoContextIntent>()
      .toEqualTypeOf<'kNone' | 'kCurrentPage'>();
});

it('matches the Mojo Ask Maho dispatch field contract', () => {
  expectTypeOf<AskMahoDispatch>().toEqualTypeOf<{
    requestId: string;
    query: string;
    source: AskMahoSource;
    submit: boolean;
    mode: InteractionMode;
    contextIntent: AskMahoContextIntent;
    targetSessionId: string | null;
  }>();
});

it('requires immutable runtime session correlation at the renderer boundary', () => {
  expectTypeOf<RuntimeEvent['sessionId']>().toEqualTypeOf<string>();
});

it('exposes typed approval metadata enums declared by Mojo', () => {
  expectTypeOf<keyof typeof ApprovalPolicy>()
      .toEqualTypeOf<'kPrompt' | 'kAllowAll' | 'kAllowMcp' | 'kDenySensitive' | 'kDenyAll'>();
  expectTypeOf<keyof typeof ApprovalSensitivity>()
      .toEqualTypeOf<'kReadOnly' | 'kSensitive'>();
  expectTypeOf<keyof typeof ApprovalState>()
      .toEqualTypeOf<'kPending' | 'kApproved' | 'kDenied' | 'kCancelled'>();
  expectTypeOf<keyof typeof ApprovalDecision>()
      .toEqualTypeOf<'kNone' | 'kAllow' | 'kAllowOnce' | 'kDeny'>();
});

it('matches the Mojo approval payload field contract', () => {
  expectTypeOf<ApprovalRequestInfo>().toEqualTypeOf<{
    approvalId: string;
    description: string;
    approvalPolicy: ApprovalPolicy;
    sensitivity: ApprovalSensitivity;
    state: ApprovalState;
    pageDerivedJustification: boolean;
    relatedToolCall?: RuntimeEvent['toolCall'];
  }>();
  expectTypeOf<ApprovalResultInfo>().toEqualTypeOf<{
    approvalId: string;
    approved: boolean;
    reason?: string | null;
    approvalPolicy: ApprovalPolicy;
    sensitivity: ApprovalSensitivity;
    state: ApprovalState;
    decision: ApprovalDecision;
    pageDerivedJustification: boolean;
  }>();
});
