// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/boost/maho_boost_visual_capture_test_support.h"

#include <map>
#include <set>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "content/public/test/browser_test.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/gfx/codec/png_codec.h"

namespace {

using maho::boost::test::ValidateVisualCaptureArtifact;
using maho::boost::test::VisualCaptureValidationError;

bool WritePngFixture(const base::FilePath& path,
                     const gfx::Size& size,
                     bool partial) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  SkBitmap bitmap;
  bitmap.allocN32Pixels(size.width(), size.height());
  bitmap.eraseColor(SK_ColorWHITE);
  if (partial) {
    bitmap.erase(SK_ColorBLACK,
                 SkIRect::MakeXYWH(0, 0, size.width(), 64));
  }
  const auto png = gfx::PNGCodec::EncodeBGRASkBitmap(
      bitmap, /*discard_transparency=*/false);
  return png && base::WriteFile(path, *png);
}

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoBoostVisualCaptureTest,
                       CaptureContractEnumeratesEveryRequiredState) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const auto& matrix = visual_capture_matrix();
  EXPECT_EQ(matrix.size(), 105u);

  std::set<std::string> ids;
  std::set<std::string> filenames;
  std::map<std::string, size_t> owner_counts;
  int reference_diffs = 0;
  for (const auto& state : matrix) {
    EXPECT_TRUE(ids.insert(state.id).second) << state.id;
    EXPECT_TRUE(filenames.insert(state.filename).second) << state.filename;
    ++owner_counts[state.owner_test];
    if (state.surface == "boost") {
      EXPECT_EQ(state.size, gfx::Size(184, 582)) << state.id;
    } else {
      EXPECT_EQ(state.surface, "code") << state.id;
      EXPECT_EQ(state.size, gfx::Size(452, 582)) << state.id;
    }
    reference_diffs += state.reference.has_value() ? 1 : 0;
  }
  EXPECT_EQ(reference_diffs, 2);
  EXPECT_EQ(owner_counts["TitleStripMatchesReference"], 26u);
  EXPECT_EQ(owner_counts["ColorWorkspaceMatchesReference"], 20u);
  EXPECT_EQ(owner_counts["TypographyAndUtilityRowsMatchReference"], 28u);
  EXPECT_EQ(owner_counts["CodeRouteMatchesReference"], 21u);
  EXPECT_EQ(owner_counts["KeyboardAndDragRegions"], 10u);
  EXPECT_EQ(owner_counts.size(), 5u);
  EXPECT_EQ(visual_source_paths().size(), 17u);
  for (const base::FilePath& source : visual_source_paths()) {
    EXPECT_TRUE(base::PathExists(source)) << source;
  }
}

IN_PROC_BROWSER_TEST_F(
    MahoBoostVisualCaptureTest,
    CaptureValidationRejectsStaleWrongSizedCorruptAndPartialFrames) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const gfx::Size expected_size(184, 582);

  const base::FilePath valid_path = temp_dir.GetPath().AppendASCII("valid.png");
  ASSERT_TRUE(WritePngFixture(valid_path, expected_size, false));
  EXPECT_TRUE(ValidateVisualCaptureArtifact(valid_path, expected_size,
                                            base::Time())
                  .ok());

  const auto stale = ValidateVisualCaptureArtifact(
      valid_path, expected_size, base::Time::Now() + base::Seconds(1));
  EXPECT_EQ(stale.error, VisualCaptureValidationError::kStale);

  const base::FilePath wrong_size_path =
      temp_dir.GetPath().AppendASCII("wrong-size.png");
  ASSERT_TRUE(WritePngFixture(wrong_size_path, gfx::Size(10, 10), false));
  const auto wrong_size = ValidateVisualCaptureArtifact(
      wrong_size_path, expected_size, base::Time());
  EXPECT_EQ(wrong_size.error,
            VisualCaptureValidationError::kWrongDimensions);

  const base::FilePath corrupt_path =
      temp_dir.GetPath().AppendASCII("corrupt.png");
  ASSERT_TRUE(base::WriteFile(corrupt_path,
                              "\x89PNG\r\n\x1a\ntruncated"));
  const auto corrupt = ValidateVisualCaptureArtifact(
      corrupt_path, expected_size, base::Time());
  EXPECT_EQ(corrupt.error, VisualCaptureValidationError::kDecodeFailed);

  const base::FilePath wrong_signature_path =
      temp_dir.GetPath().AppendASCII("wrong-signature.png");
  ASSERT_TRUE(base::WriteFile(wrong_signature_path, "not a png"));
  const auto wrong_signature = ValidateVisualCaptureArtifact(
      wrong_signature_path, expected_size, base::Time());
  EXPECT_EQ(wrong_signature.error,
            VisualCaptureValidationError::kWrongSignature);

  const base::FilePath partial_path =
      temp_dir.GetPath().AppendASCII("partial.png");
  ASSERT_TRUE(WritePngFixture(partial_path, expected_size, true));
  const auto partial = ValidateVisualCaptureArtifact(
      partial_path, expected_size, base::Time());
  EXPECT_EQ(partial.error, VisualCaptureValidationError::kPartialFrame);
}
