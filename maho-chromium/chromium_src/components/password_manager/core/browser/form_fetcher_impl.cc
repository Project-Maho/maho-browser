// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for form_fetcher_impl.cc.
//
// build/scripts/apply_chromium_src_overrides.py applies the guarded Task 8
// document-aware password-fill contract to:
//   components/password_manager/core/browser/form_fetcher_impl.cc
//
// Discovery remains secret-free. Final delivery uses a move-only, driver-bound
// resolver capability; document token and origin are rechecked before legacy
// renderer dispatch, and transient resolution storage is zeroized on drop.
