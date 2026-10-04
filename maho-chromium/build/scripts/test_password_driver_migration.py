import tempfile
import unittest
from pathlib import Path

from apply_chromium_src_overrides import REPLACEMENTS, apply_replacements


class PasswordDriverMigrationTest(unittest.TestCase):
    def test_old_content_definition_is_removed_without_changing_navigation(self):
        legacy = REPLACEMENTS[
            "components/password_manager/core/browser/stub_password_manager_driver.cc"
        ][0].old
        relative = (
            "components/password_manager/content/browser/"
            "content_password_manager_driver.cc"
        )
        navigation = (
            "void ContentPasswordManagerDriver::DidNavigate() {\n"
            "  RotatePasswordManagerDocumentToken();\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "content_password_manager_driver.cc"
            target.write_text(legacy + navigation, encoding="utf-8")
            apply_replacements(target, REPLACEMENTS[relative], relative_path=relative)
            self.assertEqual(target.read_text(encoding="utf-8"), navigation)
            changed, _ = apply_replacements(
                target, REPLACEMENTS[relative], relative_path=relative
            )
            self.assertFalse(changed)


if __name__ == "__main__":
    unittest.main()
