// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_space_context_menu.h"

#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "maho/browser/ui/theme/maho_space_theme_io.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "chrome/browser/ui/dialogs/browser_dialogs.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/space_config/maho_space_config_dialog.h"
#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"  // nogncheck
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/models/dialog_model.h"
#include "maho/browser/ui/webui/maho_space_config/maho_space_config.mojom.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "url/gurl.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/menus/simple_menu_model.h"

namespace maho {

std::string DispatchSpaceContextMenuCoreEventForTesting(
    MahoCore* core,
    const std::string& event_json) {
  if (!core) {
    return std::string();
  }
  char* result = maho_core_handle_event(core, event_json.c_str());
  if (!result) {
    return std::string();
  }
  std::string updates(result);
  maho_string_free(result);
  InvalidateSidebarCoreCacheForUpdatesJson(updates);
  return updates;
}

void RecolorSpaceContextMenuForTesting(MahoCore* core,
                                       const std::string& space_id,
                                       const std::string& color) {
  if (!core) {
    return;
  }
  base::DictValue color_dict;
  color_dict.Set("color", color);
  std::string color_json;
  base::JSONWriter::Write(color_dict, &color_json);
  maho_core_recolor_space(core, space_id.c_str(), color_json.c_str());
  SidebarCacheInvalidation invalidation;
  invalidation.fragments = SidebarCoreFragment::kFooter;
  InvalidateSidebarCoreCache(invalidation);
}

}  // namespace maho

namespace {

constexpr char kDefaultSpaceName[] = "Space";
constexpr char kCreateFolderKind[] = "create_folder";
constexpr char kSpaceIconHome[] = "🏠";
constexpr char kSpaceIconWork[] = "💼";
constexpr char kSpaceIconArt[] = "🎨";
constexpr char kSpaceIconGames[] = "🎮";
constexpr char kSpaceIconBooks[] = "📚";
constexpr char kSpaceIconMusic[] = "🎵";
constexpr char kSpaceIconStar[] = "🌟";
constexpr char kSpaceIconLightning[] = "⚡";

constexpr char kSpaceThemeRed[] = "#e94560";
constexpr char kSpaceThemeNavy[] = "#0f3460";
constexpr char kSpaceThemePurple[] = "#533483";
constexpr char kSpaceThemeTeal[] = "#16c79a";
constexpr char kSpaceThemeAmber[] = "#f5a623";
constexpr char kSpaceThemeBlue[] = "#3498db";
constexpr char kSpaceThemeOrange[] = "#e67e22";
constexpr char kSpaceThemeGreen[] = "#2ecc71";

const char* GetIconForCommand(int command_id) {
  switch (command_id) {
    case IDC_MAHO_SPACE_ICON_HOME:
      return kSpaceIconHome;
    case IDC_MAHO_SPACE_ICON_WORK:
      return kSpaceIconWork;
    case IDC_MAHO_SPACE_ICON_ART:
      return kSpaceIconArt;
    case IDC_MAHO_SPACE_ICON_GAMES:
      return kSpaceIconGames;
    case IDC_MAHO_SPACE_ICON_BOOKS:
      return kSpaceIconBooks;
    case IDC_MAHO_SPACE_ICON_MUSIC:
      return kSpaceIconMusic;
    case IDC_MAHO_SPACE_ICON_STAR:
      return kSpaceIconStar;
    case IDC_MAHO_SPACE_ICON_LIGHTNING:
      return kSpaceIconLightning;
    default:
      return nullptr;
  }
}

const char* GetThemeForCommand(int command_id) {
  switch (command_id) {
    case IDC_MAHO_SPACE_THEME_RED:
      return kSpaceThemeRed;
    case IDC_MAHO_SPACE_THEME_NAVY:
      return kSpaceThemeNavy;
    case IDC_MAHO_SPACE_THEME_PURPLE:
      return kSpaceThemePurple;
    case IDC_MAHO_SPACE_THEME_TEAL:
      return kSpaceThemeTeal;
    case IDC_MAHO_SPACE_THEME_AMBER:
      return kSpaceThemeAmber;
    case IDC_MAHO_SPACE_THEME_BLUE:
      return kSpaceThemeBlue;
    case IDC_MAHO_SPACE_THEME_ORANGE:
      return kSpaceThemeOrange;
    case IDC_MAHO_SPACE_THEME_GREEN:
      return kSpaceThemeGreen;
    default:
      return nullptr;
  }
}

std::string GetSpaceName(MahoCore* core, const std::string& space_id) {
  if (!core) {
    return kDefaultSpaceName;
  }

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return kDefaultSpaceName;
  }

  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return kDefaultSpaceName;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    if (id && *id == space_id && name && !name->empty()) {
      return *name;
    }
  }

  return kDefaultSpaceName;
}

void CreateFolderOnPool(const std::string& space_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  base::DictValue event;
  event.Set("kind", kCreateFolderKind);
  event.Set("space_id", space_id);
  std::string event_json;
  base::JSONWriter::Write(event, &event_json);
  maho::DispatchSpaceContextMenuCoreEventForTesting(core, event_json);
}

void ChangeSpaceIconOnPool(const std::string& space_id,
                           const std::string& icon) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  // `update_space_config` is the core's icon seam (SpaceConfigUpdate.icon).
  // There is no `set_space_icon` ShellEvent; that kind was silently dropped.
  base::DictValue changes;
  changes.Set("spaceId", space_id);
  changes.Set("icon", icon);
  base::DictValue event;
  event.Set("kind", "update_space_config");
  event.Set("changes", std::move(changes));
  std::string event_json;
  base::JSONWriter::Write(event, &event_json);
  maho::DispatchSpaceContextMenuCoreEventForTesting(core, event_json);

  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce([]() {
        if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance()) {
          bridge->NotifyChanged(/*is_structural=*/true);
        }
      }));
}

void RecolorSpaceOnPool(const std::string& space_id,
                        const std::string& color) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  maho::RecolorSpaceContextMenuForTesting(core, space_id, color);
}

void DeleteSpaceOnPool(const std::string& space_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_delete_space(core, space_id.c_str());
  maho::SidebarCacheInvalidation invalidation;
  invalidation.fragments = maho::SidebarCoreFragment::kAll;
  invalidation.space_ids.push_back(space_id);
  maho::InvalidateSidebarCoreCache(invalidation);

  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce([]() {
        auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
        if (bridge) {
          bridge->NotifyChanged(/*is_structural=*/true);
        }
      }));
}

}  // namespace

MahoSpaceContextMenu::MahoSpaceContextMenu(Browser* browser,
                                           const std::string& space_id)
    : browser_(browser), space_id_(space_id) {}

MahoSpaceContextMenu::~MahoSpaceContextMenu() = default;

std::unique_ptr<ui::SimpleMenuModel> MahoSpaceContextMenu::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);

  model->AddItem(IDC_MAHO_SPACE_RENAME, u"Rename Space");
  model->AddItem(IDC_MAHO_SPACE_CHANGE_ICON, u"Change Space Icon");
  model->AddItem(IDC_MAHO_SPACE_EDIT_THEME, u"Edit Theme Color");
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItem(IDC_MAHO_SPACE_NEW_FOLDER, u"New Folder");
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItem(IDC_MAHO_SPACE_SHARE, u"Share Space");
  model->AddItem(IDC_MAHO_SPACE_EXPORT_THEME, u"Export Theme…");
  model->AddItem(IDC_MAHO_SPACE_IMPORT_THEME, u"Import Theme…");
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItem(IDC_MAHO_SPACE_DELETE, u"Delete Space");

  return model;
}

void MahoSpaceContextMenu::ShareSpace() {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(
          [](const std::string& space_id) {
            MahoCore* core = maho::GetCore();
            if (!core) {
              return;
            }

            const std::string space_name = GetSpaceName(core, space_id);
            char* collection_json = maho_core_create_shared_collection(
                core, space_id.c_str(), space_name.c_str());
            if (!collection_json) {
              return;
            }

            std::string collection_result(collection_json);
            maho_string_free(collection_json);
            std::optional<base::Value> parsed = base::JSONReader::Read(
                collection_result, base::JSON_PARSE_RFC);
            if (!parsed || !parsed->is_dict()) {
              return;
            }

            const auto& dict = parsed->GetDict();
            const std::string* collection_id = dict.FindString("id");
            if (!collection_id) {
              collection_id = dict.FindString("collection_id");
            }
            if (!collection_id) {
              return;
            }

            char* share_link_json =
                maho_core_get_share_link(core, collection_id->c_str());
            if (!share_link_json) {
              return;
            }
            std::string share_link_result(share_link_json);
            maho_string_free(share_link_json);

            std::optional<base::Value> link_parsed =
                base::JSONReader::Read(share_link_result, base::JSON_PARSE_RFC);
            std::string link_to_copy;
            if (link_parsed && link_parsed->is_dict()) {
              const std::string* url =
                  link_parsed->GetDict().FindString("url");
              if (!url) {
                url = link_parsed->GetDict().FindString("link");
              }
              if (url) {
                link_to_copy = *url;
              }
            } else if (link_parsed && link_parsed->is_string()) {
              link_to_copy = link_parsed->GetString();
            } else {
              link_to_copy = share_link_result;
            }

            if (!link_to_copy.empty()) {
              ui::ScopedClipboardWriter writer(
                  ui::ClipboardBuffer::kCopyPaste);
              writer.WriteText(base::UTF8ToUTF16(link_to_copy));
            }
          },
          space_id_));
}

void MahoSpaceContextMenu::ExportTheme() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  pending_file_op_ = PendingFileOp::kExportTheme;
  select_file_dialog_ = ui::SelectFileDialog::Create(this, nullptr);

  ui::SelectFileDialog::FileTypeInfo file_type_info;
  file_type_info.extensions.push_back({FILE_PATH_LITERAL("mahotheme")});
  file_type_info.extensions.push_back({FILE_PATH_LITERAL("json")});
  file_type_info.extension_description_overrides.push_back(
      u"Maho Theme (.mahotheme)");
  file_type_info.extension_description_overrides.push_back(u"JSON (.json)");

  const std::string space_name = GetSpaceName(core, space_id_);
  base::FilePath default_path(
      base::FilePath::FromUTF8Unsafe(space_name + ".mahotheme"));

  select_file_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_SAVEAS_FILE, u"Export Theme", default_path,
      &file_type_info, 1, FILE_PATH_LITERAL("mahotheme"),
      browser_->GetWindow()->GetNativeWindow());
}

void MahoSpaceContextMenu::ImportTheme() {
  pending_file_op_ = PendingFileOp::kImportTheme;
  select_file_dialog_ = ui::SelectFileDialog::Create(this, nullptr);

  ui::SelectFileDialog::FileTypeInfo file_type_info;
  file_type_info.extensions.push_back(
      {FILE_PATH_LITERAL("mahotheme"), FILE_PATH_LITERAL("json")});
  file_type_info.extension_description_overrides.push_back(
      u"Maho Theme / JSON");

  select_file_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_OPEN_FILE, u"Import Theme",
      base::FilePath(), &file_type_info, 1, FILE_PATH_LITERAL("mahotheme"),
      browser_->GetWindow()->GetNativeWindow());
}

void MahoSpaceContextMenu::FileSelected(const ui::SelectedFileInfo& file,
                                         int index) {
  const base::FilePath path = file.path();
  const PendingFileOp op = pending_file_op_;
  const std::string space_id = space_id_;
  pending_file_op_ = PendingFileOp::kNone;

  if (op == PendingFileOp::kExportTheme) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
        base::BindOnce(
            [](const std::string& space_id, const base::FilePath& path) {
              MahoCore* core = maho::GetCore();
              if (!core || !maho_theme::ExportSpaceThemeToFile(core, space_id, path)) {
                LOG(WARNING) << "ExportTheme: failed to write file";
              }
            },
            space_id, path));
  } else if (op == PendingFileOp::kImportTheme) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
        base::BindOnce(&maho_theme::ReadValidatedThemeJson, path),
        base::BindOnce(
            [](const std::string& space_id,
               std::optional<std::string> validated_json) {
               if (!validated_json) {
                 return;
               }
               MahoCore* core = maho::GetCore();
               if (!core) {
                 return;
               }
               if (!maho_theme::ApplyThemeJsonToSpace(core, space_id,
                                                     *validated_json)) {
                 return;
               }
               MahoSpaceThemeState::ChangedSpaceIds changed_spaces =
                   MahoSpaceThemeState::UpdateFromCore();
               if (changed_spaces.empty()) {
                 changed_spaces.push_back(space_id);
               }
               for (Browser* affected_browser :
                    maho::MahoSpaceProfileBridge::GetInstance()
                        ->GetBrowsersForSpaces(changed_spaces)) {
                 maho::MahoSidebarView::RefreshSpaceThemeForBrowser(
                     affected_browser);
               }
            },
            space_id));
  }
}

void MahoSpaceContextMenu::FileSelectionCanceled() {
  pending_file_op_ = PendingFileOp::kNone;
}

void MahoSpaceContextMenu::ExecuteCommand(int command_id, int event_flags) {
  (void)event_flags;
  switch (command_id) {
    case IDC_MAHO_SPACE_RENAME:
      MahoSpaceConfigDialog::Open(
          browser_->GetProfile(), browser_->GetWindow()->GetNativeWindow(),
          space_id_, maho_space_config::mojom::InitialFocus::kName);
      break;
    case IDC_MAHO_SPACE_CHANGE_ICON:
      MahoSpaceConfigDialog::Open(
          browser_->GetProfile(), browser_->GetWindow()->GetNativeWindow(),
          space_id_, maho_space_config::mojom::InitialFocus::kIcon);
      break;
    case IDC_MAHO_SPACE_EDIT_THEME: {
      MahoCore* core = maho::GetCore();
      if (!core) {
        break;
      }
      std::string initial_json =
          maho_theme::SerializeSpaceThemeJson(core, space_id_)
              .value_or(std::string());
      MahoSpaceThemePickerDialog::Open(
          browser_, initial_json,
          base::BindOnce(
              [](std::string space_id,
                 const std::string& committed_theme_json,
                 bool cancelled) {
                if (cancelled || committed_theme_json.empty()) {
                  return;
                }
                MahoCore* core = maho::GetCore();
                if (!core) {
                  return;
                }
                if (!maho_theme::ApplyThemeJsonToSpace(core, space_id,
                                                      committed_theme_json)) {
                  return;
                }
                std::vector<std::string> changed_spaces =
                    MahoSpaceThemeState::UpdateFromCore();
                if (changed_spaces.empty()) {
                  changed_spaces.push_back(space_id);
                }
                for (Browser* affected_browser :
                     maho::MahoSpaceProfileBridge::GetInstance()
                         ->GetBrowsersForSpaces(changed_spaces)) {
                  maho::MahoSidebarView::RefreshSpaceThemeForBrowser(
                      affected_browser);
                }
              },
              space_id_));
      break;
    }
    case IDC_MAHO_SPACE_DELETE: {
      if (!browser_) {
        break;
      }
      MahoCore* core = maho::GetCore();
      if (!core) {
        break;
      }
      std::string space_name = GetSpaceName(core, space_id_);
      std::u16string name_16 = base::UTF8ToUTF16(space_name);

      ui::DialogModel::Builder builder;
      builder.SetTitle(u"Delete Space?")
          .AddParagraph(ui::DialogModelLabel(
              u"Are you sure you want to delete \"" + name_16 +
              u"\"? This will also close all tabs parked in this space."))
          .AddOkButton(
              base::BindOnce(
                  [](std::string space_id) {
                    base::ThreadPool::PostTask(
                        FROM_HERE,
                        {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
                        base::BindOnce(&DeleteSpaceOnPool, space_id));
                  },
                  space_id_),
              ui::DialogModel::Button::Params()
                  .SetLabel(u"Delete")
                  .SetStyle(ui::ButtonStyle::kProminent))
          .AddCancelButton(base::DoNothing());

      chrome::ShowBrowserModal(browser_, builder.Build());
      break;
    }
    case IDC_MAHO_SPACE_NEW_FOLDER:
      base::ThreadPool::PostTask(
          FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
          base::BindOnce(&CreateFolderOnPool, space_id_));
      break;
    case IDC_MAHO_SPACE_SHARE:
      ShareSpace();
      break;
    case IDC_MAHO_SPACE_EXPORT_THEME:
      ExportTheme();
      break;
    case IDC_MAHO_SPACE_IMPORT_THEME:
      ImportTheme();
      break;
    case IDC_MAHO_SPACE_ICON_HOME:
    case IDC_MAHO_SPACE_ICON_WORK:
    case IDC_MAHO_SPACE_ICON_ART:
    case IDC_MAHO_SPACE_ICON_GAMES:
    case IDC_MAHO_SPACE_ICON_BOOKS:
    case IDC_MAHO_SPACE_ICON_MUSIC:
    case IDC_MAHO_SPACE_ICON_STAR:
    case IDC_MAHO_SPACE_ICON_LIGHTNING: {
      const char* icon = GetIconForCommand(command_id);
      if (icon) {
        base::ThreadPool::PostTask(
            FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
            base::BindOnce(&ChangeSpaceIconOnPool, space_id_, std::string(icon)));
      }
      break;
    }
    case IDC_MAHO_SPACE_THEME_RED:
    case IDC_MAHO_SPACE_THEME_NAVY:
    case IDC_MAHO_SPACE_THEME_PURPLE:
    case IDC_MAHO_SPACE_THEME_TEAL:
    case IDC_MAHO_SPACE_THEME_AMBER:
    case IDC_MAHO_SPACE_THEME_BLUE:
    case IDC_MAHO_SPACE_THEME_ORANGE:
    case IDC_MAHO_SPACE_THEME_GREEN: {
      const char* color = GetThemeForCommand(command_id);
      if (color) {
        base::ThreadPool::PostTask(
            FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
            base::BindOnce(&RecolorSpaceOnPool, space_id_, std::string(color)));
      }
      break;
    }
    default:
      break;
  }
}

bool MahoSpaceContextMenu::IsCommandIdEnabled(int command_id) const {
  return true;
}

bool MahoSpaceContextMenu::IsCommandIdChecked(int command_id) const {
  return false;
}
