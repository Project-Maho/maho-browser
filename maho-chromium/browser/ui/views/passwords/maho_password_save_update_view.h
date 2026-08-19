// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_PASSWORDS_MAHO_PASSWORD_SAVE_UPDATE_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_PASSWORDS_MAHO_PASSWORD_SAVE_UPDATE_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/passwords/bubble_controllers/save_update_bubble_controller.h"
#include "chrome/browser/ui/views/passwords/password_bubble_view_base.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "ui/base/metadata/metadata_header_macros.h"

namespace views {
class Label;
class MdTextButton;
class View;
}  // namespace views

namespace maho::passwords {

// Returns true only for the Maho Native provider while the existing native
// write kill switch is enabled. Unknown or external providers fail closed.
bool ShouldUseMahoPasswordSaveUpdateView(EffectivePasswordProvider provider,
                                         PrefService* local_state);

// Maho's native credential-save surface. SaveUpdateBubbleController remains the
// sole owner of password actions and metrics; this class owns only presentation
// and keeps secrets masked unless the user explicitly chooses to reveal them.
class MahoPasswordSaveUpdateView : public PasswordBubbleViewBase {
  METADATA_HEADER(MahoPasswordSaveUpdateView, PasswordBubbleViewBase)

 public:
  MahoPasswordSaveUpdateView(content::WebContents* web_contents,
                             views::BubbleAnchor anchor_view,
                             DisplayReason reason);
  ~MahoPasswordSaveUpdateView() override;

  MahoPasswordSaveUpdateView(const MahoPasswordSaveUpdateView&) = delete;
  MahoPasswordSaveUpdateView& operator=(
      const MahoPasswordSaveUpdateView&) = delete;

#ifdef UNIT_TEST
  views::Label* origin_label_for_testing() const { return origin_label_; }
  views::Label* username_label_for_testing() const { return username_label_; }
  views::Label* password_label_for_testing() const { return password_label_; }
  views::View* credential_card_for_testing() const { return credential_card_; }
  views::MdTextButton* password_reveal_button_for_testing() const {
    return password_reveal_button_;
  }
  views::MdTextButton* never_button_for_testing() const {
    return never_button_;
  }
  bool password_revealed_for_testing() const { return password_revealed_; }
  bool CancelForTesting() { return views::DialogDelegate::Cancel(); }
  bool HasMahoWindowIconForTesting() { return !GetWindowIcon().IsEmpty(); }
#endif

 private:
  PasswordBubbleControllerBase* GetController() override;
  const PasswordBubbleControllerBase* GetController() const override;
  views::View* GetInitiallyFocusedView() override;
  ui::ImageModel GetWindowIcon() override;
  void AddedToWidget() override;
  void VisibilityChanged(views::View* starting_from,
                         bool is_visible) override;

  void SaveOrUpdate();
  void CancelAction();
  void NeverForThisSite();
  void TogglePasswordVisibility();

  SaveUpdateBubbleController controller_;
  const bool is_update_bubble_;
  raw_ptr<views::View> credential_card_ = nullptr;
  raw_ptr<views::Label> origin_label_ = nullptr;
  raw_ptr<views::Label> username_label_ = nullptr;
  raw_ptr<views::Label> password_label_ = nullptr;
  raw_ptr<views::MdTextButton> password_reveal_button_ = nullptr;
  raw_ptr<views::MdTextButton> never_button_ = nullptr;
  bool password_revealed_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho::passwords

#endif  // MAHO_BROWSER_UI_VIEWS_PASSWORDS_MAHO_PASSWORD_SAVE_UPDATE_VIEW_H_