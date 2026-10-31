// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_VISUAL_CAPTURE_TEST_SUPPORT_H_
#define MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_VISUAL_CAPTURE_TEST_SUPPORT_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "maho/browser/ui/views/boost/maho_boost_interactive_test_support.h"
#include "ui/gfx/geometry/size.h"

namespace content {
class WebContents;
}

namespace maho::boost::test {

enum class VisualCaptureValidationError {
  kNone,
  kMissing,
  kUnreadable,
  kWrongSignature,
  kDecodeFailed,
  kWrongDimensions,
  kStale,
  kPartialFrame,
};

struct VisualCaptureValidationResult {
  VisualCaptureValidationError error = VisualCaptureValidationError::kNone;
  std::string detail;

  bool ok() const { return error == VisualCaptureValidationError::kNone; }
};

struct VisualCaptureSpec {
  VisualCaptureSpec();
  VisualCaptureSpec(const VisualCaptureSpec&);
  VisualCaptureSpec& operator=(const VisualCaptureSpec&);
  ~VisualCaptureSpec();

  std::string id;
  std::string owner_test;
  std::string surface;
  gfx::Size size;
  std::string filename;
  std::optional<std::string> reference;
  std::string frame;
};

VisualCaptureValidationResult ValidateVisualCaptureArtifact(
    const base::FilePath& path,
    const gfx::Size& expected_size,
    base::Time minimum_mtime);

class MahoBoostVisualCaptureTestBase : public MahoBoostInteractiveUiTest {
 public:
  MahoBoostVisualCaptureTestBase();
  ~MahoBoostVisualCaptureTestBase() override;

 protected:
  const std::vector<VisualCaptureSpec>& visual_capture_matrix();
  const std::vector<base::FilePath>& visual_source_paths();

  bool CaptureVisualState(content::WebContents* editor_web_contents,
                          std::string_view state_id);
  bool RefreshVisualCaptureManifest();

  base::FilePath visual_capture_directory() const;
  base::FilePath visual_contract_path() const;
  base::FilePath workspace_root() const;

 private:
  bool LoadVisualCaptureContract();
  std::optional<base::Time> LatestVisualSourceMtime() const;

  std::vector<VisualCaptureSpec> visual_capture_matrix_;
  std::vector<base::FilePath> visual_source_paths_;
  bool visual_capture_contract_loaded_ = false;
};

}  // namespace maho::boost::test

class MahoBoostVisualCaptureTest
    : public maho::boost::test::MahoBoostVisualCaptureTestBase {};

#endif  // MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_VISUAL_CAPTURE_TEST_SUPPORT_H_
