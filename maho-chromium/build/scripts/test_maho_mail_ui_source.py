from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]


class MahoMailUiSourceTest(unittest.TestCase):
    def test_mail_webui_routes_remote_images_through_sanitized_proxy(self) -> None:
        source = (ROOT / "maho-chromium/browser/ui/webui/maho_mail/maho_mail_ui.cc").read_text()
        self.assertIn("std::make_unique<SanitizedImageSource>(profile)", source)
        self.assertIn(
            "default trusted img-src already allowlists chrome://image", source
        )
        self.assertNotIn("img-src 'self' chrome-res: https:", source)

    def test_active_mail_oauth_paths_use_bound_loopback_flow(self) -> None:
        mail = (ROOT / "maho-chromium/browser/ui/webui/maho_mail/maho_mail_page_handler.cc").read_text()
        welcome = (ROOT / "maho-chromium/browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc").read_text()
        settings = (ROOT / "maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc").read_text()

        for name, source in (("mail", mail), ("welcome", welcome), ("settings", settings)):
            with self.subTest(surface=name):
                self.assertIn("BuildMailOAuthStartOptionsJson", source)
                self.assertRegex(
                    source,
                    r"OAuthLoopbackSignIn\(\s*provider,\s*options_json,",
                )

        settings_method = settings.split(
            "void MahoSettingsPageHandler::MailBeginOAuth(", 1
        )[1].split(
            "void MahoSettingsPageHandler::MailOAuthComplete(", 1
        )[0]
        self.assertNotIn("service->OAuthStartUrl(", settings_method)

        settings_react = (ROOT / "maho-chromium/browser/resources/maho_settings/react/mail_accounts.tsx").read_text()
        self.assertIn("handler.mailBeginOAuth(provider)", settings_react)
        self.assertNotIn("mailOAuthStartUrl(provider, '', '')", settings_react)


if __name__ == "__main__":
    unittest.main()
