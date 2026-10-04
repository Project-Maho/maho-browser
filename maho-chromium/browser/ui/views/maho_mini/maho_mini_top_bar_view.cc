// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.h"

#include <utility>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/url_formatter/url_fixer.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/render_text.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace maho {

namespace {

constexpr int kMahoMiniTopBarHeight = 46;
constexpr int kBarOuterInset = 8;
#if BUILDFLAG(IS_MAC)
constexpr int kTrafficLightsReservation = 60;
#endif
constexpr int kZoneGap = 6;
constexpr int kIntegratedSurfaceY = 7;
constexpr int kIntegratedSurfaceHeight = 32;
constexpr int kIntegratedSurfaceCornerRadius = 10;
constexpr int kIntegratedSurfaceInnerInset = 6;
constexpr int kSpacePillVerticalInset = 4;
constexpr int kSpacePillHorizontalInset = 6;
constexpr int kSpacePillContentSpacing = 5;
constexpr int kSpaceGlyphSize = 14;
constexpr int kCopyButtonSize = 24;
constexpr int kPromoteControlWidth = 210;
constexpr int kPromoteControlY = 9;
constexpr int kPromoteButtonHeight = 28;
constexpr int kPromoteControlCornerRadius = 8;
constexpr int kPromoteDropButtonWidth = 30;
constexpr int kAddressFieldMaxWidth = 112;
constexpr int kAddressFieldMinWidth = 72;
constexpr int kAddressControlGap = 6;
constexpr int kAddressFieldHeight = 26;
constexpr int kMinimumPersistedWindowSize = 320;

class MahoMiniAddressField : public views::Textfield {
 public:
  void DisableSubpixelRendering() {
    GetRenderText()->set_subpixel_rendering_suppressed(true);
  }

  METADATA_HEADER(MahoMiniAddressField, views::Textfield)
};

BEGIN_METADATA(MahoMiniAddressField)
END_METADATA

std::u16string GetSpaceName(const std::string& space_id) {
  maho::FooterSpaceState state = maho::GetCoreFooterSpaceStateRaw();
  for (size_t i = 0; i < state.space_ids.size(); ++i) {
    if (state.space_ids[i] == space_id) {
      return base::UTF8ToUTF16(state.space_names[i]);
    }
  }
  return base::UTF8ToUTF16(space_id);
}

void ExecuteTabPromotion(Browser* popup_browser,
                         Profile* target_profile,
                         const std::string& space_id) {
  if (!popup_browser || !target_profile) {
    return;
  }
  TabStripModel* popup_tab_strip = popup_browser->GetTabStripModel();
  if (!popup_tab_strip || popup_tab_strip->empty()) {
    return;
  }

  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(target_profile);
  BrowserWindowInterface* target_bwi =
      collection ? collection->FindTabbedBrowser(/*match_original_profiles=*/false)
                 : nullptr;
  Browser* target_browser = static_cast<Browser*>(target_bwi);
  if (!target_browser || target_browser == popup_browser) {
    target_browser =
        static_cast<Browser*>(CreateBrowserWindow(BrowserWindowCreateParams(target_profile, true)));
  }

  int active_index = popup_tab_strip->active_index();
  std::unique_ptr<content::WebContents> contents =
      popup_tab_strip->DetachWebContentsAtForInsertion(active_index);
  if (contents) {
    auto* bridge = MahoSpaceProfileBridge::GetInstance();
    if (bridge) {
      bridge->SetActiveSpaceId(target_browser, space_id);
    }
    target_browser->GetTabStripModel()->InsertWebContentsAt(
        /*index=*/-1, std::move(contents),
        AddTabTypes::ADD_ACTIVE | AddTabTypes::ADD_INHERIT_OPENER);
    target_browser->GetWindow()->Show();
    target_browser->GetWindow()->Activate();
  }
  popup_browser->GetWindow()->Close();
}

}  // namespace

class MahoMiniPromoteButton : public views::View,
                              public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(MahoMiniPromoteButton, views::View)

 public:
  explicit MahoMiniPromoteButton(MahoMiniTopBarView* parent) : parent_(parent) {
    SetPreferredSize(
        gfx::Size(kPromoteControlWidth, kPromoteButtonHeight));
    SetBackground(views::CreateRoundedRectBackground(
        kMahoColorMiniControlBackground, kPromoteControlCornerRadius));
    SetBorder(views::CreateRoundedRectBorder(
        1, kPromoteControlCornerRadius, kMahoColorMiniControlBorder));

    main_button_ = AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&MahoMiniPromoteButton::OnMainPressed,
                            base::Unretained(this)),
        u"Open in Space  ⌘O"));
    main_button_->SetCustomPadding(gfx::Insets::VH(0, 7));
    main_button_->SetMinSize(gfx::Size(0, kPromoteButtonHeight - 2));
    main_button_->SetBgColorOverrideDeprecated(SK_ColorTRANSPARENT);
    main_button_->SetStrokeColorOverrideDeprecated(SK_ColorTRANSPARENT);
    main_button_->SetEnabledTextColors(kMahoColorMiniPrimaryText);
    main_button_->SetTextSubpixelRenderingEnabled(false);
    main_button_->SetAccessibleName(u"Open in Space");

    divider_ = AddChildView(std::make_unique<views::View>());
    divider_->SetPreferredSize(gfx::Size(1, 18));
    divider_->SetBackground(
        views::CreateSolidBackground(kMahoColorMiniControlBorder));

    drop_button_ =
        AddChildView(std::make_unique<views::ImageButton>(base::BindRepeating(
            &MahoMiniPromoteButton::OnDropPressed, base::Unretained(this))));
    drop_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(vector_icons::kCaretDownOldIcon,
                                       kMahoColorMiniSecondaryText, 8));
    drop_button_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    drop_button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    drop_button_->SetPreferredSize(
        gfx::Size(kPromoteDropButtonWidth, kPromoteButtonHeight));
    drop_button_->SetTooltipText(u"Choose Space");
    drop_button_->SetAccessibleName(u"Choose Space");
    views::InkDrop::Get(drop_button_)
        ->SetMode(views::InkDropHost::InkDropMode::ON);
  }

  void Layout(PassKey pass_key) override {
    LayoutSuperclass<views::View>(this);

    const int drop_width = std::min(kPromoteDropButtonWidth, width());
    const int drop_x = width() - drop_width;
    const int divider_width = drop_x > 0 ? 1 : 0;
    const int divider_x = drop_x - divider_width;
    const int divider_height = std::min(18, height());

    main_button_->SetBounds(0, 0, divider_x, height());
    divider_->SetBounds(divider_x, (height() - divider_height) / 2,
                         divider_width, divider_height);
    drop_button_->SetBounds(drop_x, 0, drop_width, height());
  }

  void SetDefaultTarget(const std::string& space_id,
                        const std::u16string& space_name) {
    if (has_explicit_target_ || space_id.empty()) {
      return;
    }
    SetTarget(space_id, space_name, false);
  }

  const std::string& target_space_id() const { return target_space_id_; }

  std::string EffectiveTargetSpaceId() const {
    if (!target_space_id_.empty()) {
      return target_space_id_;
    }
    FooterSpaceState state = GetCoreFooterSpaceStateRaw();
    if (!state.space_ids.empty()) {
      return state.space_ids[0];
    }
    return std::string();
  }

  void OnMainPressed() {
    const std::string target = EffectiveTargetSpaceId();
    if (!target.empty()) {
      parent_->PromoteToSpace(target);
    }
  }

  void OnDropPressed() {
    auto* bridge = MahoSpaceProfileBridge::GetInstance();
    if (!bridge) {
      return;
    }

    FooterSpaceState state = GetCoreFooterSpaceStateRaw();
    menu_spaces_ = state.space_ids;
    menu_space_names_.clear();

    menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    for (size_t i = 0; i < state.space_ids.size(); ++i) {
      std::u16string space_name = GetSpaceName(state.space_ids[i]);
      menu_space_names_.push_back(space_name);
      menu_model_->AddItem(static_cast<int>(i), space_name);
    }

    menu_runner_ = std::make_unique<views::MenuRunner>(
        menu_model_.get(), views::MenuRunner::COMBOBOX);

    menu_runner_->RunMenuAt(
        GetWidget(), nullptr, drop_button_->GetBoundsInScreen(),
        views::MenuAnchorPosition::kTopRight, ui::mojom::MenuSourceType::kNone);
  }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    (void)event_flags;
    if (command_id >= 0 && command_id < static_cast<int>(menu_spaces_.size())) {
      SetTarget(menu_spaces_[command_id], menu_space_names_[command_id], true);
    }
  }

 private:
  void SetTarget(const std::string& space_id,
                 const std::u16string& space_name,
                 bool explicit_selection) {
    target_space_id_ = space_id;
    target_space_name_ =
        space_name.empty() ? base::UTF8ToUTF16(space_id) : space_name;
    has_explicit_target_ = has_explicit_target_ || explicit_selection;
    main_button_->SetText(u"Open in " + target_space_name_ + u"  ⌘O");
    main_button_->SetAccessibleName(u"Open in " + target_space_name_);
    PreferredSizeChanged();
  }

  raw_ptr<MahoMiniTopBarView> parent_;
  raw_ptr<views::MdTextButton> main_button_ = nullptr;
  raw_ptr<views::View> divider_ = nullptr;
  raw_ptr<views::ImageButton> drop_button_ = nullptr;

  std::vector<std::string> menu_spaces_;
  std::vector<std::u16string> menu_space_names_;
  std::string target_space_id_;
  std::u16string target_space_name_;
  bool has_explicit_target_ = false;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

MahoMiniTopBarView::MahoMiniTopBarView(Browser* browser,
                                       BrowserView* browser_view)
    : browser_(browser), browser_view_(browser_view) {
  SetPreferredSize(gfx::Size(0, kMahoMiniTopBarHeight));

  space_pill_ = AddChildView(std::make_unique<views::View>());
  space_pill_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  auto* space_pill_layout =
      space_pill_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kSpacePillVerticalInset, kSpacePillHorizontalInset),
          kSpacePillContentSpacing));
  space_pill_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  space_pill_->SetBackground(nullptr);
  space_pill_->SetBorder(nullptr);
  space_pill_icon_ =
      space_pill_->AddChildView(std::make_unique<views::ImageView>());
  space_pill_icon_->SetPreferredSize(
      gfx::Size(kSpaceGlyphSize, kSpaceGlyphSize));
  space_pill_label_ =
      space_pill_->AddChildView(std::make_unique<views::Label>(u""));
  space_pill_label_->SetFontList(views::Label::GetDefaultFontList().Derive(
      -1, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM));
  space_pill_label_->SetEnabledColor(kMahoColorMiniSecondaryText);
  space_pill_label_->SetSubpixelRenderingEnabled(false);

  copy_button_ =
      AddChildView(views::CreateVectorImageButton(base::BindRepeating(
          &MahoMiniTopBarView::OnCopyPressed, base::Unretained(this))));
  copy_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(maho_lucide_icons::kLinkIcon,
                                     kMahoColorMiniSecondaryText, 14));
  copy_button_->SetPreferredSize(gfx::Size(kCopyButtonSize, kCopyButtonSize));
  copy_button_->SetTooltipText(u"Copy link");
  copy_button_->SetAccessibleName(u"Copy link");
  copy_button_->SetBackground(nullptr);
  copy_button_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  views::InstallCircleHighlightPathGenerator(copy_button_);
  views::InkDrop::Get(copy_button_)
      ->SetMode(views::InkDropHost::InkDropMode::ON);

  promote_button_ = AddChildView(std::make_unique<MahoMiniPromoteButton>(this));

  auto address_field = std::make_unique<MahoMiniAddressField>();
  auto* address_field_raw = address_field.get();
  address_field_ = AddChildView(std::move(address_field));
  address_field_->SetFontList(views::Label::GetDefaultFontList().Derive(
      -1, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM));
  address_field_->SetPlaceholderText(u"Search or enter address");
  address_field_->GetViewAccessibility().SetName(u"Address");
  address_field_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  address_field_->SetBorder(nullptr);
  address_field_->SetBackgroundEnabled(false);
  address_field_raw->DisableSubpixelRendering();
  views::FocusRing::Install(address_field_);
  views::FocusRing::Get(address_field_)
      ->SetColorId(ui::kColorSysStateFocusRing);
  views::InstallRoundRectHighlightPathGenerator(
      address_field_, gfx::Insets(), kIntegratedSurfaceCornerRadius);
  address_field_->SetPreferredSize(
      gfx::Size(kAddressFieldMaxWidth, kAddressFieldHeight));
  address_field_->set_controller(this);

  browser_->GetTabStripModel()->AddObserver(this);
  if (browser_->GetTabStripModel()->GetActiveWebContents()) {
    Observe(browser_->GetTabStripModel()->GetActiveWebContents());
  }

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (bridge) {
    space_observation_.Observe(bridge);
  }

  UpdateHostname();
  UpdateSpacePill();
}

MahoMiniTopBarView::~MahoMiniTopBarView() {
  if (browser_ && browser_->GetTabStripModel()) {
    browser_->GetTabStripModel()->RemoveObserver(this);
  }
}

void MahoMiniTopBarView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  const ui::ColorProvider* color_provider = GetColorProvider();
  if (!color_provider) {
    return;
  }
  canvas->FillRect(GetLocalBounds(),
                   color_provider->GetColor(kMahoColorMiniFrameWash));

  if (!integrated_surface_bounds_.IsEmpty()) {
    cc::PaintFlags flags;
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setAntiAlias(true);
    flags.setColor(color_provider->GetColor(kMahoColorMiniControlBackground));
    canvas->DrawRoundRect(gfx::RectF(integrated_surface_bounds_),
                          kIntegratedSurfaceCornerRadius, flags);

    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(color_provider->GetColor(kMahoColorMiniControlBorder));
    canvas->DrawRoundRect(gfx::RectF(integrated_surface_bounds_),
                          kIntegratedSurfaceCornerRadius, flags);
  }

  canvas->DrawLine(gfx::Point(0, height() - 1),
                    gfx::Point(width(), height() - 1),
                   color_provider->GetColor(kMahoColorMiniFrameEdge));
}

void MahoMiniTopBarView::AddedToWidget() {
  AddAccelerator(ui::Accelerator(ui::VKEY_O, ui::EF_COMMAND_DOWN));
  if (GetWidget() && !widget_observation_.IsObserving()) {
    widget_observation_.Observe(GetWidget());
    last_widget_size_ = GetWidget()->GetWindowBoundsInScreen().size();
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&MahoMiniTopBarView::FocusAddressFieldIfBlank,
                                weak_factory_.GetWeakPtr()));
}

void MahoMiniTopBarView::RemovedFromWidget() {
  RemoveAccelerator(ui::Accelerator(ui::VKEY_O, ui::EF_COMMAND_DOWN));
  widget_observation_.Reset();
  can_persist_widget_size_ = false;
}

bool MahoMiniTopBarView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_O && accelerator.IsCmdDown()) {
    if (promote_button_) {
      const std::string target = promote_button_->EffectiveTargetSpaceId();
      if (!target.empty()) {
        PromoteToSpace(target);
      }
    }
    return true;
  }
  return false;
}

void MahoMiniTopBarView::Layout(PassKey pass_key) {
  LayoutSuperclass<views::View>(this);

  if (width() <= 0 || !space_pill_ || !address_field_ || !copy_button_ ||
      !promote_button_) {
    return;
  }

  const int promote_width = std::min(kPromoteControlWidth,
                                     std::max(0, width() - 2 * kBarOuterInset));
  const int promote_x =
      std::max(kBarOuterInset, width() - kBarOuterInset - promote_width);
  promote_button_->SetBounds(promote_x, kPromoteControlY, promote_width,
                             kPromoteButtonHeight);

  int integrated_x = kBarOuterInset;
#if BUILDFLAG(IS_MAC)
  integrated_x += kTrafficLightsReservation + kZoneGap;
#endif
  const int integrated_right = std::max(integrated_x, promote_x - kZoneGap);
  integrated_surface_bounds_ =
      gfx::Rect(integrated_x, kIntegratedSurfaceY,
                integrated_right - integrated_x, kIntegratedSurfaceHeight);

  if (integrated_surface_bounds_.IsEmpty()) {
    space_pill_->SetBounds(integrated_x, 0, 0, 0);
    copy_button_->SetBounds(integrated_x, 0, 0, 0);
    address_field_->SetBounds(width() / 2, 0, 0, kAddressFieldHeight);
    return;
  }

  const int inner_left =
      integrated_surface_bounds_.x() + kIntegratedSurfaceInnerInset;
  const int inner_right =
      integrated_surface_bounds_.right() - kIntegratedSurfaceInnerInset;
  const int center_x = width() / 2;

  const int copy_width = std::min(kCopyButtonSize,
                                  std::max(0, inner_right - inner_left));
  const int copy_x = std::max(inner_left, inner_right - copy_width);
  copy_button_->SetBounds(copy_x, (height() - kCopyButtonSize) / 2,
                          copy_width, kCopyButtonSize);

  const gfx::Size space_preferred = space_pill_->GetPreferredSize();
  const int max_space_width = std::max(
      0, center_x - kAddressFieldMinWidth / 2 - kAddressControlGap - inner_left);
  const int space_width = std::min(space_preferred.width(), max_space_width);
  space_pill_->SetBounds(inner_left,
                         (height() - space_preferred.height()) / 2,
                         space_width, space_preferred.height());

  int left_limit = inner_left;
  if (space_pill_ && space_pill_->GetVisible() &&
      !space_pill_->bounds().IsEmpty()) {
    left_limit =
        std::max(left_limit, space_pill_->bounds().right() + kAddressControlGap);
  }

  const int right_limit = copy_button_->GetVisible()
                              ? copy_button_->bounds().x() - kAddressControlGap
                              : inner_right;

  const int side_clearance =
      std::max(0, std::min(center_x - left_limit, right_limit - center_x));
  int field_width = std::min(kAddressFieldMaxWidth, side_clearance * 2);
  field_width = std::min(field_width, width());

  if (field_width <= 0) {
    address_field_->SetBounds(center_x, (height() - kAddressFieldHeight) / 2, 0,
                              kAddressFieldHeight);
    return;
  }

  const int x = (width() - field_width) / 2;
  const int y = (height() - kAddressFieldHeight) / 2;
  address_field_->SetBounds(x, y, field_width, kAddressFieldHeight);
}

void MahoMiniTopBarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  if (!address_field_ || !GetColorProvider()) {
    return;
  }
  address_field_->SetTextColorId(kMahoColorMiniPrimaryText);
  address_field_->SetPlaceholderTextColorId(kMahoColorMiniSecondaryText);
}

bool MahoMiniTopBarView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  const views::View* interactive[] = {address_field_.get(), copy_button_.get(),
                                      promote_button_.get()};
  for (const views::View* child : interactive) {
    if (child && child->GetVisible()) {
      gfx::Point point_in_child = point;
      views::View::ConvertPointToTarget(this, child, &point_in_child);
      if (child->GetLocalBounds().Contains(point_in_child)) {
        return false;
      }
    }
  }
  return true;
}

bool MahoMiniTopBarView::HandleKeyEvent(views::Textfield* sender,
                                        const ui::KeyEvent& event) {
  if (sender != address_field_ || event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (event.key_code() == ui::VKEY_RETURN) {
    AcceptAddressInput();
    return true;
  }
  return false;
}

void MahoMiniTopBarView::OnWidgetBoundsChanged(views::Widget* widget,
                                               const gfx::Rect& new_bounds) {
  if (!can_persist_widget_size_ || widget != GetWidget() ||
      widget->IsMinimized()) {
    return;
  }
  const gfx::Size size = new_bounds.size();
  if (size == last_widget_size_ || size.width() < kMinimumPersistedWindowSize ||
      size.height() < kMinimumPersistedWindowSize) {
    return;
  }
  last_widget_size_ = size;
  PrefService* prefs = browser_->GetProfile()->GetPrefs();
  if (prefs) {
    prefs->SetInteger(sidebar_prefs::kMahoMiniWindowWidth, size.width());
    prefs->SetInteger(sidebar_prefs::kMahoMiniWindowHeight, size.height());
  }
}

void MahoMiniTopBarView::OnWidgetVisibilityChanged(views::Widget* widget,
                                                   bool visible) {
  if (widget != GetWidget()) {
    return;
  }
  can_persist_widget_size_ = visible;
  if (visible) {
    last_widget_size_ = widget->GetWindowBoundsInScreen().size();
  }
}

void MahoMiniTopBarView::OnWidgetDestroying(views::Widget* widget) {
  if (widget_observation_.IsObservingSource(widget)) {
    widget_observation_.Reset();
  }
  can_persist_widget_size_ = false;
}

void MahoMiniTopBarView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    Observe(tab_strip_model->GetActiveWebContents());
    UpdateHostname();
  }
}

void MahoMiniTopBarView::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame() &&
      navigation_handle->HasCommitted()) {
    UpdateHostname();
  }
}

void MahoMiniTopBarView::OnSpaceProfileBridgeChanged() {
  UpdateSpacePill();
}

void MahoMiniTopBarView::OnSpaceProfileBridgeChanged(bool is_structural) {
  UpdateSpacePill();
}

void MahoMiniTopBarView::PromoteToSpace(const std::string& space_id) {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (!bridge) {
    return;
  }

  base::FilePath relative_path = bridge->GetProfilePathForSpace(space_id);
  if (relative_path.empty()) {
    return;
  }

  base::FilePath absolute_path =
      g_browser_process->profile_manager()->user_data_dir().Append(
          relative_path);
  Profile* target_profile =
      g_browser_process->profile_manager()->GetProfileByPath(absolute_path);
  if (!target_profile) {
    g_browser_process->profile_manager()->LoadProfileByPath(
        absolute_path, false,
        base::BindOnce(
            [](base::WeakPtr<BrowserWindowInterface> weak_popup_browser, std::string space_id,
               Profile* target_profile) {
              if (weak_popup_browser && target_profile) {
                ExecuteTabPromotion(static_cast<Browser*>(weak_popup_browser.get()), target_profile,
                                    space_id);
              }
            },
            browser_->GetWeakPtr(), space_id));
    return;
  }

  ExecuteTabPromotion(browser_, target_profile, space_id);
}

SkColor MahoMiniTopBarView::ResolveSpaceColorWithAlpha() {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (!bridge) {
    return SkColorSetARGB(40, 128, 128, 128);
  }
  std::string space_id = bridge->GetActiveSpaceId(browser_);
  FooterSpaceState core_state = GetCoreFooterSpaceStateRaw();
  SkColor space_color = SkColorSetRGB(128, 128, 128);
  for (size_t i = 0; i < core_state.space_ids.size(); ++i) {
    if (core_state.space_ids[i] == space_id) {
      std::string color_hex = core_state.space_colors[i];
      if (color_hex.size() == 7 && color_hex[0] == '#') {
        unsigned int r = 0;
        unsigned int g = 0;
        unsigned int b = 0;
        if (base::HexStringToUInt(color_hex.substr(1, 2), &r) &&
            base::HexStringToUInt(color_hex.substr(3, 2), &g) &&
            base::HexStringToUInt(color_hex.substr(5, 2), &b)) {
          space_color = SkColorSetRGB(r, g, b);
        }
      }
      break;
    }
  }
  return SkColorSetA(space_color, 40);
}

void MahoMiniTopBarView::UpdateHostname() {
  if (!web_contents()) {
    address_field_->SetText(u"");
    return;
  }
  GURL url = web_contents()->GetVisibleURL();
  if (url.is_empty() || url.SchemeIs("about")) {
    address_field_->SetText(u"");
    return;
  }
  std::string host = net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);
  if (host.empty()) {
    host = url.host();
    if (host.rfind("www.", 0) == 0) {
      host.erase(0, 4);
    }
  }
  if (host.empty() && url.SchemeIsFile()) {
    host = "file";
  }
  address_field_->SetText(base::UTF8ToUTF16(host));
}

void MahoMiniTopBarView::AcceptAddressInput() {
  std::string input = base::UTF16ToUTF8(address_field_->GetText());
  base::TrimWhitespaceASCII(input, base::TRIM_ALL, &input);
  if (input.empty()) {
    return;
  }

  GURL target = url_formatter::FixupURL(input, std::string());
  const bool is_direct_url =
      target.is_valid() &&
      (target.has_host() || target.SchemeIsFile() || target.SchemeIs("about") ||
       target.SchemeIs("chrome"));
  if (!is_direct_url) {
    TemplateURLService* template_url_service =
        TemplateURLServiceFactory::GetForProfile(browser_->GetProfile());
    if (!template_url_service ||
        !template_url_service->GetDefaultSearchProvider()) {
      return;
    }
    target =
        template_url_service->GetDefaultSearchProvider()->GenerateSearchURL(
            template_url_service->search_terms_data(),
            base::UTF8ToUTF16(input));
  }
  if (!target.is_valid()) {
    return;
  }

  browser_->OpenURL(content::OpenURLParams(target, content::Referrer(),
                                           WindowOpenDisposition::CURRENT_TAB,
                                           ui::PAGE_TRANSITION_TYPED, false),
                    base::NullCallback());
  if (web_contents()) {
    web_contents()->Focus();
  }
}

void MahoMiniTopBarView::FocusAddressFieldIfBlank() {
  if (!address_field_) {
    return;
  }
  const GURL url = web_contents() ? web_contents()->GetVisibleURL() : GURL();
  if (url.is_empty() || url.SchemeIs("about")) {
    address_field_->SetText(u"");
    address_field_->RequestFocus();
  }
}

void MahoMiniTopBarView::UpdateSpacePill() {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (!bridge) {
    return;
  }

  std::string space_id = bridge->GetActiveSpaceId(browser_);
  if (space_id.empty()) {
    space_id = GetMahoMiniOriginatingSpaceId(browser_);
  }
  if (space_id.empty()) {
    FooterSpaceState state = GetCoreFooterSpaceStateRaw();
    if (!state.space_ids.empty()) {
      space_id = state.space_ids[0];
    }
  }
  std::u16string space_name = GetSpaceName(space_id);

  space_pill_label_->SetText(space_name);
  space_pill_->GetViewAccessibility().SetName(space_name);

  space_color_ = ResolveSpaceColorWithAlpha();
  space_pill_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      maho_lucide_icons::kGlobeIcon, SkColorSetA(space_color_, 0xFF),
      kSpaceGlyphSize));
  promote_button_->SetDefaultTarget(space_id, space_name);

  space_pill_->InvalidateLayout();
  InvalidateLayout();
}

void MahoMiniTopBarView::OnCopyPressed() {
  if (!web_contents()) {
    return;
  }
  GURL url = web_contents()->GetVisibleURL();
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(base::UTF8ToUTF16(url.spec()));

  auto* overlay = MahoNotificationOverlay::GetOrCreateForBrowser(browser_);
  if (overlay) {
    overlay->Show(l10n_util::GetStringUTF16(IDS_MAHO_TOAST_LINK_COPIED),
                  std::u16string(), base::Seconds(2));
  }
}

BEGIN_METADATA(MahoMiniPromoteButton)
END_METADATA

BEGIN_METADATA(MahoMiniTopBarView)
END_METADATA

}  // namespace maho
