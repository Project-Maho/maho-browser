// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_MAHO_CONTENT_GRADIENT_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_MAHO_CONTENT_GRADIENT_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/views/view.h"

namespace maho {

class MahoSidebarGrainOverlayView;

class MahoContentGradientView : public views::View {
  METADATA_HEADER(MahoContentGradientView, views::View)

 public:
  MahoContentGradientView();
  MahoContentGradientView(const MahoContentGradientView&) = delete;
  MahoContentGradientView& operator=(const MahoContentGradientView&) = delete;
  ~MahoContentGradientView() override;

  // Sets browser-local themed stops (from MahoSidebarPalette). Uses the first
  // 1-3 stops with their alpha preserved so low Space opacity lets the window
  // glass show through; empty falls back to the ColorProvider gradient.
  void SetPalette(const MahoSidebarPalette& palette);

  // Sets the grain/noise texture amount (MahoSidebarPalette::grain), matching
  // the sidebar's grain on the empty-content surface.
  void SetGrain(float grain);

  // views::View:
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

 private:
  MahoSidebarPalette palette_;
  raw_ptr<MahoSidebarGrainOverlayView> grain_overlay_ = nullptr;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_MAHO_CONTENT_GRADIENT_VIEW_H_
