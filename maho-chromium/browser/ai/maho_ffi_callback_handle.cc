// Copyright 2025 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "maho/browser/ai/maho_ffi_callback_handle.h"

// FfiCallbackHandle<T> is a class template — all definitions live in the
// _inl.h header (included transitively by the .h). This .cc exists for:
//
// 1. Keeping the build system happy (source_set needs at least one .cc).
// 2. Hosting explicit template instantiations once concrete owner types
//    are introduced. Add them below as each adapter lands.
//
// Planned instantiations (task 4.1.4):
//   template class maho::FfiCallbackHandle<MahoUnifiedAgentAdapter>;
//
// After ConversationTask lands (task 4.1.2):
//   template class maho::FfiCallbackHandle<ConversationTask>;
