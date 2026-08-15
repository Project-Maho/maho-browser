// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_MAIL_COMMAND_CATALOG_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_MAIL_COMMAND_CATALOG_H_

#include <array>
#include <string_view>

namespace maho {

struct MahoMailCommand {
  std::string_view action_id;
  std::string_view path;
};

inline constexpr std::array<MahoMailCommand, 7> kMahoMailCommands = {{
    {"open_mail", ""},
    {"compose_mail", "?view=compose"},
    {"search_mail", "?view=search"},
    {"mail_inbox", "?folder=inbox"},
    {"mail_sent", "?folder=sent"},
    {"mail_drafts", "?folder=drafts"},
    {"mail_starred", "?folder=starred"},
}};

constexpr const MahoMailCommand* FindMahoMailCommand(
    std::string_view action_id) {
  for (const MahoMailCommand& command : kMahoMailCommands) {
    if (command.action_id == action_id) {
      return &command;
    }
  }
  return nullptr;
}

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_MAIL_COMMAND_CATALOG_H_
