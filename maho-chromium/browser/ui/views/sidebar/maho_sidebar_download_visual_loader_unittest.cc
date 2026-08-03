// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_download_visual_loader.h"

#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "content/public/test/browser_task_environment.h"
#include "services/data_decoder/public/cpp/test_support/in_process_data_decoder.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

class MahoSidebarDownloadVisualLoaderTest : public ::testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  data_decoder::test::InProcessDataDecoder in_process_data_decoder_;
  base::ScopedTempDir temp_dir_;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  base::FilePath CreateTempPngFile() {
    base::FilePath path = temp_dir_.GetPath().AppendASCII("test.png");
    SkBitmap bitmap;
    bitmap.allocN32Pixels(10, 10);
    bitmap.eraseColor(SK_ColorRED);
    std::optional<std::vector<uint8_t>> png_data =
        gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false);
    EXPECT_TRUE(png_data.has_value());
    bool success = base::WriteFile(path, *png_data);
    EXPECT_TRUE(success);
    return path;
  }

  base::FilePath CreateTempCorruptFile() {
    base::FilePath path = temp_dir_.GetPath().AppendASCII("corrupt.png");
    static const uint8_t kCorruptBytes[] = { 0x00, 0x01, 0x02, 0x03 };
    bool success = base::WriteFile(path, kCorruptBytes);
    EXPECT_TRUE(success);
    return path;
  }
};

TEST_F(MahoSidebarDownloadVisualLoaderTest, SuccessLoadsThumbnail) {
  base::FilePath png_path = CreateTempPngFile();
  MahoSidebarDownloadVisualLoader loader;
  base::RunLoop run_loop;
  bool called = false;

  loader.Start(png_path, 28, base::BindLambdaForTesting([&](const gfx::Image& image) {
    called = true;
    EXPECT_FALSE(image.IsEmpty());
    run_loop.Quit();
  }));

  run_loop.Run();
  EXPECT_TRUE(called);
}

TEST_F(MahoSidebarDownloadVisualLoaderTest, FailureReturnsEmptyImage) {
  base::FilePath corrupt_path = CreateTempCorruptFile();
  MahoSidebarDownloadVisualLoader loader;
  base::RunLoop run_loop;
  bool called = false;

  loader.Start(corrupt_path, 28, base::BindLambdaForTesting([&](const gfx::Image& image) {
    called = true;
    EXPECT_TRUE(image.IsEmpty());
    run_loop.Quit();
  }));

  run_loop.Run();
  EXPECT_TRUE(called);
}

TEST_F(MahoSidebarDownloadVisualLoaderTest, EmptyPathOrNonImageReturnsEmptyImage) {
  MahoSidebarDownloadVisualLoader loader;
  base::RunLoop run_loop;
  bool called = false;

  loader.Start(base::FilePath(), 28, base::BindLambdaForTesting([&](const gfx::Image& image) {
    called = true;
    EXPECT_TRUE(image.IsEmpty());
    run_loop.Quit();
  }));

  run_loop.Run();
  EXPECT_TRUE(called);
}

TEST_F(MahoSidebarDownloadVisualLoaderTest, DestroyedBeforeCompletion) {
  base::FilePath png_path = CreateTempPngFile();
  bool called = false;

  {
    MahoSidebarDownloadVisualLoader loader;
    loader.Start(png_path, 28, base::BindLambdaForTesting([&](const gfx::Image& image) {
      called = true;
    }));
  }

  task_environment_.RunUntilIdle();
  EXPECT_FALSE(called);
}

}  // namespace maho
