// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_SF_SYMBOL_SOURCE_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_SF_SYMBOL_SOURCE_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/ref_counted.h"
#include "base/memory/ref_counted_memory.h"

namespace maho_ai {

bool ShouldHandleSFSymbolRequest(const std::string& path);

void HandleSFSymbolRequest(
    const std::string& path,
    base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> callback);

inline constexpr char kSFSymbolPathPrefix[] = "sf-symbol/";

}  // namespace maho_ai

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_SF_SYMBOL_SOURCE_H_
