// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {
  AdapterDescriptor,
  ExtractRequest,
  ExtractResult,
  PageSignals,
  PageSurfaceAdapter,
  RedactionPolicy,
} from './types';

export enum EscalationTier {
  TIER_1_GENERIC_ISOLATED = 1,
  TIER_2_AX_SNAPSHOT = 2,
  TIER_3_DOM_SAFE_SELECTOR = 3,
  TIER_4_SPECIALIZED_ADAPTER = 4,
  TIER_5_VISUAL_TARGET_REF = 5,
}

const DEFAULT_SENSITIVE_PATTERNS: RegExp[] = [
  /bearer\s+[a-zA-Z0-9_\-\.=:_+/]+/gi,
  /token["':\s=]+[a-zA-Z0-9_\-\.]{16,}/gi,
  /api[_-]?key["':\s=]+[a-zA-Z0-9_\-]{16,}/gi,
  /auth(?:orization)?["':\s=]+[a-zA-Z0-9_\-\.]{16,}/gi,
  /password["':\s=]+[^\r\n]+/gi,
  /session[_-]?id["':\s=]+[a-zA-Z0-9_\-]{16,}/gi,
];

export class PageAdapterRegistry {
  private static instance: PageAdapterRegistry | null = null;
  private adapters = new Map<string, PageSurfaceAdapter>();

  public static getInstance(): PageAdapterRegistry {
    if (!PageAdapterRegistry.instance) {
      PageAdapterRegistry.instance = new PageAdapterRegistry();
    }
    return PageAdapterRegistry.instance;
  }

  public static resetInstance(): void {
    PageAdapterRegistry.instance = null;
  }

  public registerAdapter(adapter: PageSurfaceAdapter): void {
    const desc = adapter.descriptor();
    if (!desc || !desc.adapter_id) {
      throw new Error('Adapter must have a valid descriptor and adapter_id');
    }
    this.adapters.set(desc.adapter_id, adapter);
  }

  public unregisterAdapter(adapterId: string): boolean {
    return this.adapters.delete(adapterId);
  }

  public getAdapter(adapterId: string): PageSurfaceAdapter | undefined {
    return this.adapters.get(adapterId);
  }

  public getAllAdapters(): PageSurfaceAdapter[] {
    return Array.from(this.adapters.values());
  }

  public clear(): void {
    this.adapters.clear();
  }

  /**
   * Find matching specialized adapter according to Proposal I (Tier 4 escalation).
   * Exact-origin patterns and page signals are evaluated.
   */
  public findMatchingAdapter(
    origin: string,
    signals?: PageSignals
  ): PageSurfaceAdapter | null {
    for (const adapter of this.adapters.values()) {
      if (adapter.Matches(origin, signals)) {
        return adapter;
      }
    }
    return null;
  }

  /**
   * Execute an extract request against a registered adapter with strict safety policy:
   * - exact origin match verification
   * - schema/byte cap enforcement
   * - credential and token redaction
   */
  public executeExtract(
    adapterId: string,
    request: ExtractRequest,
    origin: string,
    signals?: PageSignals
  ): ExtractResult {
    const adapter = this.adapters.get(adapterId);
    if (!adapter) {
      return {
        success: false,
        source_adapter: adapterId,
        error: `Adapter '${adapterId}' not found`,
        warnings: ['ADAPTER_NOT_REGISTERED'],
      };
    }

    if (!adapter.Matches(origin, signals)) {
      return {
        success: false,
        source_adapter: adapterId,
        error: `Origin '${origin}' does not match adapter policy`,
        warnings: ['ORIGIN_MISMATCH_FAIL_CLOSED'],
      };
    }

    const desc = adapter.descriptor();
    if (!desc.supported_operations.includes(request.operation)) {
      return {
        success: false,
        source_adapter: adapterId,
        error: `Operation '${request.operation}' is not supported by adapter '${adapterId}'`,
        warnings: ['UNSUPPORTED_OPERATION'],
      };
    }

    try {
      const rawResult = adapter.Extract(request);
      if (!rawResult.success) {
        return rawResult;
      }

      // Redact and bound output bytes
      const { sanitized, bytes, warnings } = this.sanitizeAndBoundOutput(
        rawResult.data,
        desc.redaction_policy,
        desc.max_output_bytes
      );

      return {
        success: true,
        source_adapter: adapterId,
        data: sanitized,
        warnings: [...(rawResult.warnings || []), ...warnings],
        bytes_produced: bytes,
      };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return {
        success: false,
        source_adapter: adapterId,
        error: `Extract execution failed: ${message}`,
        warnings: ['EXECUTION_ERROR_FAIL_CLOSED'],
      };
    }
  }

  /**
   * Redacts decoded values and rejects output exceeding the byte budget.
   */
  public sanitizeAndBoundOutput(
    data: unknown,
    policy: RedactionPolicy = {},
    maxBytes: number = 65536
  ): { sanitized: unknown; bytes: number; warnings: string[] } {
    const warnings: string[] = [];
    if (data === undefined || data === null) {
      return { sanitized: data, bytes: 0, warnings };
    }

    let jsonStr: string;
    try {
      jsonStr = JSON.stringify(data);
    } catch {
      return {
        sanitized: null,
        bytes: 0,
        warnings: ['NON_SERIALIZABLE_DATA'],
      };
    }

    const shouldRedact = policy.maskCredentials !== false || policy.maskTokens !== false;
    if (shouldRedact) {
      const patterns = [...DEFAULT_SENSITIVE_PATTERNS, ...(policy.customPatterns || [])];
      const redact = (value: unknown, key = ''): unknown => {
        if (/^(password|token|api[_-]?key|auth(?:orization)?|session[_-]?id)$/i.test(key)) {
          warnings.push('SENSITIVE_DATA_REDACTED');
          return '[REDACTED_CREDENTIAL]';
        }
        if (typeof value === 'string') {
          for (const pattern of patterns) {
            pattern.lastIndex = 0;
            value = (value as string).replace(pattern, () => {
              warnings.push('SENSITIVE_DATA_REDACTED');
              return '[REDACTED_CREDENTIAL]';
            });
          }
          return value;
        }
        if (Array.isArray(value)) return value.map(item => redact(item));
        if (value && typeof value === 'object') {
          return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, redact(item, key)]));
        }
        return value;
      };
      jsonStr = JSON.stringify(redact(JSON.parse(jsonStr)));
    }

    const encodedBytes = new TextEncoder().encode(jsonStr).length;
    if (encodedBytes > maxBytes) {
      throw new Error(`Output exceeds ${maxBytes} byte limit`);
    }

    let sanitized: unknown;
    try {
      sanitized = JSON.parse(jsonStr);
    } catch {
      sanitized = jsonStr;
    }

    return { sanitized, bytes: encodedBytes, warnings };
  }
}
