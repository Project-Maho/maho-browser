// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_top_bar_view.h"

#include <algorithm>
#include <memory>
#include <string_view>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "build/buildflag.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/components/constants/webui_url_constants.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "ui/base/models/image_model.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/controls/label.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ai/maho_ai_llm_client.h"
#include "maho/browser/ai/maho_extract_prompt_messages.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/window_show_state.mojom.h"

namespace maho {

namespace {

// Extra margin below the top-bar row (currently zero; the kTopBarInsets bottom
// value handles separation from the search pill).
constexpr int kTopBarBottomMarginDp = 0;
// Internal padding inside each button cluster (leading and nav).
constexpr int kTopBarClusterHorizontalInsetDp = 0;
constexpr int kTopBarClusterVerticalInsetDp = 0;
// Left margin on the sidebar-toggle button, shifting it inward from the
// traffic-light spacer edge for optical alignment with macOS window controls.
constexpr int kSidebarToggleLeftMarginDp = 6;

class MahoTidySuggestionDialogView : public views::DialogDelegate {
 public:
  MahoTidySuggestionDialogView(std::string space_id, base::Value folders)
      : space_id_(std::move(space_id)), folders_(std::move(folders)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
               static_cast<int>(ui::mojom::DialogButton::kCancel));
    SetModalType(ui::mojom::ModalType::kWindow);
    SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kOk));
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Apply");
    SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");

    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 12));

    auto* desc_label = contents->AddChildView(std::make_unique<views::Label>(
        u"Tidy suggestions for your space:"));
    desc_label->SetFontList(views::Label::GetDefaultFontList().Derive(
        1, gfx::Font::NORMAL, gfx::Font::Weight::BOLD));
    desc_label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);

    // Build tab map
    std::map<std::string, std::string> tab_titles;
    MahoCore* core = maho::GetCore();
    if (core) {
      char* tabs_json_c = maho_core_get_space_tabs(core, space_id_.c_str(), nullptr);
      if (tabs_json_c) {
        std::string json(tabs_json_c);
        maho_string_free(tabs_json_c);
        std::optional<base::Value> parsed_tabs =
            base::JSONReader::Read(json, base::JSON_PARSE_RFC);
        if (parsed_tabs && parsed_tabs->is_list()) {
          for (const auto& tab_val : parsed_tabs->GetList()) {
            const auto* tab_dict = tab_val.GetIfDict();
            if (tab_dict) {
              const std::string* id = tab_dict->FindString("id");
              const std::string* title = tab_dict->FindString("title");
              if (id && title) {
                tab_titles[*id] = *title;
              }
            }
          }
        }
      }
    }

    // Add list of folders & tabs
    for (const auto& folder_val : folders_.GetList()) {
      const auto* folder_dict = folder_val.GetIfDict();
      if (!folder_dict) continue;

      const std::string* name = folder_dict->FindString("name");
      const auto* tab_ids = folder_dict->FindList("tab_ids");
      if (!name || !tab_ids) continue;

      auto* folder_label = contents->AddChildView(std::make_unique<views::Label>(
          base::UTF8ToUTF16("📁 " + *name)));
      folder_label->SetFontList(views::Label::GetDefaultFontList().Derive(
          0, gfx::Font::NORMAL, gfx::Font::Weight::BOLD));
      folder_label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);

      for (const auto& tab_id_val : *tab_ids) {
        const std::string* tab_id = tab_id_val.GetIfString();
        if (!tab_id) continue;

        std::string title = tab_titles.count(*tab_id) ? tab_titles[*tab_id] : *tab_id;
        if (title.length() > 60) {
          title = title.substr(0, 57) + "...";
        }
        auto* tab_label = contents->AddChildView(std::make_unique<views::Label>(
            base::UTF8ToUTF16("  • " + title)));
        tab_label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
      }
    }

    SetContentsView(std::move(contents));
  }

  std::u16string GetWindowTitle() const override {
    return u"Tab Tidy Suggestions";
  }

  bool Accept() override {
    base::DictValue event_dict;
    event_dict.Set("kind", "apply_tidy_tabs");
    event_dict.Set("space_id", space_id_);
    std::string event_json;
    base::JSONWriter::Write(event_dict, &event_json);

    MahoCore* core = maho::GetCore();
    if (core) {
      ::maho::core::HandleEvent(core, event_json.c_str());
      if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
        bridge->NotifyChanged();
      }
    }
    return true;
  }

 private:
  std::string space_id_;
  base::Value folders_;
};

void ShowMahoTidySuggestionDialog(Browser* browser,
                                  const std::string& space_id,
                                  base::Value folders) {
  auto dialog = std::make_unique<MahoTidySuggestionDialogView>(
      space_id, std::move(folders));
  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog), browser->window()->GetNativeWindow(),
      gfx::NativeView());
  if (widget) {
    widget->Show();
  }
}

ui::ImageModel CreateTopBarButtonImage(
    const gfx::VectorIcon& icon,
    int icon_size,
    ui::ColorId color_id) {
  return ui::ImageModel::FromVectorIcon(icon, color_id, icon_size);
}

void ApplyButtonChrome(views::ImageButton* button) {
  button->SetBackground(nullptr);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 0)));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetInstallFocusRingOnFocus(false);
  button->SetRequestFocusOnPress(false);
}

void StyleClusterView(views::View* cluster) {
  cluster->SetBackground(nullptr);
  cluster->SetBorder(nullptr);
}

gfx::Rect GetButtonIconHitRect(const views::ImageButton* button) {
  gfx::Rect icon_hit_rect(button->GetLocalBounds());
  const int icon_hit_width =
      std::min(icon_hit_rect.width(), sidebar_layout::kTopBarIconSizeDp);
  const int icon_hit_height =
      std::min(icon_hit_rect.height(), sidebar_layout::kTopBarIconSizeDp);
  const int icon_hit_x = icon_hit_rect.x() +
                         (icon_hit_rect.width() - icon_hit_width) / 2;
  const int icon_hit_y = icon_hit_rect.y() +
                         (icon_hit_rect.height() - icon_hit_height) / 2;
  return gfx::Rect(icon_hit_x, icon_hit_y, icon_hit_width, icon_hit_height);
}

ui::ColorId GetTopBarButtonColor(bool enabled) {
  return enabled ? ui::kColorSysOnSurface : ui::kColorSysOnSurfaceSubtle;
}

views::ImageButton* AddControlButton(views::View* parent,
                                     views::Button::PressedCallback callback,
                                     const gfx::VectorIcon& icon,
                                     const std::u16string& tooltip,
                                     bool enabled = true) {
  auto button = views::CreateVectorImageButton(std::move(callback));
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      CreateTopBarButtonImage(icon,
                              sidebar_layout::kTopBarIconSizeDp,
                              GetTopBarButtonColor(enabled)));
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  button->SetPreferredSize(gfx::Size(sidebar_layout::kTopBarButtonSizeDp,
                                     sidebar_layout::kTopBarButtonSizeDp));
  button->SetEnabled(enabled);
  views::InstallCircleHighlightPathGenerator(button.get());
  ApplyButtonChrome(button.get());
  return parent->AddChildView(std::move(button));
}

}  // namespace

BEGIN_METADATA(MahoSidebarTopBarView)
END_METADATA

MahoSidebarTopBarView::MahoSidebarTopBarView(Browser* browser)
    : browser_(browser) {
  // Horizontal BoxLayout: [traffic-light spacer | leading cluster | flex | nav cluster]
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(sidebar_layout::kTopBarInsets.top(),
                        sidebar_layout::kTopBarInsets.left(),
                        sidebar_layout::kTopBarInsets.bottom(),
                        sidebar_layout::kTopBarInsets.right()),
      0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  SetProperty(views::kMarginsKey,
              gfx::Insets::TLBR(0, 0, kTopBarBottomMarginDp, 0));

  // Fixed-width spacer reserving room for macOS traffic-light buttons.
  traffic_light_spacer_ = AddChildView(std::make_unique<views::View>());
  traffic_light_spacer_->SetPreferredSize(gfx::Size(
      sidebar_layout::kTrafficLightAlignmentWidthDp,
      sidebar_layout::kTopBarTrafficLightAlignmentHeightDp));

  // Leading cluster: sidebar-toggle (and any future leading actions).
  leading_cluster_ = AddChildView(std::make_unique<views::View>());
  auto* leading_layout =
      leading_cluster_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kTopBarClusterVerticalInsetDp,
                          kTopBarClusterHorizontalInsetDp),
          sidebar_layout::kTopBarButtonSpacingDp));
  leading_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  sidebar_toggle_button_ = AddControlButton(
      leading_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnToggleSidebarPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kPanelLeftIcon, u"Toggle Sidebar");
  sidebar_toggle_button_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, kSidebarToggleLeftMarginDp, 0, 0));

  // Flexible spacer pushes the nav cluster to the trailing edge.
  flex_spacer_ = AddChildView(std::make_unique<views::View>());
  layout->SetFlexForView(flex_spacer_, 1);

  // Nav cluster: back, forward, reload, AI (trailing, right-aligned).
  nav_cluster_ = AddChildView(std::make_unique<views::View>());
  auto* nav_layout =
      nav_cluster_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kTopBarClusterVerticalInsetDp,
                          kTopBarClusterHorizontalInsetDp),
          sidebar_layout::kTopBarButtonSpacingDp));
  nav_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  back_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnBackPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kChevronLeftIcon, u"Back");
  forward_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnForwardPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kChevronRightIcon, u"Forward");
  reload_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnReloadPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kRotateCwIcon, u"Reload");
  ai_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnAiPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kMessageSquareIcon, u"AI Panel");
  tidy_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnTidyPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kWandSparklesIcon, u"Tidy Tabs");

  StyleClusterView(leading_cluster_);
  StyleClusterView(nav_cluster_);

  ApplyChromeSpacing();
  UpdateButtonImages();
}

MahoSidebarTopBarView::~MahoSidebarTopBarView() = default;

void MahoSidebarTopBarView::Update(const MahoSidebarTopBarModel& model) {
  model_ = model;
  ApplyChromeSpacing();
  UpdateButtonImages();
}

void MahoSidebarTopBarView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  views::View::OnBoundsChanged(previous_bounds);
  ApplyChromeSpacing();
}

bool MahoSidebarTopBarView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  for (const views::ImageButton* button :
       {sidebar_toggle_button_.get(), back_button_.get(),
        forward_button_.get(), reload_button_.get(), ai_button_.get(), tidy_button_.get()}) {
    if (button && button->GetVisible()) {
      gfx::Point point_in_button = point;
      views::View::ConvertPointToTarget(this, button, &point_in_button);
      if (GetButtonIconHitRect(button).Contains(point_in_button)) {
        return false;
      }
    }
  }
  return true;
}

// Adjusts spacing based on whether in-sidebar navigation is active. The chrome
// stays visible even before the first navigable tab exists so startup/no-tab
// states preserve the same top-row scaffold and caption behavior; button
// enabled state is handled separately in UpdateButtonImages().
void MahoSidebarTopBarView::ApplyChromeSpacing() {
  auto* leading_layout =
      static_cast<views::BoxLayout*>(leading_cluster_->GetLayoutManager());
  auto* nav_layout =
      static_cast<views::BoxLayout*>(nav_cluster_->GetLayoutManager());

  const bool navigation_active = model_.navigation_enabled;

  leading_layout->set_between_child_spacing(
      navigation_active ? sidebar_layout::kTopBarCompactLeadingGapDp
                        : sidebar_layout::kTopBarButtonSpacingDp);
  nav_layout->set_between_child_spacing(sidebar_layout::kTopBarButtonSpacingDp);

  leading_cluster_->SetVisible(true);
  leading_cluster_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, 0, 0,
                        navigation_active
                            ? sidebar_layout::kTopBarCompactLeadingGapDp
                            : 0));
  flex_spacer_->SetVisible(true);
  nav_cluster_->SetVisible(true);

  StyleClusterView(leading_cluster_);
  StyleClusterView(nav_cluster_);
  PreferredSizeChanged();
}

void MahoSidebarTopBarView::UpdateButtonImages() {
  UpdateButtonState(sidebar_toggle_button_, true,
                    maho_lucide_icons::kPanelLeftIcon, u"Toggle Sidebar");
  UpdateButtonState(back_button_, model_.navigation_enabled && model_.can_go_back,
                    maho_lucide_icons::kChevronLeftIcon, u"Back");
  UpdateButtonState(forward_button_,
                    model_.navigation_enabled && model_.can_go_forward,
                    maho_lucide_icons::kChevronRightIcon, u"Forward");
  UpdateButtonState(reload_button_,
                    model_.navigation_enabled && model_.can_reload,
                    model_.is_loading ? maho_lucide_icons::kXIcon
                                     : maho_lucide_icons::kRotateCwIcon,
                    model_.is_loading ? u"Stop" : u"Reload");
  UpdateButtonState(ai_button_, true,
                    maho_lucide_icons::kMessageSquareIcon, u"AI Panel");
  UpdateButtonState(tidy_button_, true,
                    maho_lucide_icons::kWandSparklesIcon, u"Tidy Tabs");
}

void MahoSidebarTopBarView::UpdateButtonState(
    views::ImageButton* button,
    bool enabled,
    const gfx::VectorIcon& icon,
    const std::u16string& tooltip) {
  if (!button) {
    return;
  }

  button->SetEnabled(enabled);
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      CreateTopBarButtonImage(icon,
                              sidebar_layout::kTopBarIconSizeDp,
                              GetTopBarButtonColor(enabled)));
}

void MahoSidebarTopBarView::OnBackPressed(const ui::Event& event) {
  if (browser_) {
    chrome::GoBack(browser_, WindowOpenDisposition::CURRENT_TAB);
  }
}

void MahoSidebarTopBarView::OnForwardPressed(const ui::Event& event) {
  if (browser_) {
    chrome::GoForward(browser_, WindowOpenDisposition::CURRENT_TAB);
  }
}

void MahoSidebarTopBarView::OnReloadPressed(const ui::Event& event) {
  if (browser_) {
    chrome::Reload(browser_, WindowOpenDisposition::CURRENT_TAB);
  }
}

void MahoSidebarTopBarView::OnAiPressed(const ui::Event& event) {
  if (!browser_) {
    return;
  }

  auto* side_panel_ui = browser_->GetFeatures().side_panel_ui();
  if (side_panel_ui->GetCurrentEntryId(SidePanelType::kContent) ==
      SidePanelEntryId::kMahoAiPanel) {
    side_panel_ui->Close(SidePanelType::kContent,
                         SidePanelEntryHideReason::kSidePanelClosed,
                         /*suppress_animations=*/true);
    return;
  }

  side_panel_ui->Show(SidePanelEntryKey(SidePanelEntryId::kMahoAiPanel),
                      SidePanelOpenTrigger::kToolbarButton);
}

void MahoSidebarTopBarView::OnToggleSidebarPressed(const ui::Event& event) {
  if (browser_) {
    sidebar_prefs::ToggleSidebarPanelExpanded(browser_->profile()->GetPrefs());
  }
}

void MahoSidebarTopBarView::OnTidyPressed(const ui::Event& event) {
  if (!browser_) return;
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (!bridge) return;
  std::string active_space_id = bridge->GetActiveSpaceId(browser_);
  if (active_space_id.empty()) return;

  // 1. Dispatch request_tidy_tabs
  base::DictValue event_dict;
  event_dict.Set("kind", "request_tidy_tabs");
  event_dict.Set("space_id", active_space_id);
  std::string event_json;
  base::JSONWriter::Write(event_dict, &event_json);

  MahoCore* core = maho::GetCore();
  if (!core) return;

  std::string updates = ::maho::core::HandleEvent(core, event_json.c_str());
  
  // 2. Parse RequestLlmCompletion from updates
  std::optional<base::Value> parsed = base::JSONReader::Read(updates, base::JSON_PARSE_RFC);
  if (!parsed) return;

  const base::DictValue* llm_request = nullptr;
  if (parsed->is_list()) {
    for (const auto& item : parsed->GetList()) {
      const auto* d = item.GetIfDict();
      if (d) {
        const std::string* kind = d->FindString("kind");
        if (kind && *kind == "request_llm_completion") {
          llm_request = d;
          break;
        }
      }
    }
  } else if (parsed->is_dict()) {
    const std::string* kind = parsed->GetDict().FindString("kind");
    if (kind && *kind == "request_llm_completion") {
      llm_request = &parsed->GetDict();
    }
  }

  if (!llm_request) return;

  const std::string* request_id_ptr = llm_request->FindString("request_id");
  if (!request_id_ptr) return;
  std::string request_id = *request_id_ptr;

  auto prompt_messages = maho::ai::ExtractPromptMessages(*llm_request);
  if (!prompt_messages) return;

  // Disable button while tidying
  tidy_button_->SetEnabled(false);

  // Initialize and call LLM Client
  auto* url_loader_factory = browser_->profile()->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess().get();
  auto llm_client = std::make_unique<MahoAiLlmClient>(
      browser_->profile()->GetPrefs(),
      url_loader_factory,
      /*provider_adapter_v2_enabled=*/false);

  MahoAiLlmClient::CompletionRequest request;
  request.prompt_messages = base::ListValue(std::move(*prompt_messages));

  auto* raw_client = llm_client.get();
  raw_client->StartCompletion(
      std::move(request),
      base::BindRepeating([](const std::string&) {}),
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarTopBarView> view, std::unique_ptr<MahoAiLlmClient> client,
             std::string space_id, std::string request_id, MahoAiLlmClient::CompletionResult result) {
            if (view) {
              view->tidy_button_->SetEnabled(true);
              
              // Send LLM result back to Rust
              base::DictValue event_dict;
              event_dict.Set("kind", "llm_result");
              event_dict.Set("request_id", request_id);
              event_dict.Set("result", result.full_text);
              std::string event_json;
              base::JSONWriter::Write(event_dict, &event_json);

              MahoCore* core = maho::GetCore();
              if (core) {
                std::string updates = ::maho::core::HandleEvent(core, event_json.c_str());
                
                // Parse TidyTabsReady from updates
                std::optional<base::Value> parsed = base::JSONReader::Read(updates, base::JSON_PARSE_RFC);
                if (parsed) {
                  const base::DictValue* tidy_ready = nullptr;
                  if (parsed->is_list()) {
                    for (const auto& item : parsed->GetList()) {
                      const auto* d = item.GetIfDict();
                      if (d) {
                        const std::string* kind = d->FindString("kind");
                        if (kind && *kind == "tidy_tabs_ready") {
                          tidy_ready = d;
                          break;
                        }
                      }
                    }
                  } else if (parsed->is_dict()) {
                    const std::string* kind = parsed->GetDict().FindString("kind");
                    if (kind && *kind == "tidy_tabs_ready") {
                      tidy_ready = &parsed->GetDict();
                    }
                  }

                  if (tidy_ready) {
                    const auto* folders = tidy_ready->FindList("folders");
                    if (folders) {
                      ShowMahoTidySuggestionDialog(view->browser_, space_id, base::Value(folders->Clone()));
                    }
                  }
                }
              }
            }
          },
          weak_factory_.GetWeakPtr(), std::move(llm_client), active_space_id, request_id),
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarTopBarView> view, std::unique_ptr<MahoAiLlmClient> client,
             const std::string& error) {
            if (view) {
              view->tidy_button_->SetEnabled(true);
              LOG(ERROR) << "Tidy LLM completion failed: " << error;
            }
          },
          weak_factory_.GetWeakPtr(), std::move(llm_client))
  );
}

}  // namespace maho
