// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_tab_id_helper.h"

#include "base/uuid.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoTabIdHelper);

MahoTabIdHelper::MahoTabIdHelper(content::WebContents* web_contents)
    : content::WebContentsUserData<MahoTabIdHelper>(*web_contents),
      stable_tab_id_(
          base::Uuid::GenerateRandomV4().AsLowercaseString()) {}

MahoTabIdHelper::~MahoTabIdHelper() = default;

void MahoTabIdHelper::SetRestoredTabId(const std::string& id) {
  if (!id.empty()) {
    stable_tab_id_ = id;
  }
}
