// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_attachment_model.h"

#include <utility>

namespace maho {

MahoCommandAttachmentModel::MahoCommandAttachmentModel() = default;
MahoCommandAttachmentModel::~MahoCommandAttachmentModel() = default;

bool MahoCommandAttachmentModel::AddCurrentTab(std::string label) {
  return Add({Kind::kCurrentTab, -1, {}, std::move(label)});
}

bool MahoCommandAttachmentModel::AddOpenTab(int tab_id, std::string label) {
  if (tab_id < 0)
    return false;
  for (const auto& chip : chips_) {
    if ((chip.kind == Kind::kCurrentTab || chip.kind == Kind::kOpenTab) &&
        chip.tab_id == tab_id) {
      return false;
    }
  }
  return Add({Kind::kOpenTab, tab_id, {}, std::move(label)});
}

bool MahoCommandAttachmentModel::AddLocalFile(std::string browser_token,
                                               std::string label) {
  if (browser_token.empty())
    return false;
  for (const auto& chip : chips_) {
    if (chip.kind == Kind::kLocalFile && chip.file_token == browser_token)
      return false;
  }
  return Add({Kind::kLocalFile, -1, std::move(browser_token),
              std::move(label)});
}

bool MahoCommandAttachmentModel::Add(Chip chip) {
  if (chips_.size() >= kMaxChips || chip.label.empty())
    return false;
  chips_.push_back(std::move(chip));
  return true;
}

bool MahoCommandAttachmentModel::Remove(size_t index) {
  if (index >= chips_.size())
    return false;
  chips_.erase(chips_.begin() + index);
  return true;
}

bool MahoCommandAttachmentModel::RemoveLastOnBackspace(
    bool input_empty) const {
  return input_empty && !chips_.empty();
}

void MahoCommandAttachmentModel::RemoveLast() {
  if (!chips_.empty())
    chips_.pop_back();
}

std::vector<maho_ai::mojom::ContextAttachmentPtr>
MahoCommandAttachmentModel::ToMojom() const {
  std::vector<maho_ai::mojom::ContextAttachmentPtr> result;
  result.reserve(chips_.size());
  for (const auto& chip : chips_) {
    auto attachment = maho_ai::mojom::ContextAttachment::New();
    attachment->kind = chip.kind == Kind::kCurrentTab
                           ? maho_ai::mojom::ContextSourceKind::kCurrentPage
                           : chip.kind == Kind::kOpenTab
                                 ? maho_ai::mojom::ContextSourceKind::kOpenTab
                                 : maho_ai::mojom::ContextSourceKind::kLocalFile;
    attachment->tab_id = chip.tab_id;
    attachment->label = chip.label;
    attachment->url.clear();
    if (chip.kind == Kind::kLocalFile)
      attachment->file_token = chip.file_token;
    result.push_back(std::move(attachment));
  }
  return result;
}

}  // namespace maho
