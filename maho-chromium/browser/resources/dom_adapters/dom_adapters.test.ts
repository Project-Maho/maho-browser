// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { describe, expect, it, beforeEach } from 'bun:test';
import {
  PageAdapterRegistry,
  GoogleDocsAdapter,
  ReactFiberInspector,
  initializeDefaultAdapters,
  VisualTargetRef,
} from './index';

describe('PageAdapterRegistry', () => {
  beforeEach(() => {
    PageAdapterRegistry.resetInstance();
  });

  it('registers and retrieves adapters', () => {
    const registry = initializeDefaultAdapters();
    const docs = registry.getAdapter('google_docs_canvas_adapter');
    const react = registry.getAdapter('react_fiber_dom_inspector');

    expect(docs).toBeDefined();
    expect(react).toBeDefined();
    expect(registry.getAllAdapters().length).toBe(2);
  });

  it('matches origin patterns correctly', () => {
    const registry = initializeDefaultAdapters();
    const docsMatch = registry.findMatchingAdapter('https://docs.google.com/document/d/12345/edit');
    expect(docsMatch).toBeDefined();
    expect(docsMatch?.descriptor().adapter_id).toBe('google_docs_canvas_adapter');

    const nonDocs = registry.findMatchingAdapter('https://example.com/page');
    // On example.com without React signals, React adapter matches '*' wildcard
    expect(nonDocs).toBeDefined();
  });

  it('enforces schema and byte bounding', () => {
    const registry = initializeDefaultAdapters();
    const { sanitized, bytes, warnings } = registry.sanitizeAndBoundOutput(
      { normal: 'hello', token: 'bearer secret_token_value_1234567890_extra' },
      { maskTokens: true, maskCredentials: true },
      1000
    );

    expect((sanitized as any).token).toContain('[REDACTED_CREDENTIAL]');
    expect(warnings).toContain('SENSITIVE_DATA_REDACTED');
    expect(bytes).toBeGreaterThan(0);
  });

  it('fails closed on unknown or invalid extract operation', () => {
    const registry = initializeDefaultAdapters();
    const res = registry.executeExtract(
      'google_docs_canvas_adapter',
      { operation: 'NonExistentOp' },
      'https://docs.google.com/document/d/123'
    );
    expect(res.success).toBe(false);
    expect(res.warnings).toContain('UNSUPPORTED_OPERATION');
  });
});

describe('GoogleDocsAdapter', () => {
  it('has valid descriptor matching Proposal J requirements', () => {
    const adapter = new GoogleDocsAdapter(1, 'frame_0');
    const desc = adapter.descriptor();

    expect(desc.adapter_id).toBe('google_docs_canvas_adapter');
    expect(desc.execution_world).toBe('ISOLATED');
    expect(desc.supported_operations).toContain('GetDocumentSurfaceInfo');
    expect(desc.supported_operations).toContain('GetVisibleText');
    expect(desc.supported_operations).toContain('GetSelectionSnapshot');
    expect(desc.supported_operations).toContain('GetCaretContext');
    expect(desc.supported_operations).toContain('ResolveVisibleRangeRects');
    expect(desc.supported_operations).toContain('TranslateAction');
    expect(desc.max_output_bytes).toBe(65536);
  });

  it('extracts DocumentSurfaceInfo fallback when headless', () => {
    const adapter = new GoogleDocsAdapter(1, 'frame_0');
    const res = adapter.Extract({ operation: 'GetDocumentSurfaceInfo' });
    expect(res.success).toBe(true);
    expect(res.data).toBeDefined();
    expect((res.data as any).surface_type).toBeDefined();
    expect((res.data as any).viewport_width).toBe(1024);
  });

  it('extracts selection snapshot with structured fields', () => {
    const adapter = new GoogleDocsAdapter(1, 'frame_0');
    const res = adapter.Extract({ operation: 'GetSelectionSnapshot' });
    expect(res.success).toBe(true);
    const snapshot = res.data as any;
    expect(snapshot.source).toBe('docs_adapter');
    expect(snapshot.selected_text).toBe('');
    expect(snapshot.warnings).toBeArray();
  });

  it('creates and validates short-lived VisualTargetRef', () => {
    const adapter = new GoogleDocsAdapter(42, 'main_frame');
    const rect = { x: 100, y: 150, width: 200, height: 20 };
    const ref = adapter.createVisualTargetRef(rect, 'search_match', 5000);

    expect(ref.tab_id).toBe(42);
    expect(ref.frame_global_id).toBe('main_frame');
    expect(ref.viewport_rect).toEqual(rect);
    expect(ref.capture_hash).toBeDefined();
    expect(ref.expires_at).toBeGreaterThan(Date.now());

    // Valid ref
    expect(adapter.validateVisualTargetRef(ref)).toBe(true);

    // Mismatched document token fails closed
    expect(adapter.validateVisualTargetRef(ref, 'different_token')).toBe(false);

    // Expired ref fails closed
    const expiredRef: VisualTargetRef = {
      ...ref,
      expires_at: Date.now() - 1000,
    };
    expect(adapter.validateVisualTargetRef(expiredRef)).toBe(false);

    // Tampered rect fails hash validation
    const tamperedRef: VisualTargetRef = {
      ...ref,
      viewport_rect: { ...rect, x: 999 },
    };
    expect(adapter.validateVisualTargetRef(tamperedRef)).toBe(false);
  });

  it('translates actions safely with revalidation flag', () => {
    const adapter = new GoogleDocsAdapter(1, 'main');
    const targetRef = adapter.createVisualTargetRef({ x: 50, y: 80, width: 100, height: 20 }, 'btn');

    const clickAction = adapter.TranslateAction({
      kind: 'click_target',
      target_ref: targetRef,
      revalidation_required: false,
    });
    expect(clickAction).toBeDefined();
    expect(clickAction?.kind).toBe('click_target');
    expect(clickAction?.target_point).toEqual({ x: 100, y: 90 });
    expect(clickAction?.revalidation_required).toBe(true);

    const typeAction = adapter.TranslateAction({
      kind: 'type_into_caret',
      text_to_type: 'Hello Maho',
      revalidation_required: false,
    });
    expect(typeAction?.kind).toBe('type_into_caret');
    expect(typeAction?.text_to_type).toBe('Hello Maho');
    expect(typeAction?.revalidation_required).toBe(true);
  });

  it('enforces strict origin checking and rejects unauthorized origins', () => {
    const adapter = new GoogleDocsAdapter(1, 'main');

    // Authorized Google Docs origins
    expect(adapter.Matches('https://docs.google.com/document/d/123/edit')).toBe(true);
    expect(adapter.Matches('https://docs.google.com/spreadsheets/d/123/edit')).toBe(true);
    expect(adapter.Matches('https://docs.google.com/presentation/d/123/edit')).toBe(true);
    expect(adapter.Matches('https://docs.google.com')).toBe(true);
    expect(GoogleDocsAdapter.isAuthorizedOrigin('https://docs.google.com')).toBe(true);
    expect(GoogleDocsAdapter.isAuthorizedOrigin('https://docs.google.com/document/d/abc')).toBe(true);

    // Unauthorized origins must fail closed even if signals claim Google Docs DOM
    expect(adapter.Matches('https://evil.com', { hasGoogleDocsDom: true })).toBe(false);
    expect(adapter.Matches('https://docs.google.com.attacker.com', { isCanvasEditor: true })).toBe(false);
    expect(adapter.Matches('http://docs.google.com/document/d/123')).toBe(false);
    expect(adapter.Matches('https://google.com')).toBe(false);
    expect(adapter.Matches('')).toBe(false);
    expect(GoogleDocsAdapter.isAuthorizedOrigin('https://evil.com')).toBe(false);
    expect(GoogleDocsAdapter.isAuthorizedOrigin('http://docs.google.com')).toBe(false);
  });

  it('validates event.origin strictly against https://docs.google.com', () => {
    const adapter = new GoogleDocsAdapter(1, 'main');

    expect(adapter.validateEventOrigin({ origin: 'https://docs.google.com' })).toBe(true);
    expect(adapter.validateEventOrigin({ origin: 'https://docs.google.com/doc' })).toBe(false);
    expect(adapter.validateEventOrigin({ origin: 'http://docs.google.com' })).toBe(false);
    expect(adapter.validateEventOrigin({ origin: 'https://evil.docs.google.com' })).toBe(false);
    expect(adapter.validateEventOrigin({ origin: 'https://docs.google.com.evil.com' })).toBe(false);
    expect(adapter.validateEventOrigin(null as any)).toBe(false);
  });

  it('sanitizes custom event payloads against unauthorized origins', () => {
    const adapter = new GoogleDocsAdapter(1, 'main');

    // Unauthorized origin fails closed and returns null
    const unauthorizedPayload = { action: 'insert_text', content: 'Hello' };
    expect(adapter.sanitizeCustomEventPayload(unauthorizedPayload, 'https://evil.com')).toBeNull();
    expect(adapter.handleCustomEvent({ origin: 'https://attacker.org', detail: { key: 'val' } })).toBeNull();

    // Authorized origin sanitizes payload and strips prototype pollution
    const validPayload = {
      action: 'insert_text',
      content: 'Safe text',
      nested: { count: 42 },
      __proto__: { polluted: true },
    };
    const sanitized = adapter.sanitizeCustomEventPayload(validPayload, 'https://docs.google.com');
    expect(sanitized).toBeDefined();
    expect((sanitized as any).action).toBe('insert_text');
    expect((sanitized as any).content).toBe('Safe text');
    expect((sanitized as any).nested.count).toBe(42);
    expect(Object.prototype.hasOwnProperty.call(sanitized, '__proto__')).toBe(false);

    // CustomEvent handling with authorized origin
    const customEvtRes = adapter.handleCustomEvent({
      origin: 'https://docs.google.com',
      detail: { text: 'Testing handler' },
    });
    expect(customEvtRes).toEqual({ text: 'Testing handler' });
  });
});

describe('ReactFiberInspector', () => {
  it('has valid descriptor matching Proposal K requirements', () => {
    const inspector = new ReactFiberInspector();
    const desc = inspector.descriptor();

    expect(desc.adapter_id).toBe('react_fiber_dom_inspector');
    expect(desc.execution_world).toBe('MAIN');
    expect(desc.supported_operations).toContain('InspectElement');
    expect(desc.supported_operations).toContain('FindComponentsByDisplayName');
    expect(desc.supported_operations).toContain('GetFiberHierarchy');
    expect(desc.supported_operations).toContain('ResolveTarget');
  });

  it('extracts primitive prop names only (never prop values)', () => {
    const inspector = new ReactFiberInspector();
    const mockFiber = {
      tag: 0,
      type: function MockButton() {},
      memoizedProps: {
        label: 'Click me',
        count: 42,
        isActive: true,
        secretApiKey: 'sk-1234567890abcdef',
        onCustomClick: () => {},
        complexObject: { nested: 'secret' },
      },
    };

    const propNames = inspector.extractPrimitivePropNames(mockFiber);
    expect(propNames).toContain('label');
    expect(propNames).toContain('count');
    expect(propNames).toContain('isActive');
    expect(propNames).toContain('secretApiKey');
    // Function and object types are excluded
    expect(propNames).not.toContain('onCustomClick');
    expect(propNames).not.toContain('complexObject');
  });

  it('generates safe selectors avoiding generated CSS classes', () => {
    const inspector = new ReactFiberInspector();
    
    // Mock element with data-testid
    const mockElWithTestId = {
      tagName: 'BUTTON',
      getAttribute: (name: string) => (name === 'data-testid' ? 'submit-btn' : null),
      hasAttribute: (name: string) => name === 'data-testid',
      classList: ['css-1yv9g2', 'btn-primary'],
      id: '',
    } as unknown as Element;

    expect(inspector.generateSafeSelector(mockElWithTestId)).toBe('[data-testid="submit-btn"]');

    // Mock element with stable id
    const mockElWithId = {
      tagName: 'INPUT',
      getAttribute: () => null,
      hasAttribute: () => false,
      classList: ['css-123xyz'],
      id: 'username-field',
    } as unknown as Element;

    expect(inspector.generateSafeSelector(mockElWithId)).toBe('#username-field');
  });

  it('inspects element with React Fiber hierarchy properly', () => {
    const inspector = new ReactFiberInspector();

    const parentFiber = {
      tag: 0,
      type: function AppContainer() {},
      return: null,
    };

    const childFiber = {
      tag: 0,
      type: function SubmitButton() {},
      key: 'btn-key-1',
      return: parentFiber,
      memoizedProps: {
        title: 'Submit Form',
        disabled: false,
        onClick: () => {},
      },
    };

    const mockEl = {
      tagName: 'BUTTON',
      getAttribute: () => null,
      hasAttribute: () => false,
      classList: [],
      id: '',
      __reactFiber$abc: childFiber,
    } as unknown as Element;

    const inspected = inspector.inspectElement(mockEl);
    expect(inspected.display_name).toBe('SubmitButton');
    expect(inspected.key_present).toBe(true);
    expect(inspected.owner_display_names).toContain('AppContainer');
    expect(inspected.primitive_prop_names).toEqual(['title', 'disabled']);
  });

  it('handles ForwardRef and Memo components', () => {
    const inspector = new ReactFiberInspector();

    const forwardRefFiber = {
      tag: 11,
      type: {
        render: function CustomInput() {},
      },
      return: null,
      memoizedProps: {
        placeholder: 'Enter name',
      },
    };

    const mockEl = {
      tagName: 'INPUT',
      getAttribute: () => null,
      hasAttribute: () => false,
      classList: [],
      id: '',
      __reactFiber$123: forwardRefFiber,
    } as unknown as Element;

    const inspected = inspector.inspectElement(mockEl);
    expect(inspected.display_name).toBe('CustomInput');
    expect(inspected.primitive_prop_names).toEqual(['placeholder']);
  });
});

