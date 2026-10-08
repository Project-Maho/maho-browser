#!/usr/bin/env python3
"""No-build contracts for the Library Downloads-icon progress indicator.

Two layers are covered without a Chromium checkout:

* the source contract that wires the throttled MahoDownloadBridgeService
  notifications into the rail icon through the owning sidebar, and
* the aggregation behaviour itself, compiled from the dependency-free helper
  header and executed with the host C++ compiler.
"""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import tempfile
import textwrap
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
SIDEBAR = ROOT / "maho-chromium/browser/ui/views/sidebar"
DATA_HEADER = SIDEBAR / "maho_sidebar_downloads_data.h"
DATA_SOURCE = SIDEBAR / "maho_sidebar_downloads_data.cc"
RAIL_HEADER = SIDEBAR / "maho_sidebar_library_rail_view.h"
RAIL_SOURCE = SIDEBAR / "maho_sidebar_library_rail_view.cc"
SIDEBAR_SOURCE = SIDEBAR / "maho_sidebar_view.cc"
SIDEBAR_HEADER = SIDEBAR / "maho_sidebar_view.h"
DOWNLOADS_SOURCE = SIDEBAR / "maho_sidebar_downloads_view.cc"
BUILD_GN = SIDEBAR / "BUILD.gn"

HEADER_INCLUDE = "maho-chromium/browser/ui/views/sidebar/maho_sidebar_downloads_data.h"

HARNESS = r"""
#include "%(header)s"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// DownloadItem's special members are declared in the header and defined
// out-of-line by maho_sidebar_downloads_data.cc. The harness must not link
// Chromium code, so it supplies the same defaulted definitions here.
namespace maho {
DownloadItem::DownloadItem() = default;
DownloadItem::DownloadItem(const DownloadItem&) = default;
DownloadItem::DownloadItem(DownloadItem&&) = default;
DownloadItem& DownloadItem::operator=(const DownloadItem&) = default;
DownloadItem& DownloadItem::operator=(DownloadItem&&) = default;
DownloadItem::~DownloadItem() = default;
}  // namespace maho

namespace {

int failures = 0;

void Check(bool condition, const char* name) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL " << name << "\n";
    return;
  }
  std::cout << "ok " << name << "\n";
}

maho::DownloadItem Make(const std::string& id,
                        const std::string& state,
                        uint64_t total_bytes,
                        uint64_t received_bytes) {
  maho::DownloadItem item;
  item.id = id;
  item.state = state;
  item.total_bytes = total_bytes;
  item.received_bytes = received_bytes;
  return item;
}

}  // namespace

int main() {
  using maho::ComputeDownloadsIndicatorState;
  using maho::DownloadsIndicatorState;

  const DownloadsIndicatorState empty = ComputeDownloadsIndicatorState({});
  Check(!empty.visible, "no_downloads_is_cleared");
  Check(empty.active_count == 0, "no_downloads_has_no_active_count");
  Check(empty.fraction == 0.0, "no_downloads_has_no_fraction");

  const DownloadsIndicatorState terminal = ComputeDownloadsIndicatorState({
      Make("a", "completed", 1000, 1000),
      Make("b", "cancelled", 1000, 10),
      Make("c", "failed", 1000, 10),
      Make("d", "interrupted", 1000, 10),
  });
  Check(!terminal.visible, "terminal_states_clear_the_indicator");
  Check(terminal.total_bytes == 0, "terminal_states_weight_nothing");

  const DownloadsIndicatorState multiple = ComputeDownloadsIndicatorState({
      Make("a", "downloading", 200, 100),
      Make("b", "downloading", 400, 300),
  });
  Check(multiple.visible, "multiple_downloads_are_visible");
  Check(multiple.active_count == 2, "multiple_downloads_count");
  Check(multiple.received_bytes == 400, "multiple_downloads_sum_received");
  Check(multiple.total_bytes == 600, "multiple_downloads_sum_total");
  Check(!multiple.indeterminate, "multiple_known_totals_are_determinate");

  const DownloadsIndicatorState unknown =
      ComputeDownloadsIndicatorState({Make("a", "downloading", 0, 4096)});
  Check(unknown.visible, "unknown_total_is_visible");
  Check(unknown.indeterminate, "unknown_total_is_indeterminate");
  Check(unknown.received_bytes == 0, "unknown_total_weights_no_bytes");
  Check(unknown.fraction == 0.0, "unknown_total_has_no_fraction");

  const DownloadsIndicatorState mixed = ComputeDownloadsIndicatorState({
      Make("a", "downloading", 1000, 500),
      Make("b", "downloading", 0, 250),
  });
  Check(mixed.indeterminate, "mixed_unknown_total_is_indeterminate");
  Check(mixed.received_bytes == 500, "mixed_keeps_known_bytes");
  Check(mixed.total_bytes == 1000, "mixed_keeps_known_total");

  const DownloadsIndicatorState clamped =
      ComputeDownloadsIndicatorState({Make("a", "downloading", 100, 400)});
  Check(clamped.fraction == 1.0, "fraction_is_clamped_to_one");

  const DownloadsIndicatorState paused =
      ComputeDownloadsIndicatorState({Make("a", "paused", 1000, 250)});
  Check(paused.visible, "paused_download_is_active");
  Check(paused.fraction == 0.25, "paused_download_keeps_progress");
  const DownloadsIndicatorState resumed =
      ComputeDownloadsIndicatorState({Make("a", "downloading", 1000, 250)});
  Check(resumed.fraction == paused.fraction,
        "resume_keeps_the_same_fraction");

  const DownloadsIndicatorState remaining = ComputeDownloadsIndicatorState({
      Make("a", "completed", 1000, 1000),
      Make("b", "downloading", 1000, 500),
  });
  Check(remaining.visible, "one_active_download_keeps_it_visible");
  Check(remaining.active_count == 1, "one_active_download_counts_once");
  Check(remaining.fraction == 0.5, "one_active_download_fraction");

  if (failures != 0) {
    std::cerr << failures << " aggregation checks failed\n";
    return 1;
  }
  std::cout << "all aggregation checks passed\n";
  return 0;
}
""" % {"header": HEADER_INCLUDE}


def _host_compiler() -> str | None:
  for candidate in ("clang++", "g++", "c++"):
    found = shutil.which(candidate)
    if found:
      return found
  return None


class DownloadsIndicatorSourceTest(unittest.TestCase):
  @classmethod
  def setUpClass(cls) -> None:
    cls.data_header = DATA_HEADER.read_text(encoding="utf-8")
    cls.data_source = DATA_SOURCE.read_text(encoding="utf-8")
    cls.rail_header = RAIL_HEADER.read_text(encoding="utf-8")
    cls.rail_source = RAIL_SOURCE.read_text(encoding="utf-8")
    cls.sidebar_source = SIDEBAR_SOURCE.read_text(encoding="utf-8")
    cls.sidebar_header = SIDEBAR_HEADER.read_text(encoding="utf-8")
    cls.downloads_source = DOWNLOADS_SOURCE.read_text(encoding="utf-8")
    cls.build_gn = BUILD_GN.read_text(encoding="utf-8")

  def test_aggregation_helper_is_dependency_free_and_shared(self) -> None:
    self.assertIn("struct DownloadsIndicatorState", self.data_header)
    self.assertIn("ComputeDownloadsIndicatorState(", self.data_header)
    self.assertIn("IsActiveDownloadState(", self.data_header)
    # The row progress bar and the rail indicator must agree on what is active.
    self.assertIn("IsActiveDownloadState(state)", self.downloads_source)
    self.assertNotIn('state == "downloading" || state == "paused"',
                     self.downloads_source)
    self.assertIn("DownloadsIndicatorAccessibleDescription(", self.data_source)
    self.assertIn("ui::FormatBytes(", self.data_source)

  def test_indicator_only_exists_on_the_downloads_category(self) -> None:
    self.assertIn("void SetDownloadsIndicatorState(", self.rail_header)
    self.assertIn("void MahoSidebarLibraryRailView::SetDownloadsIndicatorState(",
                  self.rail_source)
    create = self.rail_source.index("MahoSidebarDownloadsIndicatorView>();")
    guard = self.rail_source.rindex(
        "category == MahoSidebarLibraryRailView::Category::kDownloads", 0, create)
    self.assertGreater(guard, 0)
    self.assertIn("views::kViewIgnoredByLayoutKey, true", self.rail_source)

  def test_indicator_is_not_polling_and_carries_a_description(self) -> None:
    for forbidden in ("Timer", "PostDelayedTask", "base::Milliseconds"):
      self.assertNotIn(forbidden, self.rail_source)
    self.assertIn("GetViewAccessibility().SetDescription(", self.rail_source)
    self.assertIn("DownloadsIndicatorAccessibleDescription(", self.rail_source)
    self.assertIn("SetIsIgnored(true)", self.rail_source)
    self.assertIn("palette.row_selected", self.rail_source)
    self.assertIn("palette.focus_ring", self.rail_source)

  def test_sidebar_observes_and_releases_the_download_bridge(self) -> None:
    self.assertIn("public MahoDownloadBridgeService::Observer",
                  self.sidebar_header)
    self.assertIn("void OnMahoDownloadsChanged() override;", self.sidebar_header)
    self.assertIn("downloads_observation_{this}", self.sidebar_header)
    self.assertIn("downloads_observation_.Observe(service);", self.sidebar_source)
    self.assertIn("downloads_observation_.Reset();", self.sidebar_source)
    self.assertIn("ComputeDownloadsIndicatorState(ParseDownloads())",
                  self.sidebar_source)

  def test_sidebar_fails_closed_for_off_the_record_windows(self) -> None:
    self.assertIn("MahoPrivateCapability::kMahoDownloadMetadata",
                  self.sidebar_source)
    self.assertIn("is_otr_ ||", self.sidebar_source)
    self.assertIn("SetDownloadsIndicatorState(DownloadsIndicatorState());",
                  self.sidebar_source)
    self.assertNotIn("IsOffTheRecord", self.rail_source)

  def test_new_tests_are_registered_in_gn(self) -> None:
    for source in (
        "maho_sidebar_downloads_data_unittest.cc",
        "maho_sidebar_library_rail_view_unittest.cc",
        "maho_sidebar_downloads_indicator_wiring_unittest.cc",
    ):
      self.assertIn('"%s"' % source, self.build_gn)
    self.assertIn('"//maho/browser:maho_download_bridge"', self.build_gn)


class DownloadsIndicatorBehaviourTest(unittest.TestCase):
  def test_aggregation_header_behaves_as_documented(self) -> None:
    compiler = _host_compiler()
    if compiler is None:
      self.skipTest("no host C++ compiler available")
    with tempfile.TemporaryDirectory() as workdir:
      harness = pathlib.Path(workdir) / "indicator_harness.cc"
      harness.write_text(textwrap.dedent(HARNESS), encoding="utf-8")
      binary = pathlib.Path(workdir) / "indicator_harness"
      compile_result = subprocess.run(
          [compiler, "-std=c++17", "-I", str(ROOT), str(harness), "-o",
           str(binary)],
          capture_output=True,
          text=True,
      )
      self.assertEqual(0, compile_result.returncode, compile_result.stderr)
      run_result = subprocess.run([str(binary)], capture_output=True, text=True)
      self.assertEqual(0, run_result.returncode, run_result.stderr)
      self.assertIn("all aggregation checks passed", run_result.stdout)


if __name__ == "__main__":
  unittest.main()
