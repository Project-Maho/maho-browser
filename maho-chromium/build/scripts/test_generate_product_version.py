#!/usr/bin/env python3

from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest

import generate_product_version


class TestGenerateProductVersion(unittest.TestCase):

    def test_generate_header_content_format(self):
        content = generate_product_version.generate_header_content("2026.9.27")
        self.assertIn('kMahoProductVersion[] = "2026.9.27";', content)
        self.assertIn("inline base::Version GetMahoProductVersion()", content)
        self.assertIn("namespace maho {", content)
        self.assertIn("namespace updates {", content)

    def test_find_version_txt_default(self):
        # Must not assert a literal release number: version.txt is bumped on
        # every release, so a hardcoded value here makes each release ship a red
        # test. Assert resolution + shape instead.
        version_path = generate_product_version.find_version_txt()
        self.assertTrue(version_path.is_file())
        self.assertEqual(version_path.parent.name, "maho")
        self.assertEqual(version_path.name, "version.txt")
        version = generate_product_version.read_version(version_path)
        self.assertRegex(version, r"^\d+(\.\d+)+$")

    def test_write_file_if_changed_and_depfile(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            out_path = Path(temp_dir) / "gen" / "maho_product_version.h"
            dep_path = Path(temp_dir) / "gen" / "maho_product_version.d"
            version_path = Path(temp_dir) / "version.txt"
            version_path.write_text("2026.9.99\n", encoding="utf-8")

            argv = [
                "--version-file", str(version_path),
                "--output", str(out_path),
                "--depfile", str(dep_path),
            ]
            old_argv = generate_product_version.sys.argv
            try:
                generate_product_version.sys.argv = ["generate_product_version.py"] + argv
                exit_code = generate_product_version.main()
                self.assertEqual(exit_code, 0)
            finally:
                generate_product_version.sys.argv = old_argv

            self.assertTrue(out_path.is_file())
            content = out_path.read_text(encoding="utf-8")
            self.assertIn('kMahoProductVersion[] = "2026.9.99";', content)

            self.assertTrue(dep_path.is_file())
            dep_content = dep_path.read_text(encoding="utf-8")
            self.assertIn(str(out_path), dep_content)
            self.assertIn(str(version_path), dep_content)

            mtime_before = out_path.stat().st_mtime_ns
            changed = generate_product_version.write_file_if_changed(out_path, content)
            self.assertFalse(changed)
            mtime_after = out_path.stat().st_mtime_ns
            self.assertEqual(mtime_before, mtime_after)


if __name__ == "__main__":
    unittest.main()
