// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export type ExecutionWorld = 'ISOLATED' | 'MAIN';

export interface RedactionPolicy {
  maskCredentials?: boolean;
  maskTokens?: boolean;
  customPatterns?: RegExp[];
}

export interface AdapterDescriptor {
  adapter_id: string;
  version: string;
  exact_origin_patterns: string[];
  execution_world: ExecutionWorld;
  supported_operations: string[];
  required_browser_capabilities: string[];
  output_schema_version: string;
  redaction_policy: RedactionPolicy;
  max_output_bytes: number;
  component_package_id?: string;
}

export interface PageSignals {
  url?: string;
  origin?: string;
  framework?: 'react' | 'vue' | 'angular' | 'vanilla' | 'unknown';
  isCanvasEditor?: boolean;
  hasGoogleDocsDom?: boolean;
  hasReactFiber?: boolean;
  documentTitle?: string;
}

export interface ViewportRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface DocsSelectionSnapshot {
  selected_text: string;
  before_context: string;
  after_context: string;
  viewport_rects: ViewportRect[];
  document_revision_hint: string | number;
  source: 'docs_adapter';
  warnings: string[];
}

export interface CaretContext {
  is_collapsed: boolean;
  cursor_rect?: ViewportRect;
  adjacent_prefix: string;
  adjacent_suffix: string;
  line_index?: number;
  source: 'docs_adapter';
}

export interface DocumentSurfaceInfo {
  surface_type: 'canvas_kix' | 'canvas_editor' | 'dom_fallback';
  canvas_count: number;
  viewport_width: number;
  viewport_height: number;
  zoom_level: number;
  is_focused: boolean;
  editable: boolean;
}

export interface VisualTargetRef {
  tab_id: number;
  frame_global_id: string;
  document_token: string;
  viewport_rect: ViewportRect;
  capture_hash: string;
  semantic_hint: string;
  created_at: number;
  expires_at: number;
}

export type ActionKind =
  | 'focus_editor'
  | 'click_target'
  | 'type_into_caret'
  | 'key_chord'
  | 'select_range';

export interface ActionTranslation {
  kind: ActionKind;
  target_point?: { x: number; y: number };
  target_ref?: VisualTargetRef;
  text_to_type?: string;
  key?: string;
  modifiers?: string[];
  revalidation_required: boolean;
}

export interface ReactComponentRef {
  dom_target_ref: string;
  display_name: string;
  owner_display_names: string[];
  key_present: boolean;
  stable_test_id: string | null;
  primitive_prop_names: string[];
}

export interface ExtractRequest {
  operation: string;
  params?: Record<string, unknown>;
}

export interface ExtractResult<T = unknown> {
  success: boolean;
  source_adapter: string;
  data?: T;
  error?: string;
  warnings?: string[];
  bytes_produced?: number;
}

export interface ResolveSelectionResult {
  has_selection: boolean;
  snapshot?: DocsSelectionSnapshot;
  error?: string;
}

export interface ResolveTargetResult {
  found: boolean;
  target_ref?: VisualTargetRef | ReactComponentRef | string;
  viewport_rect?: ViewportRect;
  error?: string;
}

export interface PageSurfaceAdapter {
  descriptor(): AdapterDescriptor;
  Matches(origin: string, signals?: PageSignals): boolean;
  Extract(request: ExtractRequest): ExtractResult;
  ResolveSelection(): ResolveSelectionResult;
  ResolveTarget(query: string | Record<string, unknown>): ResolveTargetResult;
  TranslateAction(action: ActionTranslation): ActionTranslation | null;
}
