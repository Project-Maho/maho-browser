// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {
  ActionTranslation,
  AdapterDescriptor,
  ExtractRequest,
  ExtractResult,
  PageSignals,
  PageSurfaceAdapter,
  ReactComponentRef,
  ResolveSelectionResult,
  ResolveTargetResult,
} from './types';

// Fiber tag constants from React internals
const TAG_FUNCTION_COMPONENT = 0;
const TAG_CLASS_COMPONENT = 1;
const TAG_INDETERMINATE_COMPONENT = 2;
const TAG_HOST_ROOT = 3;
const TAG_HOST_PORTAL = 4;
const TAG_HOST_COMPONENT = 5;
const TAG_HOST_TEXT = 6;
const TAG_FRAGMENT = 7;
const TAG_FORWARD_REF = 11;
const TAG_MEMO_COMPONENT = 14;
const TAG_SIMPLE_MEMO_COMPONENT = 15;

const MAX_OWNER_HIERARCHY_DEPTH = 10;
const MAX_SEARCH_MATCHES = 20;

// Generated / CSS-in-JS class pattern heuristics (e.g. css-1yv9g2, sc-bdVaJa, styled-*, _xyz_123)
const GENERATED_CLASS_PATTERNS: RegExp[] = [
  /^css-[a-zA-Z0-9_-]+$/,
  /^sc-[a-zA-Z0-9_-]+$/,
  /^[a-zA-Z0-9_-]{6,12}$/, // Short random hashes
  /^styled-[a-zA-Z0-9_-]+$/,
  /^style__[a-zA-Z0-9_-]+$/,
  /^_[a-zA-Z0-9_-]{5,}$/,
];

function cssEscape(value: string): string {
  if (typeof CSS !== 'undefined' && typeof CSS.escape === 'function') {
    return CSS.escape(value);
  }
  return value.replace(/([!"#$%&'()*+,.\/:;<=>?@[\\\]^`{|}~])/g, '\\$1');
}

export class ReactFiberInspector implements PageSurfaceAdapter {
  private static readonly ADAPTER_ID = 'react_fiber_dom_inspector';
  private static readonly VERSION = '1.0.0';
  private static readonly MAX_OUTPUT_BYTES = 65536;

  public descriptor(): AdapterDescriptor {
    return {
      adapter_id: ReactFiberInspector.ADAPTER_ID,
      version: ReactFiberInspector.VERSION,
      exact_origin_patterns: ['*'],
      execution_world: 'MAIN',
      supported_operations: [
        'InspectElement',
        'FindComponentsByDisplayName',
        'GetFiberHierarchy',
        'ResolveTarget',
      ],
      required_browser_capabilities: ['dom_read'],
      output_schema_version: '1.0',
      redaction_policy: {
        maskCredentials: true,
        maskTokens: true,
      },
      max_output_bytes: ReactFiberInspector.MAX_OUTPUT_BYTES,
    };
  }

  public Matches(origin: string, signals?: PageSignals): boolean {
    if (signals?.hasReactFiber || signals?.framework === 'react') {
      return true;
    }
    if (typeof document === 'undefined') return false;

    // Check if any element has React Fiber keys or root container
    const root = document.getElementById('root') || document.body;
    if (root) {
      if ((root as any)._reactRootContainer) return true;
      for (const key of Object.keys(root)) {
        if (key.startsWith('__reactFiber$') || key.startsWith('__reactInternalInstance$')) {
          return true;
        }
      }
    }
    return false;
  }

  public Extract(request: ExtractRequest): ExtractResult {
    switch (request.operation) {
      case 'ResolveTarget': {
        const selector = request.params?.selector;
        if (typeof selector !== 'string' || !selector) {
          return {success: false, source_adapter: ReactFiberInspector.ADAPTER_ID, error: 'selector param required'};
        }
        return {
          success: true,
          source_adapter: ReactFiberInspector.ADAPTER_ID,
          data: this.ResolveTarget(selector),
        };
      }
      case 'InspectElement': {
        const selector = request.params?.selector as string | undefined;
        let el: Element | null = null;
        if (selector && typeof document !== 'undefined') {
          try {
            el = document.querySelector(selector);
          } catch {
            return {
              success: false,
              source_adapter: ReactFiberInspector.ADAPTER_ID,
              error: `Invalid DOM selector: ${selector}`,
            };
          }
        }
        if (!el && typeof document !== 'undefined') {
          el = document.activeElement;
        }
        if (!el) {
          return {
            success: false,
            source_adapter: ReactFiberInspector.ADAPTER_ID,
            error: 'No target element found to inspect',
          };
        }
        const ref = this.inspectElement(el);
        return {
          success: true,
          source_adapter: ReactFiberInspector.ADAPTER_ID,
          data: ref,
        };
      }
      case 'FindComponentsByDisplayName': {
        const name = (request.params?.displayName as string) || '';
        if (!name) {
          return {
            success: false,
            source_adapter: ReactFiberInspector.ADAPTER_ID,
            error: 'displayName param required',
          };
        }
        const refs = this.findComponentsByDisplayName(name);
        return {
          success: true,
          source_adapter: ReactFiberInspector.ADAPTER_ID,
          data: { matches: refs },
        };
      }
      case 'GetFiberHierarchy': {
        const selector = request.params?.selector as string | undefined;
        const el = selector && typeof document !== 'undefined' ? document.querySelector(selector) : null;
        if (!el) {
          return {
            success: false,
            source_adapter: ReactFiberInspector.ADAPTER_ID,
            error: 'Target element not found',
          };
        }
        const ref = this.inspectElement(el);
        return {
          success: true,
          source_adapter: ReactFiberInspector.ADAPTER_ID,
          data: {
            display_name: ref.display_name,
            owners: ref.owner_display_names,
            dom_target: ref.dom_target_ref,
          },
        };
      }
      default:
        return {
          success: false,
          source_adapter: ReactFiberInspector.ADAPTER_ID,
          error: `Unsupported operation: ${request.operation}`,
        };
    }
  }

  public ResolveSelection(): ResolveSelectionResult {
    return {
      has_selection: false,
      error: 'ReactFiberInspector is a target inspector, not a text selection adapter',
    };
  }

  public ResolveTarget(query: string | Record<string, unknown>): ResolveTargetResult {
    const selector = typeof query === 'string' ? query : (query.selector as string) || '';
    if (!selector || typeof document === 'undefined') {
      return { found: false, error: 'Selector query empty' };
    }

    let el: Element | null = null;
    try {
      el = document.querySelector(selector);
    } catch (e: any) {
      return { found: false, error: `Invalid selector: ${e.message}` };
    }

    if (!el) {
      return { found: false, error: 'Element not found in DOM' };
    }

    const compRef = this.inspectElement(el);
    const rect = el.getBoundingClientRect();

    return {
      found: true,
      target_ref: compRef,
      viewport_rect: {
        x: Math.round(rect.left),
        y: Math.round(rect.top),
        width: Math.round(rect.width),
        height: Math.round(rect.height),
      },
    };
  }

  public TranslateAction(action: ActionTranslation): ActionTranslation | null {
    // Read-only bridge: TranslateAction only validates revalidation without framework mutations
    return {
      ...action,
      revalidation_required: true,
    };
  }

  // ---- Safe Fiber Traversal and Metadata Mapping ----

  public inspectElement(el: Element): ReactComponentRef {
    const domRef = this.generateSafeSelector(el);
    const testId = this.extractStableTestId(el);
    const fiber = this.getFiberFromNode(el);

    if (!fiber) {
      return {
        dom_target_ref: domRef,
        display_name: el.tagName.toLowerCase(),
        owner_display_names: [],
        key_present: Boolean(testId),
        stable_test_id: testId,
        primitive_prop_names: [],
      };
    }

    const componentFiber = this.findNearestComponentFiber(fiber);
    const displayName = this.getFiberDisplayName(componentFiber || fiber);
    const ownerNames = this.getOwnerHierarchy(componentFiber || fiber);
    const keyPresent = (componentFiber || fiber).key != null;
    const primitiveProps = this.extractPrimitivePropNames(componentFiber || fiber);

    return {
      dom_target_ref: domRef,
      display_name: displayName,
      owner_display_names: ownerNames,
      key_present: keyPresent,
      stable_test_id: testId,
      primitive_prop_names: primitiveProps,
    };
  }

  public findComponentsByDisplayName(name: string, rootElement?: Element): ReactComponentRef[] {
    const results: ReactComponentRef[] = [];
    if (typeof document === 'undefined') return results;

    const searchRoot = rootElement || document.body;
    const allElements = searchRoot.querySelectorAll('*');

    for (let i = 0; i < allElements.length; i++) {
      if (results.length >= MAX_SEARCH_MATCHES) break;
      const el = allElements[i];
      const fiber = this.getFiberFromNode(el);
      if (fiber) {
        const compFiber = this.findNearestComponentFiber(fiber);
        if (compFiber) {
          const compName = this.getFiberDisplayName(compFiber);
          if (compName.toLowerCase() === name.toLowerCase()) {
            results.push(this.inspectElement(el));
          }
        }
      }
    }

    return results;
  }

  public getFiberFromNode(node: Node | Element): any {
    if (!node) return null;
    const anyNode = node as any;
    for (const key of Object.keys(anyNode)) {
      if (key.startsWith('__reactFiber$') || key.startsWith('__reactInternalInstance$')) {
        return anyNode[key];
      }
    }
    return null;
  }

  private findNearestComponentFiber(fiber: any): any {
    let curr = fiber;
    while (curr) {
      if (
        curr.tag === TAG_FUNCTION_COMPONENT ||
        curr.tag === TAG_CLASS_COMPONENT ||
        curr.tag === TAG_FORWARD_REF ||
        curr.tag === TAG_MEMO_COMPONENT ||
        curr.tag === TAG_SIMPLE_MEMO_COMPONENT ||
        curr.tag === TAG_INDETERMINATE_COMPONENT
      ) {
        return curr;
      }
      curr = curr.return;
    }
    return null;
  }

  private getFiberDisplayName(fiber: any): string {
    if (!fiber) return 'Unknown';
    if (typeof fiber.type === 'string') {
      return fiber.type;
    }
    if (typeof fiber.type === 'function') {
      return fiber.type.displayName || fiber.type.name || 'Anonymous';
    }
    if (fiber.type && typeof fiber.type === 'object') {
      if (fiber.type.displayName) return fiber.type.displayName;
      if (fiber.type.render?.displayName) return fiber.type.render.displayName;
      if (fiber.type.render?.name) return fiber.type.render.name;
    }
    if (fiber.elementType && typeof fiber.elementType === 'function') {
      return fiber.elementType.displayName || fiber.elementType.name || 'Anonymous';
    }
    return 'Component';
  }

  private getOwnerHierarchy(fiber: any): string[] {
    const owners: string[] = [];
    let curr = fiber?.return;
    let depth = 0;

    while (curr && depth < MAX_OWNER_HIERARCHY_DEPTH) {
      if (
        curr.tag === TAG_FUNCTION_COMPONENT ||
        curr.tag === TAG_CLASS_COMPONENT ||
        curr.tag === TAG_FORWARD_REF ||
        curr.tag === TAG_MEMO_COMPONENT ||
        curr.tag === TAG_SIMPLE_MEMO_COMPONENT
      ) {
        const name = this.getFiberDisplayName(curr);
        if (name && name !== 'Anonymous' && name !== 'Unknown' && !owners.includes(name)) {
          owners.push(name);
          depth++;
        }
      }
      curr = curr.return;
    }

    return owners;
  }

  /**
   * Strictly extracts ONLY the names of props whose values are primitive types
   * (string, number, boolean). Prop values are never accessed or returned to
   * prevent leaking secrets, tokens, or state.
   */
  public extractPrimitivePropNames(fiber: any): string[] {
    const propNames: string[] = [];
    const props = fiber?.memoizedProps || fiber?.pendingProps;
    if (!props || typeof props !== 'object') {
      return propNames;
    }

    for (const key of Object.keys(props)) {
      if (key === 'children' || key === 'key' || key === 'ref') {
        continue;
      }
      try {
        const val = props[key];
        const valType = typeof val;
        if (valType === 'string' || valType === 'number' || valType === 'boolean') {
          propNames.push(key);
        }
      } catch {
        // Ignore any getter invocation exceptions
      }
    }

    return propNames;
  }

  /**
   * Generates a safe DOM selector prioritizing stable identifiers and semantic attributes
   * over generated CSS classes according to Boost safety guidelines.
   */
  public generateSafeSelector(el: Element): string {
    const testId = this.extractStableTestId(el);
    if (testId) {
      if (el.hasAttribute('data-testid')) return `[data-testid="${cssEscape(testId)}"]`;
      if (el.hasAttribute('data-test-id')) return `[data-test-id="${cssEscape(testId)}"]`;
      if (el.hasAttribute('data-cy')) return `[data-cy="${cssEscape(testId)}"]`;
      if (el.hasAttribute('data-qa')) return `[data-qa="${cssEscape(testId)}"]`;
    }

    if (el.id && !this.isGeneratedIdentifier(el.id)) {
      return `#${cssEscape(el.id)}`;
    }

    const ariaLabel = el.getAttribute('aria-label');
    if (ariaLabel && ariaLabel.length < 50) {
      return `${el.tagName.toLowerCase()}[aria-label="${cssEscape(ariaLabel)}"]`;
    }

    const role = el.getAttribute('role');
    const nameAttr = el.getAttribute('name');
    if (nameAttr) {
      return `${el.tagName.toLowerCase()}[name="${cssEscape(nameAttr)}"]`;
    }
    if (role) {
      return `${el.tagName.toLowerCase()}[role="${cssEscape(role)}"]`;
    }

    // Filter out generated classes
    const validClasses = Array.from(el.classList).filter(
      (cls) => !this.isGeneratedClass(cls)
    );
    if (validClasses.length > 0) {
      return `${el.tagName.toLowerCase()}.${validClasses.slice(0, 2).map((c) => cssEscape(c)).join('.')}`;
    }

    return el.tagName.toLowerCase();
  }

  public extractStableTestId(el: Element): string | null {
    const testAttrs = ['data-testid', 'data-test-id', 'data-cy', 'data-qa'];
    for (const attr of testAttrs) {
      const val = el.getAttribute(attr);
      if (val) return val;
    }
    return null;
  }

  private isGeneratedClass(className: string): boolean {
    return GENERATED_CLASS_PATTERNS.some((pattern) => pattern.test(className));
  }

  private isGeneratedIdentifier(id: string): boolean {
    return /^[:_]?[a-zA-Z0-9]{8,}$/.test(id) || /^react-[0-9]+/.test(id);
  }
}
