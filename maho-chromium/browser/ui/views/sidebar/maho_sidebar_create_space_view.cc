// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"

#include <string>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace maho {

namespace {

constexpr int kCreatePanelCornerRadius = 12;
constexpr int kFieldHeight = 28;

std::string CreateSpaceOnPool(const std::string& name) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::string();
  }

  base::DictValue color_dict;
  color_dict.Set("color", "#5F8BFF");
  std::string color_json;
  base::JSONWriter::Write(color_dict, &color_json);

  char* result =
      maho_core_create_space(core, name.c_str(), color_json.c_str(), nullptr);
  std::string new_id;
  if (result) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(result, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      const std::string* parsed_id = parsed->GetDict().FindString("id");
      if (parsed_id) {
        new_id = *parsed_id;
      }
    }
    maho_string_free(result);
  }
  return new_id;
}

}  // namespace

BEGIN_METADATA(MahoSidebarCreateSpaceView)
END_METADATA

MahoSidebarCreateSpaceView::MahoSidebarCreateSpaceView(Browser* browser)
    : browser_(browser) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 8), 6));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface1,
                                                    kCreatePanelCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kCreatePanelCornerRadius,
                                            ui::kColorSysSurfaceVariant));

  auto* title_label =
      AddChildView(std::make_unique<views::Label>(u"New Space"));
  title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
  title_label->SetAutoColorReadabilityEnabled(false);
  title_label->SetEnabledColor(ui::kColorSysOnSurface);

  name_field_ = AddChildView(std::make_unique<views::Textfield>());
  name_field_->SetPlaceholderText(u"Space name");
  name_field_->SetAccessibleName(u"Space name");
  name_field_->SetPreferredSize(gfx::Size(0, kFieldHeight));
  name_field_->set_controller(this);

  auto* button_row = AddChildView(std::make_unique<views::View>());
  auto* button_layout =
      button_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  button_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kEnd);

  cancel_button_ = button_row->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&MahoSidebarCreateSpaceView::OnCancelPressed,
                              weak_factory_.GetWeakPtr()),
          u"Cancel"));
  cancel_button_->SetStyle(ui::ButtonStyle::kText);
  cancel_button_->SetAccessibleName(u"Cancel create space");

  create_button_ = button_row->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&MahoSidebarCreateSpaceView::OnCreatePressed,
                              weak_factory_.GetWeakPtr()),
          u"Create Space"));
  create_button_->SetStyle(ui::ButtonStyle::kTonal);
  create_button_->SetAccessibleName(u"Create Space");
}

MahoSidebarCreateSpaceView::~MahoSidebarCreateSpaceView() = default;

void MahoSidebarCreateSpaceView::RequestNameFocus() {
  if (!name_field_) {
    return;
  }
  name_field_->RequestFocus();
  name_field_->SelectAll(false);
}

bool MahoSidebarCreateSpaceView::HandleKeyEvent(views::Textfield* sender,
                                                const ui::KeyEvent& event) {
  if (sender != name_field_ || event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  switch (event.key_code()) {
    case ui::VKEY_RETURN:
      OnCreatePressed();
      return true;
    case ui::VKEY_ESCAPE:
      OnCancelPressed();
      return true;
    default:
      return false;
  }
}

void MahoSidebarCreateSpaceView::OnCreatePressed() {
  if (!name_field_) {
    return;
  }
  std::string name = base::UTF16ToUTF8(name_field_->GetText());
  if (name.empty()) {
    return;
  }

  create_button_->SetEnabled(false);

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&CreateSpaceOnPool, name),
      base::BindOnce(&MahoSidebarCreateSpaceView::OnSpaceCreated,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarCreateSpaceView::OnCancelPressed() {
  SetVisible(false);
}

void MahoSidebarCreateSpaceView::OnSpaceCreated(
    const std::string& new_space_id) {
  if (!new_space_id.empty()) {
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    if (bridge) {
      bridge->RegisterSpace(new_space_id,
                            base::FilePath::FromASCII("Default"));
    }
  }
  MahoSpaceThemeState::UpdateFromCore();
  ui::ColorProviderManager::Get().ResetColorProviderCache();

  if (name_field_) {
    name_field_->SetText(u"");
  }
  if (create_button_) {
    create_button_->SetEnabled(true);
  }
  SetVisible(false);
}

}  // namespace maho
