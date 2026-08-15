// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for password_store.cc.
//
// build/scripts/apply_chromium_src_overrides.py applies the guarded Task 8
// document-aware password-fill contract to:
//   components/password_manager/core/browser/password_store/password_store.cc
//
// Discovery remains secret-free. Final delivery uses a move-only, driver-bound
// resolver capability; document token and origin are rechecked before legacy
// renderer dispatch, and transient resolution storage is zeroized on drop.
