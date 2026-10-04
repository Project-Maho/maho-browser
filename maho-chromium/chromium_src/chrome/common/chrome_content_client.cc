// Copyright 2026 Maho Browser. All rights reserved.
// Reference documentation for upstream chrome/common/chrome_content_client.cc overrides.
//
// Maho registers `maho` (maho::kMahoUIScheme) as a standard scheme in
// ChromeContentClient::AddAdditionalSchemes:
//   schemes->standard_schemes.push_back(maho::kMahoUIScheme);
//
// No extra privileges (secure, CORS, service worker, savable, local, etc.) are granted.
