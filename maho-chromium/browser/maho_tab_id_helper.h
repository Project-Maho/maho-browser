// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_TAB_ID_HELPER_H_
#define MAHO_BROWSER_MAHO_TAB_ID_HELPER_H_

#include <string>

#include "content/public/browser/web_contents_user_data.h"

class MahoTabIdHelper
    : public content::WebContentsUserData<MahoTabIdHelper> {
 public:
  ~MahoTabIdHelper() override;

  MahoTabIdHelper(const MahoTabIdHelper&) = delete;
  MahoTabIdHelper& operator=(const MahoTabIdHelper&) = delete;

  // The restart-stable tab identifier (UUID v4 string).
  const std::string& stable_tab_id() const { return stable_tab_id_; }

  // Set the next stable ID to be assigned to a new WebContents.
  static void SetPendingRestoredTabId(const std::string& id);
  static std::string PopPendingRestoredTabIdForTesting();

  // Replace the ID with a previously persisted value (session restore).
  void SetRestoredTabId(const std::string& id);

  bool has_been_announced() const { return has_been_announced_; }
  void set_has_been_announced(bool announced) { has_been_announced_ = announced; }

  // Process-scoped handle handed to MCP clients. It is bound to this
  // WebContents for its lifetime, never to the mutable stable tab id.
  int mcp_id() const { return mcp_id_; }
  void set_mcp_id(int id) { mcp_id_ = id; }

  // Session extra_data key used for persist/restore.
  static constexpr char kExtraDataKey[] = "maho_stable_tab_id";

 private:
  friend class content::WebContentsUserData<MahoTabIdHelper>;
  explicit MahoTabIdHelper(content::WebContents* web_contents);

  std::string stable_tab_id_;
  bool has_been_announced_ = false;
  int mcp_id_ = 0;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // MAHO_BROWSER_MAHO_TAB_ID_HELPER_H_
