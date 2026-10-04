// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {
  ActionTranslation,
  AdapterDescriptor,
  CaretContext,
  DocsSelectionSnapshot,
  DocumentSurfaceInfo,
  ExtractRequest,
  ExtractResult,
  PageSignals,
  PageSurfaceAdapter,
  ResolveSelectionResult,
  ResolveTargetResult,
  ViewportRect,
  VisualTargetRef,
} from './types';

export class GoogleDocsAdapter implements PageSurfaceAdapter {
  public static readonly REQUIRED_ORIGIN = 'https://docs.google.com';
  private static readonly ADAPTER_ID = 'google_docs_canvas_adapter';
  private static readonly VERSION = '1.0.0';
  private static readonly MAX_OUTPUT_BYTES = 65536;
  private static readonly DEFAULT_REF_TTL_MS = 30000; // 30 seconds

  private docToken: string = '';
  private currentTabId: number = 0;
  private currentFrameId: string = 'main';

  constructor(tabId: number = 1, frameId: string = 'main') {
    this.currentTabId = tabId;
    this.currentFrameId = frameId;
    this.docToken = this.generateDocumentToken();
  }

  public descriptor(): AdapterDescriptor {
    return {
      adapter_id: GoogleDocsAdapter.ADAPTER_ID,
      version: GoogleDocsAdapter.VERSION,
      exact_origin_patterns: [
        'https://docs.google.com/document/*',
        'https://docs.google.com/spreadsheets/*',
        'https://docs.google.com/presentation/*',
      ],
      execution_world: 'ISOLATED',
      supported_operations: [
        'GetDocumentSurfaceInfo',
        'GetVisibleText',
        'GetSelectionSnapshot',
        'GetCaretContext',
        'ResolveVisibleRangeRects',
        'TranslateAction',
      ],
      required_browser_capabilities: ['dom_read', 'input_synthesis'],
      output_schema_version: '1.0',
      redaction_policy: {
        maskCredentials: true,
        maskTokens: true,
      },
      max_output_bytes: GoogleDocsAdapter.MAX_OUTPUT_BYTES,
    };
  }

  /**
   * Strictly validates that the origin or URL string belongs to "https://docs.google.com".
   */
  public static isAuthorizedOrigin(origin: string): boolean {
    if (!origin || typeof origin !== 'string') return false;
    try {
      const parsed = new URL(origin);
      return parsed.origin === GoogleDocsAdapter.REQUIRED_ORIGIN;
    } catch {
      return (
        origin === GoogleDocsAdapter.REQUIRED_ORIGIN ||
        origin.startsWith(`${GoogleDocsAdapter.REQUIRED_ORIGIN}/`)
      );
    }
  }

  /**
   * Enforces strict origin checking on message/custom events.
   */
  public validateEventOrigin(event: MessageEvent | { origin: string }): boolean {
    if (!event || typeof event.origin !== 'string') {
      return false;
    }
    return event.origin === GoogleDocsAdapter.REQUIRED_ORIGIN;
  }

  /**
   * Sanitizes custom event payloads against unauthorized origins.
   * Returns null (fails closed) if origin is not authorized.
   */
  public sanitizeCustomEventPayload<T = unknown>(
    payload: T,
    origin?: string
  ): T | null {
    const targetOrigin = origin || (typeof window !== 'undefined' ? window.location?.origin : '');
    if (!GoogleDocsAdapter.isAuthorizedOrigin(targetOrigin)) {
      return null;
    }

    if (payload === null || payload === undefined) {
      return payload;
    }

    if (typeof payload !== 'object') {
      return payload;
    }

    try {
      return this.deepSanitizePayload(payload);
    } catch {
      return null;
    }
  }

  /**
   * Validates event origin and sanitizes custom event / message data.
   */
  public handleCustomEvent(
    event: CustomEvent | MessageEvent | { origin?: string; detail?: unknown; data?: unknown }
  ): unknown | null {
    const eventOrigin =
      (event as any).origin || (typeof window !== 'undefined' ? window.location?.origin : '');
    if (!this.validateEventOrigin({ origin: eventOrigin })) {
      return null;
    }

    const payload = 'detail' in event ? (event as CustomEvent).detail : (event as MessageEvent).data;
    return this.sanitizeCustomEventPayload(payload, eventOrigin);
  }

  private deepSanitizePayload<T>(obj: T, depth = 0): T {
    if (depth > 10 || obj === null || typeof obj !== 'object') {
      return obj;
    }

    if (Array.isArray(obj)) {
      return obj.map((item) => this.deepSanitizePayload(item, depth + 1)) as unknown as T;
    }

    const cleanObj: Record<string, unknown> = {};
    for (const key of Object.keys(obj as Record<string, unknown>)) {
      if (key === '__proto__' || key === 'constructor' || key === 'prototype') {
        continue;
      }
      const val = (obj as Record<string, unknown>)[key];
      if (typeof val === 'function' || typeof val === 'symbol') {
        continue;
      }
      cleanObj[key] = this.deepSanitizePayload(val, depth + 1);
    }
    return cleanObj as T;
  }

  public Matches(origin: string, signals?: PageSignals): boolean {
    const targetOrigin = origin || signals?.origin || (typeof window !== 'undefined' ? window.location?.origin : '');
    if (!targetOrigin || !GoogleDocsAdapter.isAuthorizedOrigin(targetOrigin)) {
      return false; // Unauthorized origin - fail closed immediately
    }

    const isDocsPath =
      targetOrigin.startsWith('https://docs.google.com/document/') ||
      targetOrigin.startsWith('https://docs.google.com/spreadsheets/') ||
      targetOrigin.startsWith('https://docs.google.com/presentation/') ||
      targetOrigin === 'https://docs.google.com' ||
      targetOrigin.startsWith('https://docs.google.com/');

    if (isDocsPath) return true;

    if (signals?.hasGoogleDocsDom || signals?.isCanvasEditor) {
      return true;
    }

    // DOM-level heuristics when running inside document on docs.google.com
    if (typeof document !== 'undefined') {
      const hasKixEditor =
        document.querySelector('.kix-appview-editor') !== null ||
        document.querySelector('.docs-texteventtarget-iframe') !== null ||
        document.querySelector('canvas.kix-canvas-tile-content') !== null ||
        document.querySelector('.kix-lineview') !== null;
      if (hasKixEditor) return true;
    }

    return false;
  }

  public Extract(request: ExtractRequest): ExtractResult {
    switch (request.operation) {
      case 'GetDocumentSurfaceInfo': {
        const info = this.GetDocumentSurfaceInfo();
        return {
          success: true,
          source_adapter: GoogleDocsAdapter.ADAPTER_ID,
          data: info,
        };
      }
      case 'GetVisibleText': {
        const query = request.params?.query as string | undefined;
        const textInfo = this.GetVisibleText(query);
        return {
          success: true,
          source_adapter: GoogleDocsAdapter.ADAPTER_ID,
          data: textInfo,
        };
      }
      case 'GetSelectionSnapshot': {
        const snapshot = this.GetSelectionSnapshot();
        return {
          success: true,
          source_adapter: GoogleDocsAdapter.ADAPTER_ID,
          data: snapshot,
        };
      }
      case 'GetCaretContext': {
        const caret = this.GetCaretContext();
        return {
          success: true,
          source_adapter: GoogleDocsAdapter.ADAPTER_ID,
          data: caret,
        };
      }
      case 'ResolveVisibleRangeRects': {
        const query = (request.params?.query as string) || '';
        const rects = this.ResolveVisibleRangeRects(query);
        return {
          success: true,
          source_adapter: GoogleDocsAdapter.ADAPTER_ID,
          data: { rects },
        };
      }
      case 'TranslateAction': {
        const action = request.params?.action as ActionTranslation | undefined;
        if (!action) {
          return {
            success: false,
            source_adapter: GoogleDocsAdapter.ADAPTER_ID,
            error: 'Missing action parameter in TranslateAction request',
          };
        }
        const translated = this.TranslateAction(action);
        return {
          success: translated !== null,
          source_adapter: GoogleDocsAdapter.ADAPTER_ID,
          data: translated,
          error: translated === null ? 'Failed to translate action' : undefined,
        };
      }
      default:
        return {
          success: false,
          source_adapter: GoogleDocsAdapter.ADAPTER_ID,
          error: `Unsupported operation: ${request.operation}`,
        };
    }
  }

  public ResolveSelection(): ResolveSelectionResult {
    const snapshot = this.GetSelectionSnapshot();
    const hasSelection = snapshot.selected_text.length > 0 || snapshot.viewport_rects.length > 0;
    return {
      has_selection: hasSelection,
      snapshot,
    };
  }

  public ResolveTarget(query: string | Record<string, unknown>): ResolveTargetResult {
    const searchText = typeof query === 'string' ? query : (query.query as string) || '';
    if (!searchText) {
      return { found: false, error: 'Query string empty' };
    }

    const rects = this.ResolveVisibleRangeRects(searchText);
    if (rects.length === 0) {
      return { found: false, error: 'No matching text region found on visible canvas' };
    }

    const primaryRect = rects[0];
    const visualRef = this.createVisualTargetRef(
      primaryRect,
      `docs_match:${searchText.slice(0, 32)}`
    );

    return {
      found: true,
      target_ref: visualRef,
      viewport_rect: primaryRect,
    };
  }

  public TranslateAction(action: ActionTranslation): ActionTranslation | null {
    switch (action.kind) {
      case 'focus_editor': {
        const editorTarget = this.locateEditorTarget();
        return {
          kind: 'focus_editor',
          target_point: editorTarget,
          revalidation_required: true,
        };
      }
      case 'click_target': {
        if (action.target_ref) {
          const valid = this.validateVisualTargetRef(action.target_ref);
          if (!valid) {
            return null; // fail closed on stale or expired reference
          }
          const rect = action.target_ref.viewport_rect;
          return {
            kind: 'click_target',
            target_point: {
              x: Math.round(rect.x + rect.width / 2),
              y: Math.round(rect.y + rect.height / 2),
            },
            target_ref: action.target_ref,
            revalidation_required: true,
          };
        }
        if (action.target_point) {
          return {
            kind: 'click_target',
            target_point: action.target_point,
            revalidation_required: true,
          };
        }
        return null;
      }
      case 'type_into_caret': {
        return {
          kind: 'type_into_caret',
          text_to_type: action.text_to_type || '',
          revalidation_required: true,
        };
      }
      case 'key_chord': {
        return {
          kind: 'key_chord',
          key: action.key,
          modifiers: action.modifiers || [],
          revalidation_required: true,
        };
      }
      case 'select_range': {
        if (action.target_ref) {
          const valid = this.validateVisualTargetRef(action.target_ref);
          if (!valid) return null;
        }
        return {
          kind: 'select_range',
          target_ref: action.target_ref,
          target_point: action.target_point,
          revalidation_required: true,
        };
      }
      default:
        return null;
    }
  }

  // ---- Concrete Google Docs Canvas & DOM extraction helpers ----

  public GetDocumentSurfaceInfo(): DocumentSurfaceInfo {
    if (typeof document === 'undefined') {
      return {
        surface_type: 'dom_fallback',
        canvas_count: 0,
        viewport_width: 1024,
        viewport_height: 768,
        zoom_level: 1.0,
        is_focused: false,
        editable: true,
      };
    }

    const canvases = document.querySelectorAll('canvas');
    const hasKix = document.querySelector('.kix-appview-editor') !== null;
    const isFocused =
      document.activeElement === document.querySelector('.docs-texteventtarget-iframe') ||
      document.activeElement?.classList.contains('docs-texteventtarget-iframe') ||
      document.activeElement?.tagName === 'IFRAME';

    return {
      surface_type: hasKix ? 'canvas_kix' : canvases.length > 0 ? 'canvas_editor' : 'dom_fallback',
      canvas_count: canvases.length,
      viewport_width: window.innerWidth || document.documentElement.clientWidth || 1024,
      viewport_height: window.innerHeight || document.documentElement.clientHeight || 768,
      zoom_level: window.devicePixelRatio || 1.0,
      is_focused: Boolean(isFocused),
      editable: !document.querySelector('.docs-material-gm-disabled'),
    };
  }

  public GetVisibleText(queryFilter?: string): {
    text: string;
    line_count: number;
    pages: number;
  } {
    if (typeof document === 'undefined') {
      return { text: '', line_count: 0, pages: 1 };
    }

    const lines: string[] = [];
    const lineElements = document.querySelectorAll(
      '.kix-lineview-text-block, .kix-word-html, [role="textbox"], .kix-paragraphrenderer'
    );

    if (lineElements.length > 0) {
      lineElements.forEach((el) => {
        const text = el.textContent?.trim();
        if (text) {
          if (!queryFilter || text.toLowerCase().includes(queryFilter.toLowerCase())) {
            lines.push(text);
          }
        }
      });
    } else {
      // Fallback to text content in editor container
      const editor = document.querySelector('.kix-appview-editor') || document.body;
      const text = editor.textContent || '';
      const splitLines = text
        .split('\n')
        .map((l) => l.trim())
        .filter(Boolean);
      for (const line of splitLines) {
        if (!queryFilter || line.toLowerCase().includes(queryFilter.toLowerCase())) {
          lines.push(line);
        }
      }
    }

    const pageCount = Math.max(
      1,
      document.querySelectorAll('.kix-page, .kix-page-paginated').length
    );

    return {
      text: lines.join('\n'),
      line_count: lines.length,
      pages: pageCount,
    };
  }

  public GetSelectionSnapshot(): DocsSelectionSnapshot {
    const warnings: string[] = [];
    let selectedText = '';
    const viewportRects: ViewportRect[] = [];

    if (typeof document !== 'undefined') {
      // 1. Check native DOM selection
      const sel = window.getSelection();
      if (sel && !sel.isCollapsed && sel.toString().trim()) {
        selectedText = sel.toString().trim();
        for (let i = 0; i < sel.rangeCount; i++) {
          const r = sel.getRangeAt(i);
          const rect = r.getBoundingClientRect();
          if (rect.width > 0 && rect.height > 0) {
            viewportRects.push(this.domRectToViewport(rect));
          }
        }
      }

      // 2. Check Docs selection overlays (.kix-selection-overlay)
      const overlayNodes = document.querySelectorAll(
        '.kix-selection-overlay, .docs-selection-overlay'
      );
      overlayNodes.forEach((node) => {
        const rect = node.getBoundingClientRect();
        if (rect.width > 0 && rect.height > 0) {
          viewportRects.push(this.domRectToViewport(rect));
        }
      });

      // 3. Fallback to hidden iframe text selection if available
      if (!selectedText) {
        const iframe = document.querySelector<HTMLIFrameElement>('iframe.docs-texteventtarget-iframe');
        if (iframe && iframe.contentWindow) {
          try {
            const iframeSel = iframe.contentWindow.getSelection();
            if (iframeSel && !iframeSel.isCollapsed) {
              selectedText = iframeSel.toString().trim();
            }
          } catch {
            // cross-frame access may be restricted
            warnings.push('IFRAME_SELECTION_RESTRICTED');
          }
        }
      }
    }

    const surrounding = this.extractSurroundingContext(selectedText);

    return {
      selected_text: selectedText,
      before_context: surrounding.before,
      after_context: surrounding.after,
      viewport_rects: viewportRects,
      document_revision_hint: this.docToken,
      source: 'docs_adapter',
      warnings,
    };
  }

  public GetCaretContext(): CaretContext {
    let isCollapsed = true;
    let cursorRect: ViewportRect | undefined;
    let prefix = '';
    let suffix = '';
    let lineIdx: number | undefined;

    if (typeof document !== 'undefined') {
      const caretEl = document.querySelector(
        '.kix-cursor, .kix-cursor-caret, .docs-text-cursor'
      );
      if (caretEl) {
        const rect = caretEl.getBoundingClientRect();
        if (rect.width >= 0 && rect.height > 0) {
          cursorRect = this.domRectToViewport(rect);
        }
      }

      const sel = window.getSelection();
      if (sel) {
        isCollapsed = sel.isCollapsed;
      }

      const visibleText = this.GetVisibleText().text;
      if (visibleText) {
        prefix = visibleText.slice(0, 60);
        suffix = visibleText.slice(60, 120);
      }
    }

    return {
      is_collapsed: isCollapsed,
      cursor_rect: cursorRect,
      adjacent_prefix: prefix,
      adjacent_suffix: suffix,
      line_index: lineIdx,
      source: 'docs_adapter',
    };
  }

  public ResolveVisibleRangeRects(query: string): ViewportRect[] {
    const rects: ViewportRect[] = [];
    if (!query || typeof document === 'undefined') {
      return rects;
    }

    const textNodes = document.querySelectorAll(
      '.kix-lineview-text-block, .kix-word-html, [role="textbox"]'
    );
    textNodes.forEach((node) => {
      const content = node.textContent || '';
      if (content.toLowerCase().includes(query.toLowerCase())) {
        const r = node.getBoundingClientRect();
        if (r.width > 0 && r.height > 0) {
          rects.push(this.domRectToViewport(r));
        }
      }
    });

    return rects;
  }

  public createVisualTargetRef(
    rect: ViewportRect,
    semanticHint: string,
    ttlMs: number = GoogleDocsAdapter.DEFAULT_REF_TTL_MS
  ): VisualTargetRef {
    const now = Date.now();
    const hashData = `${this.currentTabId}:${this.currentFrameId}:${this.docToken}:${rect.x}:${rect.y}:${rect.width}:${rect.height}:${semanticHint}`;
    const hash = this.computeHash(hashData);

    return {
      tab_id: this.currentTabId,
      frame_global_id: this.currentFrameId,
      document_token: this.docToken,
      viewport_rect: rect,
      capture_hash: hash,
      semantic_hint: semanticHint,
      created_at: now,
      expires_at: now + ttlMs,
    };
  }

  public validateVisualTargetRef(
    ref: VisualTargetRef,
    expectedDocToken?: string
  ): boolean {
    const now = Date.now();
    if (now > ref.expires_at) {
      return false; // Expired
    }
    if (ref.tab_id !== this.currentTabId || ref.frame_global_id !== this.currentFrameId) {
      return false; // Tab / frame mismatch
    }
    const tokenToMatch = expectedDocToken || this.docToken;
    if (ref.document_token !== tokenToMatch) {
      return false; // Document revision changed
    }
    // Verify hash integrity
    const hashData = `${ref.tab_id}:${ref.frame_global_id}:${ref.document_token}:${ref.viewport_rect.x}:${ref.viewport_rect.y}:${ref.viewport_rect.width}:${ref.viewport_rect.height}:${ref.semantic_hint}`;
    const calculatedHash = this.computeHash(hashData);
    if (calculatedHash !== ref.capture_hash) {
      return false; // Hash mismatch
    }
    return true;
  }

  private locateEditorTarget(): { x: number; y: number } {
    if (typeof document !== 'undefined') {
      const editor =
        document.querySelector('.kix-appview-editor') ||
        document.querySelector('canvas.kix-canvas-tile-content') ||
        document.querySelector('.docs-texteventtarget-iframe');
      if (editor) {
        const rect = editor.getBoundingClientRect();
        return {
          x: Math.round(rect.left + rect.width / 2),
          y: Math.round(rect.top + rect.height / 2),
        };
      }
    }
    return { x: 300, y: 300 };
  }

  private domRectToViewport(r: DOMRect): ViewportRect {
    return {
      x: Math.round(r.left),
      y: Math.round(r.top),
      width: Math.round(r.width),
      height: Math.round(r.height),
    };
  }

  private extractSurroundingContext(selectedText: string): {
    before: string;
    after: string;
  } {
    const fullText = this.GetVisibleText().text;
    if (!selectedText || !fullText) {
      return { before: '', after: '' };
    }
    const idx = fullText.indexOf(selectedText);
    if (idx === -1) {
      return { before: '', after: '' };
    }
    const start = Math.max(0, idx - 50);
    const before = fullText.slice(start, idx);
    const end = Math.min(fullText.length, idx + selectedText.length + 50);
    const after = fullText.slice(idx + selectedText.length, end);
    return { before, after };
  }

  private generateDocumentToken(): string {
    return `docs_rev_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`;
  }

  private computeHash(str: string): string {
    let hash = 0;
    for (let i = 0; i < str.length; i++) {
      hash = (hash << 5) - hash + str.charCodeAt(i);
      hash |= 0;
    }
    return Math.abs(hash).toString(16).padStart(8, '0');
  }
}
