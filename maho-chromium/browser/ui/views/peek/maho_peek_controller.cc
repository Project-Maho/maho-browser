// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/peek/maho_peek_controller.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"               // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"  // nogncheck
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/ui/views/peek/maho_peek_view.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

bool IsPeekEligible(Browser* browser) {
  if (!browser || !browser->GetProfile() || !(browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) ||
      browser->is_maho_mini()) {
    return false;
  }

  Profile* profile = browser->GetProfile();
  if (profile->IsGuestSession() || profile->IsSystemProfile()) {
    return false;
  }
  return profile->IsRegularProfile() || profile->IsPrimaryOTRProfile();
}

bool IsPeekContentsContextCompatible(Browser* browser,
                                     content::WebContents* source,
                                     content::WebContents* contents) {
  if (!IsPeekEligible(browser)) {
    return false;
  }
  content::BrowserContext* context = browser->GetProfile();
  return (!source || source->GetBrowserContext() == context) &&
         (!contents || contents->GetBrowserContext() == context);
}

namespace {

constexpr int kCardWidthDp = 960;
constexpr int kCardHeightDp = 680;
constexpr int kCardMarginDp = 32;
constexpr int kCardCornerRadiusDp = 18;
constexpr SkColor kScrimColor = SkColorSetARGB(150, 0, 0, 0);
constexpr SkColor kCardColor = SkColorSetRGB(0x20, 0x20, 0x20);

class PeekOverlayContentsView : public views::View {
 public:
  explicit PeekOverlayContentsView(base::RepeatingClosure dismiss_callback)
      : dismiss_callback_(std::move(dismiss_callback)) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
    GetViewAccessibility().SetName(u"Peek");
    SetBackground(views::CreateSolidBackground(kScrimColor));

    card_ = AddChildView(std::make_unique<views::View>());
    card_->SetPaintToLayer();
    card_->layer()->SetFillsBoundsOpaquely(false);
    card_->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(kCardCornerRadiusDp));
    card_->SetBackground(views::CreateSolidBackground(kCardColor));
    card_->SetBorder(views::CreateEmptyBorder(gfx::Insets(1)));
    card_->SetLayoutManager(std::make_unique<views::FillLayout>());
  }

  MahoPeekView* SetPeekView(std::unique_ptr<MahoPeekView> peek_view) {
    MahoPeekView* peek_view_ptr = peek_view.get();
    card_->AddChildView(std::unique_ptr<views::View>(peek_view.release()));
    return peek_view_ptr;
  }

  void Layout(PassKey) override {
    const gfx::Rect bounds = GetContentsBounds();
    const int width =
        std::max(0, std::min(kCardWidthDp, bounds.width() - 2 * kCardMarginDp));
    const int height = std::max(
        0, std::min(kCardHeightDp, bounds.height() - 2 * kCardMarginDp));
    card_->SetBounds(bounds.x() + (bounds.width() - width) / 2,
                     bounds.y() + (bounds.height() - height) / 2, width,
                     height);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!card_->bounds().Contains(event.location()) && dismiss_callback_) {
      dismiss_callback_.Run();
      return true;
    }
    return views::View::OnMousePressed(event);
  }

 private:
  base::RepeatingClosure dismiss_callback_;
  raw_ptr<views::View> card_ = nullptr;
};

}  // namespace

class MahoPeekController::HostedContentsObserver
    : public content::WebContentsObserver {
 public:
  HostedContentsObserver(MahoPeekController* controller,
                         content::WebContents* contents)
      : content::WebContentsObserver(contents), controller_(controller) {}

  void WebContentsDestroyed() override {
    Observe(nullptr);
    if (controller_) {
      controller_->OnHostedContentsDestroyed();
    }
  }

 private:
  raw_ptr<MahoPeekController> controller_;
};

class MahoPeekController::SourceTabObserver : public TabStripModelObserver {
 public:
  SourceTabObserver(MahoPeekController* controller,
                    TabStripModel* tab_strip_model,
                    content::WebContents* contents)
      : controller_(controller),
        tab_strip_model_(tab_strip_model),
        contents_(contents) {
    CHECK(tab_strip_model_);
    CHECK(contents_);
    tab_strip_model_->AddObserver(this);
  }

  ~SourceTabObserver() override {
    if (tab_strip_model_) {
      tab_strip_model_->RemoveObserver(this);
    }
  }

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    if (change.type() != TabStripModelChange::kRemoved) {
      return;
    }
    const TabStripModelChange::Remove* removal = change.GetRemove();
    for (const TabStripModelChange::RemovedTab& removed : removal->contents) {
      if (removed.contents == contents_) {
        tab_strip_model_->RemoveObserver(this);
        tab_strip_model_ = nullptr;
        contents_ = nullptr;
        if (controller_) {
          controller_->OnSourceContentsDestroyed();
        }
        return;
      }
    }
  }

  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override {
    tab_strip_model_ = nullptr;
    contents_ = nullptr;
  }

 private:
  raw_ptr<MahoPeekController> controller_;
  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<content::WebContents> contents_;
};

MahoPeekController::MahoPeekController(
    Browser* browser,
    views::Widget* parent_widget,
    base::RepeatingClosure dismiss_other_overlays)
    : browser_(browser),
      parent_widget_(parent_widget),
      dismiss_other_overlays_(std::move(dismiss_other_overlays)) {
  CHECK(browser_);
  CHECK(parent_widget_);
  parent_widget_->AddObserver(this);
}

MahoPeekController::~MahoPeekController() {
  tearing_down_ = true;
  weak_factory_.InvalidateWeakPtrs();
  teardown_weak_factory_.InvalidateWeakPtrs();
  UnregisterParentEscAccelerator();
  source_tab_observer_.reset();
  hosted_contents_observer_.reset();
  focus_restore_tracker_.SetView(nullptr);
  peek_view_ = nullptr;
  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_.reset();
  }
  closing_widget_.reset();
  state_ = State::Closed;
}

content::WebContents* MahoPeekController::ShowUrl(content::WebContents* source,
                                                  const GURL& url) {
  if (!ReserveSlot(source, nullptr)) {
    return nullptr;
  }

  auto peek_view = std::make_unique<MahoPeekView>(
      browser_,
      base::BindRepeating(&MahoPeekController::PromoteToTab,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&MahoPeekController::Hide,
                          weak_factory_.GetWeakPtr()));
  content::WebContents* hosted_contents =
      peek_view->CreateAndLoad(browser_->GetProfile(), url);
  ShowReserved(std::move(peek_view), hosted_contents);
  return hosted_contents;
}

bool MahoPeekController::TryAdoptContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents>* contents) {
  if (!contents || !*contents || !ReserveSlot(source, contents->get())) {
    return false;
  }

  auto peek_view = std::make_unique<MahoPeekView>(
      browser_,
      base::BindRepeating(&MahoPeekController::PromoteToTab,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&MahoPeekController::Hide,
                          weak_factory_.GetWeakPtr()));
  content::WebContents* hosted_contents =
      peek_view->AdoptContents(std::move(*contents));
  ShowReserved(std::move(peek_view), hosted_contents);
  return true;
}

bool MahoPeekController::ReserveSlot(content::WebContents* source,
                                     content::WebContents* contents_to_adopt) {
  if (state_ != State::Closed || tearing_down_ || !parent_widget_) {
    return false;
  }

  CHECK(IsPeekContentsContextCompatible(browser_, source, contents_to_adopt));

  // Reserve synchronously before dismissing peers or creating/moving a
  // WebContents. Reentrant open attempts therefore observe a busy slot.
  state_ = State::Opening;
  dismiss_other_overlays_.Run();

  if (source) {
    source_tab_observer_ = std::make_unique<SourceTabObserver>(
        this, browser_->GetTabStripModel(), source);
  }
  return true;
}

void MahoPeekController::ShowReserved(std::unique_ptr<MahoPeekView> peek_view,
                                      content::WebContents* hosted_contents) {
  CHECK_EQ(state_, State::Opening);
  CHECK(peek_view);
  CHECK(hosted_contents);
  CHECK(parent_widget_);

  auto contents_view =
      std::make_unique<PeekOverlayContentsView>(base::BindRepeating(
          &MahoPeekController::OnScrimPressed, weak_factory_.GetWeakPtr()));
  peek_view_ = contents_view->SetPeekView(std::move(peek_view));
  hosted_contents_observer_ =
      std::make_unique<HostedContentsObserver>(this, hosted_contents);

  if (auto* focus_manager = parent_widget_->GetFocusManager()) {
    focus_restore_tracker_.SetView(focus_manager->GetFocusedView());
  }

  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.parent = parent_widget_->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kOpaque;
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
  widget_->Init(std::move(params));
  widget_->SetContentsView(std::move(contents_view));
  widget_->AddObserver(this);

  UpdateBounds();
  widget_->Show();
  widget_->Activate();
  RegisterParentEscAccelerator();
  state_ = State::Open;
  peek_view_->RequestFocus();
}

void MahoPeekController::Hide() {
  if (state_ == State::Closed || state_ == State::Closing || tearing_down_) {
    return;
  }

  state_ = State::Closing;
  UnregisterParentEscAccelerator();
  source_tab_observer_.reset();
  hosted_contents_observer_.reset();

  std::unique_ptr<views::Widget> closing = std::move(widget_);
  peek_view_ = nullptr;
  if (closing) {
    closing->RemoveObserver(this);
  }

  // Destroying the view tree also destroys the controller-owned hosted
  // WebContents. Observers are detached first so that this cannot recursively
  // enter Hide() or close the slot twice.
  closing.reset();
  RestoreFocus();
  state_ = State::Closed;
}

void MahoPeekController::OnHostedContentsDestroyed() {
  hosted_contents_observer_.reset();
  Hide();
}

void MahoPeekController::OnSourceContentsDestroyed() {
  source_tab_observer_.reset();
  Hide();
}

void MahoPeekController::OnScrimPressed() {
  Hide();
}

void MahoPeekController::PromoteToTab() {
  if (state_ != State::Open || !peek_view_) {
    return;
  }

  // ReleaseContents() clears the Peek delegate, observer, WebView attachment,
  // and modal-dialog delegate before ownership reaches the tab strip. Do not
  // install the Browser delegate here: Browser::OnTabInsertedAt owns that
  // reattachment, including the Browser's modal-dialog delegate and tab
  // helpers/session tracking.
  hosted_contents_observer_.reset();
  std::unique_ptr<content::WebContents> contents =
      peek_view_->ReleaseContents();
  CHECK(contents);

  browser_->GetTabStripModel()->InsertWebContentsAt(
      /*index=*/-1, std::move(contents), AddTabTypes::ADD_ACTIVE);
  Hide();
}

void MahoPeekController::OnWidgetDestroying(views::Widget* widget) {
  if (widget == parent_widget_) {
    tearing_down_ = true;
    weak_factory_.InvalidateWeakPtrs();
    UnregisterParentEscAccelerator();
    source_tab_observer_.reset();
    hosted_contents_observer_.reset();
    focus_restore_tracker_.SetView(nullptr);
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
    state_ = State::Closing;
    if (widget_) {
      widget_->RemoveObserver(this);
      peek_view_ = nullptr;
      closing_widget_ = std::move(widget_);
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&MahoPeekController::FinishUnexpectedWidgetTeardown,
                         teardown_weak_factory_.GetWeakPtr()));
    } else {
      state_ = State::Closed;
    }
    return;
  }

  if (widget == widget_.get()) {
    state_ = State::Closing;
    UnregisterParentEscAccelerator();
    source_tab_observer_.reset();
    hosted_contents_observer_.reset();
    peek_view_ = nullptr;
    widget_->RemoveObserver(this);
    closing_widget_ = std::move(widget_);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&MahoPeekController::FinishUnexpectedWidgetTeardown,
                       teardown_weak_factory_.GetWeakPtr()));
  }
}

void MahoPeekController::FinishUnexpectedWidgetTeardown() {
  closing_widget_.reset();
  if (!tearing_down_) {
    RestoreFocus();
  }
  state_ = State::Closed;
}

void MahoPeekController::OnWidgetBoundsChanged(views::Widget* widget,
                                               const gfx::Rect& new_bounds) {
  if (widget == parent_widget_ && widget_) {
    UpdateBounds();
  }
}

bool MahoPeekController::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_ESCAPE && SlotBusy()) {
    Hide();
    return true;
  }
  return false;
}

bool MahoPeekController::CanHandleAccelerators() const {
  return state_ != State::Closed;
}

void MahoPeekController::UpdateBounds() {
  if (widget_ && parent_widget_) {
    widget_->SetBounds(parent_widget_->GetClientAreaBoundsInScreen());
  }
}

void MahoPeekController::RegisterParentEscAccelerator() {
  if (esc_target_focus_manager_ || !parent_widget_) {
    return;
  }
  esc_target_focus_manager_ =
      widget_ ? widget_->GetFocusManager() : parent_widget_->GetFocusManager();
  if (!esc_target_focus_manager_) {
    return;
  }
  esc_target_focus_manager_->RegisterAccelerator(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE),
      ui::AcceleratorManager::kNormalPriority, this);
}

void MahoPeekController::UnregisterParentEscAccelerator() {
  if (!esc_target_focus_manager_) {
    return;
  }
  esc_target_focus_manager_->UnregisterAccelerator(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE), this);
  esc_target_focus_manager_ = nullptr;
}

void MahoPeekController::RestoreFocus() {
  views::View* target = focus_restore_tracker_.view();
  focus_restore_tracker_.SetView(nullptr);
  if (!target) {
    return;
  }
  views::Widget* target_widget = target->GetWidget();
  if (target_widget && !target_widget->IsClosed()) {
    target->RequestFocus();
  }
}

}  // namespace maho
