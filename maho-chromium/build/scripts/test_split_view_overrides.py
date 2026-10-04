import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from apply_chromium_src_overrides import REPLACEMENTS, apply_replacements


class SplitViewOverridesTest(unittest.TestCase):
    def test_pristine_upstream_supports_four_panes_after_idempotent_apply(self):
        fixtures = Path(__file__).parent / "testdata" / "split_upstream"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(fixtures / "components", root / "components")
            for suffix in ("h", "cc"):
                relative = f"components/split_tabs/split_tab_visual_data.{suffix}"
                target = root / relative
                self.assertIn(relative, REPLACEMENTS)
                changed, _ = apply_replacements(
                    target, REPLACEMENTS[relative], relative_path=relative
                )
                self.assertTrue(changed)
                once = target.read_bytes()
                changed, _ = apply_replacements(
                    target, REPLACEMENTS[relative], relative_path=relative
                )
                self.assertFalse(changed)
                self.assertEqual(once, target.read_bytes())

            source = root / "four_panes.cc"
            source.write_text(
                r"""
#include "components/split_tabs/split_tab_visual_data.h"
#include <cassert>
#include <vector>

int main() {
  using namespace split_tabs;
  auto data = SplitTabVisualData::CreateTwoPane(SplitTabLayout::kVertical, 0.5);
  assert(data.ValidateForPaneCount(2));
  assert(data.InsertPaneAtLeaf(0, SplitTabLayout::kHorizontal, 0.4,
                               SplitPaneInsertionSide::kAfter));
  assert(data.InsertPaneAtLeaf(2, SplitTabLayout::kHorizontal, 0.6,
                               SplitPaneInsertionSide::kBefore));
  assert(data.ValidateForPaneCount(4));
  assert((data.layout_tree()->LeafOrder() == std::vector<size_t>{0, 1, 2, 3}));
  auto copy = data;
  assert(!data.InsertPaneAtLeaf(0, SplitTabLayout::kVertical, 0.5,
                                SplitPaneInsertionSide::kAfter));
  assert(data == copy);
  assert(data.RemovePane(1));
  assert(data.ValidateForPaneCount(3));
  assert(copy.ValidateForPaneCount(4));
  assert(data.RemovePane(2));
  assert(data.ValidateForPaneCount(2));
  assert(!data.RemovePane(0));
}
""",
                encoding="utf-8",
            )
            executable = root / "four_panes"
            subprocess.run(
                [
                    os.environ.get("CXX", "c++"),
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(root),
                    str(source),
                    str(root / "components/split_tabs/split_tab_visual_data.cc"),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
                timeout=60,
            )
            subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
