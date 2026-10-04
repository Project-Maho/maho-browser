// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export * from './types';
export * from './page_adapter_registry';
export * from './google_docs_adapter';
export * from './react_fiber_inspector';

import { PageAdapterRegistry } from './page_adapter_registry';
import { GoogleDocsAdapter } from './google_docs_adapter';
import { ReactFiberInspector } from './react_fiber_inspector';

/**
 * Initialize and register default DOM adapters.
 */
export function initializeDefaultAdapters(): PageAdapterRegistry {
  const registry = PageAdapterRegistry.getInstance();
  registry.registerAdapter(new GoogleDocsAdapter());
  registry.registerAdapter(new ReactFiberInspector());
  return registry;
}
