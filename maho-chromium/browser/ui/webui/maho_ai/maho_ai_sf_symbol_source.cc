// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_sf_symbol_source.h"

#include <stdint.h>

#include "base/functional/callback.h"
#include "base/memory/ref_counted_memory.h"
#include "build/buildflag.h"

#if BUILDFLAG(IS_MAC)
#error "This file must not be compiled on macOS; use maho_ai_sf_symbol_source.mm instead."
#endif

namespace maho_ai {

namespace {

scoped_refptr<base::RefCountedMemory> MakeEmptyPNG() {
  static const uint8_t kTransparent1x1[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
      0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x62, 0x00, 0x00, 0x00, 0x02,
      0x00, 0x01, 0xe2, 0x21, 0xbc, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
      0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  return base::MakeRefCounted<base::RefCountedStaticMemory>(kTransparent1x1);
}

}  // namespace

bool ShouldHandleSFSymbolRequest(const std::string& path) {
  return false;
}

void HandleSFSymbolRequest(
    const std::string& path,
    base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> callback) {
  std::move(callback).Run(MakeEmptyPNG());
}

}  // namespace maho_ai
