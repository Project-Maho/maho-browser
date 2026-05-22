// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for keychain_password_mac.mm
//
// Documents the upstream patch applied to keychain_password_mac.mm:
//
// 1. In the #else (non-Google-branded) block, change:
//      kDefaultServiceName = "Chromium Safe Storage"  ->  "Maho Safe Storage"
//      kDefaultAccountName = "Chromium"               ->  "Maho"
//
//    This gives Maho its own macOS Keychain entry for encrypting profile
//    databases (Web Data, Login Data, Cookies, etc.), preventing conflicts
//    with Chromium's entry and avoiding access-denied errors when the
//    ad-hoc code signing identity changes between builds.
//
// Applied via: build/scripts/apply_chromium_src_overrides.py
