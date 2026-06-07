// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_tab_id_helper.h"

#include "base/no_destructor.h"
#include "base/uuid.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoTabIdHelper);

namespace {
std::string& GetPendingRestoredTabId() {
  static base::NoDestructor<std::string> id;
  return *id;
}
}  // namespace

// static
void MahoTabIdHelper::SetPendingRestoredTabId(const std::string& id) {
  GetPendingRestoredTabId() = id;
}

MahoTabIdHelper::MahoTabIdHelper(content::WebContents* web_contents)
    : content::WebContentsUserData<MahoTabIdHelper>(*web_contents) {
  std::string& pending = GetPendingRestoredTabId();
  if (!pending.empty()) {
    stable_tab_id_ = pending;
    pending.clear();
  } else {
    stable_tab_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }
}

MahoTabIdHelper::~MahoTabIdHelper() = default;

void MahoTabIdHelper::SetRestoredTabId(const std::string& id) {
  if (!id.empty()) {
    stable_tab_id_ = id;
    has_been_announced_ = false;
  }
}
