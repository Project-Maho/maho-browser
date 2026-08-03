// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/boost/maho_boost_visual_capture_test_support.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/threading/thread_restrictions.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/codec/png_codec.h"

namespace maho::boost::test {
namespace {

constexpr std::array<uint8_t, 8> kPngSignature = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

VisualCaptureValidationResult ValidationFailure(
    VisualCaptureValidationError error,
    std::string detail) {
  return {.error = error, .detail = std::move(detail)};
}

bool IsMissingPixel(SkColor color) {
  return SkColorGetA(color) == 0 ||
         (SkColorGetR(color) == 0 && SkColorGetG(color) == 0 &&
          SkColorGetB(color) == 0);
}

bool HasSuspiciousMissingBands(const SkBitmap& bitmap) {
  int missing_rows = 0;
  for (int y = 0; y < bitmap.height(); ++y) {
    bool row_missing = true;
    for (int x = 0; x < bitmap.width(); ++x) {
      if (!IsMissingPixel(bitmap.getColor(x, y))) {
        row_missing = false;
        break;
      }
    }
    missing_rows += row_missing ? 1 : 0;
  }

  int missing_columns = 0;
  for (int x = 0; x < bitmap.width(); ++x) {
    bool column_missing = true;
    for (int y = 0; y < bitmap.height(); ++y) {
      if (!IsMissingPixel(bitmap.getColor(x, y))) {
        column_missing = false;
        break;
      }
    }
    missing_columns += column_missing ? 1 : 0;
  }

  return missing_rows > 2 || missing_columns > 2;
}

}  // namespace

VisualCaptureValidationResult ValidateVisualCaptureArtifact(
    const base::FilePath& path,
    const gfx::Size& expected_size,
    base::Time minimum_mtime) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  if (!base::PathExists(path)) {
    return ValidationFailure(VisualCaptureValidationError::kMissing,
                             "capture does not exist");
  }

  const std::optional<std::vector<uint8_t>> bytes = base::ReadFileToBytes(path);
  if (!bytes) {
    return ValidationFailure(VisualCaptureValidationError::kUnreadable,
                             "capture could not be read");
  }
  if (bytes->size() < kPngSignature.size() ||
      !std::equal(kPngSignature.begin(), kPngSignature.end(), bytes->begin())) {
    return ValidationFailure(VisualCaptureValidationError::kWrongSignature,
                             "capture does not have the PNG signature");
  }

  const SkBitmap bitmap = gfx::PNGCodec::Decode(*bytes);
  if (bitmap.drawsNothing()) {
    return ValidationFailure(VisualCaptureValidationError::kDecodeFailed,
                             "capture could not be decoded as PNG");
  }
  if (bitmap.width() != expected_size.width() ||
      bitmap.height() != expected_size.height()) {
    return ValidationFailure(
        VisualCaptureValidationError::kWrongDimensions,
        "capture dimensions do not match the fixed WebUI content size");
  }

  base::File::Info info;
  if (!base::GetFileInfo(path, &info) || info.is_directory) {
    return ValidationFailure(VisualCaptureValidationError::kUnreadable,
                             "capture metadata could not be read");
  }
  if (!minimum_mtime.is_null() && info.last_modified <= minimum_mtime) {
    return ValidationFailure(VisualCaptureValidationError::kStale,
                             "capture predates the newest visual source");
  }
  if (HasSuspiciousMissingBands(bitmap)) {
    return ValidationFailure(
        VisualCaptureValidationError::kPartialFrame,
        "capture contains fully black or transparent compositor bands");
  }

  return {};
}

}  // namespace maho::boost::test
