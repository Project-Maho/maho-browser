import unittest
from argparse import Namespace
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

from scripts.beta_distribution import (
    android_internal_release_payload,
    android_bundle_upload_url,
    normalize_email_list,
    preflight_command,
    invite_testflight_testers,
    publish_android_internal_beta,
    require_google_group,
    require_internal_track,
    testflight_add_build_payload,
    testflight_beta_review_payload,
    testflight_build_query,
    testflight_beta_tester_payload,
)


class BetaDistributionPayloadTest(unittest.TestCase):
    def test_normalizes_deduplicates_and_validates_tester_emails(self) -> None:
        self.assertEqual(
            normalize_email_list(" alpha@example.com, beta@example.com\nALPHA@example.com "),
            ["alpha@example.com", "beta@example.com"],
        )

    def test_rejects_invalid_tester_email(self) -> None:
        with self.assertRaisesRegex(ValueError, "valid email"):
            normalize_email_list("not-an-email")

    def test_builds_testflight_tester_payload_for_beta_group(self) -> None:
        self.assertEqual(
            testflight_beta_tester_payload(
                "tester@example.com", "app-123", "group-456"
            ),
            {
                "data": {
                    "type": "betaTesters",
                    "attributes": {"email": "tester@example.com"},
                    "relationships": {
                        "apps": {"data": [{"type": "apps", "id": "app-123"}]},
                        "betaGroups": {
                            "data": [{"type": "betaGroups", "id": "group-456"}]
                        },
                    },
                }
            },
        )

    def test_builds_internal_track_release_without_production_publish(self) -> None:
        self.assertEqual(
            android_internal_release_payload(42),
            {
                "releases": [
                    {
                        "name": "Maho mobile beta",
                        "status": "completed",
                        "versionCodes": ["42"],
                    }
                ]
            },
        )

    def test_uses_play_media_upload_endpoint_for_aab(self) -> None:
        self.assertEqual(
            android_bundle_upload_url("dev.maho.browser", "edit-123"),
            "https://androidpublisher.googleapis.com/upload/androidpublisher/v3/"
            "applications/dev.maho.browser/edits/edit-123/bundles",
        )

    def test_rejects_non_internal_android_tracks(self) -> None:
        with self.assertRaisesRegex(ValueError, "internal testing track"):
            require_internal_track("production")

    def test_requires_one_google_group_for_android_testers(self) -> None:
        self.assertEqual(require_google_group("testers@example.com"), "testers@example.com")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            require_google_group("one@example.com,two@example.com")

    def test_builds_testflight_group_build_relationship(self) -> None:
        self.assertEqual(
            testflight_add_build_payload("build-123"),
            {"data": [{"type": "builds", "id": "build-123"}]},
        )

    def test_builds_testflight_beta_review_submission(self) -> None:
        self.assertEqual(
            testflight_beta_review_payload("build-123"),
            {
                "data": {
                    "type": "betaAppReviewSubmissions",
                    "relationships": {
                        "build": {"data": {"type": "builds", "id": "build-123"}}
                    },
                }
            },
        )

    def test_build_lookup_filters_the_specific_app_and_build_number(self) -> None:
        self.assertEqual(
            testflight_build_query("app-123", "42"),
            "filter%5Bapp%5D=app-123&filter%5Bversion%5D=42&limit=1",
        )

    def test_testflight_invite_links_build_reviews_and_existing_tester(self) -> None:
        requests: list[tuple[str, str, dict | None]] = []

        def request(url: str, method: str, token: str, body: dict | None = None) -> dict:
            requests.append((url, method, body))
            if "/apps?" in url:
                return {"data": [{"id": "app-123"}]}
            if "/betaGroups?" in url:
                return {"data": [{"id": "group-456"}]}
            if "/builds?" in url:
                return {"data": [{"id": "build-789"}]}
            if "/betaTesters?" in url:
                return {"data": [{"id": "tester-321"}]}
            return {"data": {}}

        with patch("scripts.beta_distribution.app_store_connect_token", return_value="token"), patch(
            "scripts.beta_distribution.json_request", side_effect=request
        ):
            invite_testflight_testers(
                "key", "issuer", Path("unused.p8"), "dev.maho.browser", "Maho Beta", "42",
                ["tester@example.com"], True,
            )

        urls = [url for url, _, _ in requests]
        self.assertIn(
            "https://api.appstoreconnect.apple.com/v1/betaGroups/group-456/relationships/builds",
            urls,
        )
        self.assertIn(
            "https://api.appstoreconnect.apple.com/v1/betaAppReviewSubmissions", urls
        )
        self.assertIn(
            "https://api.appstoreconnect.apple.com/v1/betaGroups/group-456/relationships/betaTesters",
            urls,
        )

    def test_android_internal_beta_only_uploads_to_internal_track(self) -> None:
        requests: list[tuple[str, str, dict | None, bytes | None]] = []

        def request(
            url: str,
            method: str,
            token: str,
            body: dict | None = None,
            binary: bytes | None = None,
        ) -> dict:
            requests.append((url, method, body, binary))
            if url.endswith("/edits"):
                return {"id": "edit-123"}
            if url.endswith("/bundles"):
                return {"versionCode": 42}
            if url.endswith("/testers/internal") and method == "GET":
                return {"googleGroups": ["existing@example.com"]}
            return {}

        with TemporaryDirectory() as directory:
            aab_path = Path(directory) / "Maho.aab"
            aab_path.write_bytes(b"aab")
            with patch("scripts.beta_distribution.google_service_account_token", return_value="token"), patch(
                "scripts.beta_distribution.android_request", side_effect=request
            ):
                publish_android_internal_beta(
                    Path("unused.json"), "dev.maho.browser", aab_path, "testers@example.com"
                )

        urls = [url for url, _, _, _ in requests]
        self.assertTrue(all("/production" not in url for url in urls))
        self.assertIn(
            "https://androidpublisher.googleapis.com/androidpublisher/v3/"
            "applications/dev.maho.browser/edits/edit-123/tracks/internal",
            urls,
        )
        self.assertIn(
            "https://androidpublisher.googleapis.com/androidpublisher/v3/"
            "applications/dev.maho.browser/edits/edit-123:commit?"
            "changesInReviewBehavior=ERROR_IF_IN_REVIEW",
            urls,
        )

    def test_android_preflight_fails_before_network_without_service_account(self) -> None:
        with self.assertRaisesRegex(ValueError, "service account file is missing"):
            preflight_command(
                Namespace(
                    platform="android",
                    service_account_path="missing-play-service-account.json",
                    tester_google_group="testers@example.com",
                    package_name="dev.maho.browser",
                )
            )

    def test_ios_preflight_requires_app_store_api_arguments(self) -> None:
        with self.assertRaisesRegex(ValueError, "--key-id is required"):
            preflight_command(Namespace(platform="ios", key_id=None, issuer_id=None, private_key_path=None, bundle_id=None))


class BetaWorkflowTest(unittest.TestCase):
    def load_workflow(self, name: str) -> str:
        return Path(__file__).parents[2].joinpath(".github", "workflows", name).read_text()

    def test_ios_workflow_is_manual_testflight_beta_only(self) -> None:
        workflow = self.load_workflow("beta-ios.yml")
        distribution = Path(__file__).with_name("beta_distribution.py").read_text()
        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("testflight-invite", workflow)
        self.assertIn("--bundle-id com.maho.browser", workflow)
        self.assertNotIn("--bundle-id dev.maho.browser", workflow)
        self.assertIn('CURRENT_PROJECT_VERSION="${{ github.run_number }}"', workflow)
        self.assertIn("fastlane pilot upload", workflow)
        self.assertIn("--api_key_path", workflow)
        self.assertNotIn("xcrun altool", workflow)
        self.assertIn("scripts/pin-ffi.sh", workflow)
        self.assertIn("--submit-beta-review", workflow)
        self.assertIn("wait_for_app_store_connect_build_id", distribution)
        self.assertNotIn("appStoreVersion", workflow)
        self.assertNotIn("submit_for_review", workflow)

    def test_android_workflow_is_manual_internal_track_only(self) -> None:
        workflow = self.load_workflow("beta-android.yml")
        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("android-internal-beta", workflow)
        self.assertIn("tester_google_group", workflow)
        self.assertIn('-PversionCode="${{ github.run_number }}"', workflow)
        self.assertIn("android-actions/setup-android", workflow)
        self.assertNotIn("production", workflow)
        self.assertNotIn("rollout", workflow)

    def test_android_build_uses_optional_ci_version_code(self) -> None:
        build_file = Path(__file__).parents[1].joinpath(
            "android-shell", "app", "build.gradle.kts"
        ).read_text()
        self.assertIn('providers.gradleProperty("versionCode")', build_file)

if __name__ == "__main__":
    unittest.main()
