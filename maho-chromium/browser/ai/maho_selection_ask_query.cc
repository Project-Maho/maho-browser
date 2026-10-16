// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_selection_ask_query.h"

#include <algorithm>

#include "base/strings/string_util.h"
#include "maho/browser/ai/maho_ai_security_utils.h"

namespace maho {
namespace {

constexpr size_t kMaxSelectionBytes = 4000;

}  // namespace

std::string BuildSelectionAskQuery(std::string_view page_url,
                                   std::string_view selected_text) {
  const size_t bounded_size = std::min(selected_text.size(), kMaxSelectionBytes);
  selected_text = selected_text.substr(0, bounded_size);
  while (!selected_text.empty() &&
         (static_cast<unsigned char>(selected_text.back()) & 0xc0) == 0x80) {
    selected_text.remove_suffix(1);
  }
  if (!selected_text.empty()) {
    size_t lead_index = selected_text.size() - 1;
    while (lead_index > 0 &&
           (static_cast<unsigned char>(selected_text[lead_index]) & 0xc0) ==
               0x80) {
      --lead_index;
    }
    const auto lead = static_cast<unsigned char>(selected_text[lead_index]);
    const size_t expected_bytes =
        lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2
                      : (lead & 0xf0) == 0xe0 ? 3
                                             : (lead & 0xf8) == 0xf0 ? 4 : 1;
    if (selected_text.size() - lead_index < expected_bytes) {
      selected_text = selected_text.substr(0, lead_index);
    }
  }

  std::string query = "Explain or answer about this text from ";
  query.append(page_url);
  query.append(":\n");
  query.append(maho::ai_security::WrapUntrusted(selected_text));
  return query;
}

}  // namespace maho
