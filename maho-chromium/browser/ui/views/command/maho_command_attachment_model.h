// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ATTACHMENT_MODEL_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ATTACHMENT_MODEL_H_

#include <string>
#include <vector>

#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"

namespace maho {

class MahoCommandAttachmentModel {
 public:
  enum class Kind { kCurrentTab, kOpenTab, kLocalFile };
  struct Chip {
    Kind kind;
    int tab_id = -1;
    std::string file_token;
    std::string label;
  };

  static constexpr size_t kMaxChips = 5;

  bool AddCurrentTab(std::string label);
  bool AddOpenTab(int tab_id, std::string label);
  bool AddLocalFile(std::string browser_token, std::string label);
  bool Remove(size_t index);
  bool RemoveLastOnBackspace(bool input_empty) const;
  static bool ShouldRemoveLastOnBackspace(bool input_empty,
                                         bool has_chips) {
    return input_empty && has_chips;
  }
  void RemoveLast();
  std::vector<maho_ai::mojom::ContextAttachmentPtr> ToMojom() const;

  const std::vector<Chip>& chips() const { return chips_; }

 private:
  bool Add(Chip chip);
  std::vector<Chip> chips_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ATTACHMENT_MODEL_H_
