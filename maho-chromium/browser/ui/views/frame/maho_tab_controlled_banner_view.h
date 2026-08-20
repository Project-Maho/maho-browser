// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_FRAME_MAHO_TAB_CONTROLLED_BANNER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_FRAME_MAHO_TAB_CONTROLLED_BANNER_VIEW_H_

#include <cstdint>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/views/view.h"

namespace views {
class Label;
class MdTextButton;
}

namespace maho {

class MahoTabControlledBannerView : public views::View,
                                    public gfx::AnimationDelegate {
  METADATA_HEADER(MahoTabControlledBannerView, views::View)

 public:
  class StatusDotView : public views::View,
                        public gfx::AnimationDelegate {
    METADATA_HEADER(StatusDotView, views::View)
   public:
    StatusDotView();
    StatusDotView(const StatusDotView&) = delete;
    StatusDotView& operator=(const StatusDotView&) = delete;
    ~StatusDotView() override;

    void StartPulse();
    void StopPulse();

    void AnimationProgressed(const gfx::Animation* animation) override;
    void AnimationEnded(const gfx::Animation* animation) override;

    gfx::Size CalculatePreferredSize(
        const views::SizeBounds& available_size) const override;
    void OnPaint(gfx::Canvas* canvas) override;

   private:
    gfx::SlideAnimation pulse_animation_{this};
  };

  using StopCallback = base::RepeatingCallback<void(int64_t tab_id)>;

  explicit MahoTabControlledBannerView(StopCallback stop_callback);
  MahoTabControlledBannerView(const MahoTabControlledBannerView&) = delete;
  MahoTabControlledBannerView& operator=(const MahoTabControlledBannerView&) = delete;
  ~MahoTabControlledBannerView() override;

  void SetControlledTarget(int64_t tab_id, const std::u16string& controller_name);
  void ClearControlledTarget();

  int64_t active_tab_id() const { return active_tab_id_; }
  bool is_controlling() const { return active_tab_id_ > 0; }

  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  views::Label* label_for_testing() { return label_; }
  views::MdTextButton* stop_button_for_testing() { return stop_button_; }

 private:
  void OnStopPressed();
  void UpdateColors();

  const StopCallback stop_callback_;
  int64_t active_tab_id_ = 0;
  std::u16string controller_name_;

  raw_ptr<StatusDotView> status_dot_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::MdTextButton> stop_button_ = nullptr;

  gfx::SlideAnimation slide_animation_{this};
  base::WeakPtrFactory<MahoTabControlledBannerView> weak_factory_{this};
};

}

#endif
