// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_SELECTION_ASK_QUERY_H_
#define MAHO_BROWSER_AI_MAHO_SELECTION_ASK_QUERY_H_

#include <string>
#include <string_view>

namespace maho {

std::string BuildSelectionAskQuery(std::string_view page_url,
                                   std::string_view selected_text);

}  // namespace maho

#endif  // MAHO_BROWSER_AI_MAHO_SELECTION_ASK_QUERY_H_
