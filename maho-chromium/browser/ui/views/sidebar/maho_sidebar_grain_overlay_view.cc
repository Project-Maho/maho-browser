// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_grain_overlay_view.h"

#include <algorithm>

#include "base/no_destructor.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia.h"

namespace maho {

namespace {

constexpr int kNoiseSize = 128;

// Procedural noise image cached globally.
const gfx::ImageSkia& GetNoiseImage() {
  static base::NoDestructor<gfx::ImageSkia> noise_image([]() {
    SkBitmap bitmap;
    bitmap.allocN32Pixels(kNoiseSize, kNoiseSize);
    
    // Seeded LCG for reproducible pseudo-random noise.
    uint32_t seed = 1337;
    auto random_byte = [&seed]() -> uint8_t {
      seed = seed * 1664525 + 1013904223;
      return static_cast<uint8_t>((seed >> 24) & 0xFF);
    };

    for (int y = 0; y < kNoiseSize; ++y) {
      for (int x = 0; x < kNoiseSize; ++x) {
        uint8_t value = random_byte();
        *bitmap.getAddr32(x, y) = SkColorSetARGB(255, value, value, value);
      }
    }

    return gfx::ImageSkia::CreateFromBitmap(bitmap, 1.0f);
  }());
  return *noise_image;
}

}  // namespace

MahoSidebarGrainOverlayView::MahoSidebarGrainOverlayView() {
  SetCanProcessEventsWithinSubtree(false);
}

MahoSidebarGrainOverlayView::~MahoSidebarGrainOverlayView() = default;

void MahoSidebarGrainOverlayView::SetTexture(float texture) {
  if (UpdateTexture(texture)) {
    SchedulePaint();
  }
}

void MahoSidebarGrainOverlayView::SetTextureWithoutRepaint(float texture) {
  UpdateTexture(texture);
}

void MahoSidebarGrainOverlayView::SetTileOrigin(
    const gfx::Point& tile_origin) {
  if (tile_origin_ == tile_origin) {
    return;
  }
  tile_origin_ = tile_origin;
  SchedulePaint();
}

bool MahoSidebarGrainOverlayView::UpdateTexture(float texture) {
  float clamped = std::clamp(texture, 0.0f, 1.0f);
  if (texture_ != clamped) {
    texture_ = clamped;
    return true;
  }
  return false;
}

void MahoSidebarGrainOverlayView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);

  if (texture_ <= 0.001f) {
    return;
  }

  const gfx::ImageSkia& noise = GetNoiseImage();
  const gfx::Rect bounds = GetLocalBounds();

  cc::PaintFlags flags;
  flags.setAlphaf(texture_ * 0.10f);
  flags.setBlendMode(SkBlendMode::kSoftLight);

  const int first_x =
      bounds.x() - ((bounds.x() - tile_origin_.x()) % kNoiseSize + kNoiseSize) %
                       kNoiseSize;
  const int first_y =
      bounds.y() - ((bounds.y() - tile_origin_.y()) % kNoiseSize + kNoiseSize) %
                       kNoiseSize;
  for (int y = first_y; y < bounds.bottom(); y += kNoiseSize) {
    for (int x = first_x; x < bounds.right(); x += kNoiseSize) {
      canvas->DrawImageInt(noise, x, y, flags);
    }
  }
}

BEGIN_METADATA(MahoSidebarGrainOverlayView)
END_METADATA

}  // namespace maho
