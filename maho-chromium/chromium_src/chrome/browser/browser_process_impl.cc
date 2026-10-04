// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for browser_process_impl.cc
//
// Documents the upstream patches applied to browser_process_impl.cc by
// build/scripts/apply_chromium_src_overrides.py:
//
// 1. Replace the IS_MAC os_crypt includes block (~line 285-287):
//
//      from:
//        #if BUILDFLAG(IS_MAC)
//        #include "components/os_crypt/async/browser/keychain_key_provider.h"
//        #endif
//
//      to:
//        #if BUILDFLAG(IS_MAC)
//        #include "maho/browser/os_crypt/maho_file_key_provider_mac.h"
//        #endif
//
//    KeychainKeyProvider is no longer included; MahoFileKeyProvider is the only
//    macOS provider, eliminating the "v10" tag collision at runtime.
//
// 2. In BrowserProcessImpl::PreMainMessageLoopRun(), replace the
//    #if BUILDFLAG(IS_MAC) provider registration block (~line 1586-1592):
//
//      from:
//        #if BUILDFLAG(IS_MAC)
//          if (base::FeatureList::IsEnabled(features::kUseKeychainKeyProvider)) {
//            providers.emplace_back(std::make_pair(
//                /*precedence=*/10u,
//                std::make_unique<os_crypt_async::KeychainKeyProvider>()));
//          }
//        #endif  // BUILDFLAG(IS_MAC)
//
//      to:
//        #if BUILDFLAG(IS_MAC)
//          {
//            base::FilePath user_data_dir;
//            base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
//            providers.emplace_back(std::make_pair(
//                /*precedence=*/15u,
//                std::make_unique<maho::MahoFileKeyProvider>(user_data_dir)));
//          }
//        #endif  // BUILDFLAG(IS_MAC)
//
//    Only MahoFileKeyProvider is registered on macOS. KeychainKeyProvider is
//    removed entirely to prevent the "Tags must not overlap" crash caused by
//    both providers returning tag "v10".  The file-based provider at
//    precedence 15 preserves the sync-compatible v10 data format.
//
// 3. Add GN dep in chrome/browser/BUILD.gn:
//      "//maho/browser:maho_os_crypt_key_provider"
//
// This ensures the injected include/registration compiles on macOS.
