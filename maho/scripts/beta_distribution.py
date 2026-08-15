import argparse
import base64
import json
import re
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, padding
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature


APP_STORE_CONNECT_API = "https://api.appstoreconnect.apple.com/v1"
ANDROID_PUBLISHER_API = "https://androidpublisher.googleapis.com/androidpublisher/v3"
ANDROID_PUBLISHER_UPLOAD_API = "https://androidpublisher.googleapis.com/upload/androidpublisher/v3"
ANDROID_PUBLISHER_SCOPE = "https://www.googleapis.com/auth/androidpublisher"
EMAIL_PATTERN = re.compile(r"^[^\s@]+@[^\s@]+\.[^\s@]+$")


def normalize_email_list(value: str) -> list[str]:
    emails: list[str] = []
    seen: set[str] = set()
    for raw_email in re.split(r"[\s,]+", value.strip()):
        if not raw_email:
            continue
        email = raw_email.lower()
        if not EMAIL_PATTERN.fullmatch(email):
            raise ValueError(f"{raw_email!r} is not a valid email")
        if email not in seen:
            seen.add(email)
            emails.append(email)
    if not emails:
        raise ValueError("at least one tester email is required")
    return emails


def testflight_beta_tester_payload(
    email: str, app_id: str, beta_group_id: str
) -> dict[str, Any]:
    return {
        "data": {
            "type": "betaTesters",
            "attributes": {"email": email},
            "relationships": {
                "apps": {"data": [{"type": "apps", "id": app_id}]},
                "betaGroups": {
                    "data": [{"type": "betaGroups", "id": beta_group_id}]
                },
            },
        }
    }


def testflight_add_build_payload(build_id: str) -> dict[str, Any]:
    return {"data": [{"type": "builds", "id": build_id}]}


def testflight_beta_review_payload(build_id: str) -> dict[str, Any]:
    return {
        "data": {
            "type": "betaAppReviewSubmissions",
            "relationships": {"build": {"data": {"type": "builds", "id": build_id}}},
        }
    }


def testflight_build_query(app_id: str, build_version: str) -> str:
    return urllib.parse.urlencode(
        {"filter[app]": app_id, "filter[version]": build_version, "limit": "1"}
    )


def android_internal_release_payload(version_code: int) -> dict[str, Any]:
    return {
        "releases": [
            {
                "name": "Maho mobile beta",
                "status": "completed",
                "versionCodes": [str(version_code)],
            }
        ]
    }


def android_bundle_upload_url(package_name: str, edit_id: str) -> str:
    return f"{ANDROID_PUBLISHER_UPLOAD_API}/applications/{package_name}/edits/{edit_id}/bundles"


def require_internal_track(track: str) -> None:
    if track != "internal":
        raise ValueError("only the internal testing track is allowed")


def require_google_group(email: str) -> str:
    normalized = normalize_email_list(email)
    if len(normalized) != 1:
        raise ValueError("exactly one tester Google Group is required")
    return normalized[0]


def require_argument(value: str | None, name: str) -> str:
    if value:
        return value
    raise ValueError(f"{name} is required")


def urlsafe_base64(value: bytes) -> str:
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode("ascii")


def json_request(
    url: str,
    method: str,
    token: str,
    body: dict[str, Any] | None = None,
) -> dict[str, Any]:
    data = None if body is None else json.dumps(body).encode("utf-8")
    headers = {"Authorization": f"Bearer {token}", "Accept": "application/json"}
    if data is not None:
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"{method} {url} failed: {error.code} {detail}") from error


def app_store_connect_token(key_id: str, issuer_id: str, private_key_path: Path) -> str:
    header = urlsafe_base64(json.dumps({"alg": "ES256", "kid": key_id}).encode("utf-8"))
    now = int(time.time())
    claims = urlsafe_base64(
        json.dumps({"iss": issuer_id, "aud": "appstoreconnect-v1", "exp": now + 1140}).encode(
            "utf-8"
        )
    )
    signing_input = f"{header}.{claims}".encode("ascii")
    private_key = serialization.load_pem_private_key(private_key_path.read_bytes(), password=None)
    der_signature = private_key.sign(signing_input, ec.ECDSA(hashes.SHA256()))
    r_value, s_value = decode_dss_signature(der_signature)
    signature = r_value.to_bytes(32, "big") + s_value.to_bytes(32, "big")
    return f"{signing_input.decode('ascii')}.{urlsafe_base64(signature)}"


def app_store_connect_app_id(token: str, bundle_id: str) -> str:
    query = urllib.parse.urlencode({"filter[bundleId]": bundle_id})
    response = json_request(f"{APP_STORE_CONNECT_API}/apps?{query}", "GET", token)
    apps = response.get("data", [])
    if len(apps) != 1:
        raise RuntimeError(f"expected one App Store Connect app for {bundle_id}")
    return apps[0]["id"]


def app_store_connect_beta_group_id(token: str, app_id: str, group_name: str) -> str:
    query = urllib.parse.urlencode({"filter[app]": app_id, "filter[name]": group_name})
    response = json_request(f"{APP_STORE_CONNECT_API}/betaGroups?{query}", "GET", token)
    groups = response.get("data", [])
    if len(groups) == 1:
        return groups[0]["id"]
    if groups:
        raise RuntimeError(f"multiple beta groups named {group_name!r}")
    created = json_request(
        f"{APP_STORE_CONNECT_API}/betaGroups",
        "POST",
        token,
        {
            "data": {
                "type": "betaGroups",
                "attributes": {"name": group_name, "isInternalGroup": False},
                "relationships": {"app": {"data": {"type": "apps", "id": app_id}}},
            }
        },
    )
    return created["data"]["id"]


def app_store_connect_build_id(token: str, app_id: str, build_version: str) -> str:
    query = testflight_build_query(app_id, build_version)
    response = json_request(f"{APP_STORE_CONNECT_API}/builds?{query}", "GET", token)
    builds = response.get("data", [])
    if len(builds) != 1:
        raise RuntimeError(f"expected one processed build with version {build_version}")
    return builds[0]["id"]


def wait_for_app_store_connect_build_id(
    token: str, app_id: str, build_version: str, timeout_seconds: int = 1800
) -> str:
    deadline = time.monotonic() + timeout_seconds
    while True:
        try:
            return app_store_connect_build_id(token, app_id, build_version)
        except RuntimeError:
            if time.monotonic() >= deadline:
                raise RuntimeError(
                    f"TestFlight build {build_version} did not finish processing in {timeout_seconds} seconds"
                )
            time.sleep(30)


def app_store_connect_beta_tester_id(token: str, email: str) -> str | None:
    query = urllib.parse.urlencode({"filter[email]": email})
    response = json_request(f"{APP_STORE_CONNECT_API}/betaTesters?{query}", "GET", token)
    testers = response.get("data", [])
    if not testers:
        return None
    if len(testers) != 1:
        raise RuntimeError(f"multiple beta testers found for {email}")
    return testers[0]["id"]


def add_testflight_build_to_group(token: str, beta_group_id: str, build_id: str) -> None:
    json_request(
        f"{APP_STORE_CONNECT_API}/betaGroups/{beta_group_id}/relationships/builds",
        "POST",
        token,
        testflight_add_build_payload(build_id),
    )


def submit_testflight_beta_review(token: str, build_id: str) -> None:
    json_request(
        f"{APP_STORE_CONNECT_API}/betaAppReviewSubmissions",
        "POST",
        token,
        testflight_beta_review_payload(build_id),
    )


def add_testflight_tester_to_group(token: str, beta_group_id: str, tester_id: str) -> None:
    json_request(
        f"{APP_STORE_CONNECT_API}/betaGroups/{beta_group_id}/relationships/betaTesters",
        "POST",
        token,
        {"data": [{"type": "betaTesters", "id": tester_id}]},
    )


def invite_testflight_testers(
    key_id: str,
    issuer_id: str,
    private_key_path: Path,
    bundle_id: str,
    group_name: str,
    build_version: str,
    emails: list[str],
    submit_beta_review: bool,
) -> None:
    token = app_store_connect_token(key_id, issuer_id, private_key_path)
    app_id = app_store_connect_app_id(token, bundle_id)
    beta_group_id = app_store_connect_beta_group_id(token, app_id, group_name)
    build_id = wait_for_app_store_connect_build_id(token, app_id, build_version)
    add_testflight_build_to_group(token, beta_group_id, build_id)
    if submit_beta_review:
        submit_testflight_beta_review(token, build_id)
    for email in emails:
        tester_id = app_store_connect_beta_tester_id(token, email)
        if tester_id is None:
            json_request(
                f"{APP_STORE_CONNECT_API}/betaTesters",
                "POST",
                token,
                testflight_beta_tester_payload(email, app_id, beta_group_id),
            )
        else:
            add_testflight_tester_to_group(token, beta_group_id, tester_id)


def google_service_account_token(service_account_path: Path) -> str:
    account = json.loads(service_account_path.read_text())
    now = int(time.time())
    header = urlsafe_base64(json.dumps({"alg": "RS256", "typ": "JWT"}).encode("utf-8"))
    claims = urlsafe_base64(
        json.dumps(
            {
                "iss": account["client_email"],
                "scope": ANDROID_PUBLISHER_SCOPE,
                "aud": account["token_uri"],
                "iat": now,
                "exp": now + 3600,
            }
        ).encode("utf-8")
    )
    signing_input = f"{header}.{claims}".encode("ascii")
    private_key = serialization.load_pem_private_key(
        account["private_key"].encode("utf-8"), password=None
    )
    signature = private_key.sign(signing_input, padding.PKCS1v15(), hashes.SHA256())
    request = urllib.request.Request(
        account["token_uri"],
        data=urllib.parse.urlencode(
            {
                "grant_type": "urn:ietf:params:oauth:grant-type:jwt-bearer",
                "assertion": f"{signing_input.decode('ascii')}.{urlsafe_base64(signature)}",
            }
        ).encode("utf-8"),
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)["access_token"]


def android_request(
    url: str,
    method: str,
    token: str,
    body: dict[str, Any] | None = None,
    binary: bytes | None = None,
) -> dict[str, Any]:
    if body is not None and binary is not None:
        raise ValueError("body and binary are mutually exclusive")
    data = binary if binary is not None else (None if body is None else json.dumps(body).encode("utf-8"))
    headers = {"Authorization": f"Bearer {token}", "Accept": "application/json"}
    if body is not None:
        headers["Content-Type"] = "application/json"
    if binary is not None:
        headers["Content-Type"] = "application/octet-stream"
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=180 if binary is not None else 60) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"{method} {url} failed: {error.code} {detail}") from error


def publish_android_internal_beta(
    service_account_path: Path,
    package_name: str,
    aab_path: Path,
    tester_google_group: str,
) -> None:
    require_internal_track("internal")
    tester_google_group = require_google_group(tester_google_group)
    token = google_service_account_token(service_account_path)
    edit = android_request(
        f"{ANDROID_PUBLISHER_API}/applications/{package_name}/edits", "POST", token, {}
    )
    edit_id = edit["id"]
    base = f"{ANDROID_PUBLISHER_API}/applications/{package_name}/edits/{edit_id}"
    uploaded = android_request(
        android_bundle_upload_url(package_name, edit_id), "POST", token, binary=aab_path.read_bytes()
    )
    tester_response = android_request(f"{base}/testers/internal", "GET", token)
    groups = sorted(set(tester_response.get("googleGroups", [])) | {tester_google_group})
    android_request(f"{base}/testers/internal", "PATCH", token, {"googleGroups": groups})
    android_request(
        f"{base}/tracks/internal",
        "PUT",
        token,
        android_internal_release_payload(int(uploaded["versionCode"])),
    )
    android_request(f"{base}:commit?changesInReviewBehavior=ERROR_IF_IN_REVIEW", "POST", token)


def testflight_command(arguments: argparse.Namespace) -> None:
    emails = normalize_email_list(arguments.emails)
    invite_testflight_testers(
        arguments.key_id,
        arguments.issuer_id,
        Path(arguments.private_key_path),
        arguments.bundle_id,
        arguments.group_name,
        arguments.build_version,
        emails,
        arguments.submit_beta_review,
    )


def android_command(arguments: argparse.Namespace) -> None:
    publish_android_internal_beta(
        Path(arguments.service_account_path),
        arguments.package_name,
        Path(arguments.aab_path),
        arguments.tester_google_group,
    )


def preflight_command(arguments: argparse.Namespace) -> None:
    if arguments.platform == "ios":
        token = app_store_connect_token(
            require_argument(arguments.key_id, "--key-id"),
            require_argument(arguments.issuer_id, "--issuer-id"),
            Path(require_argument(arguments.private_key_path, "--private-key-path")),
        )
        bundle_id = require_argument(arguments.bundle_id, "--bundle-id")
        app_store_connect_app_id(token, bundle_id)
        print(f"ios-beta-preflight=pass bundle-id={bundle_id}")
        return
    service_account_path = Path(
        require_argument(arguments.service_account_path, "--service-account-path")
    )
    if not service_account_path.is_file():
        raise ValueError(f"Google Play service account file is missing: {service_account_path}")
    require_google_group(require_argument(arguments.tester_google_group, "--tester-google-group"))
    google_service_account_token(service_account_path)
    package_name = require_argument(arguments.package_name, "--package-name")
    print(f"android-beta-preflight=pass package-name={package_name}")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    commands = result.add_subparsers(required=True)
    testflight = commands.add_parser("testflight-invite")
    testflight.add_argument("--key-id", required=True)
    testflight.add_argument("--issuer-id", required=True)
    testflight.add_argument("--private-key-path", required=True)
    testflight.add_argument("--bundle-id", required=True)
    testflight.add_argument("--group-name", required=True)
    testflight.add_argument("--build-version", required=True)
    testflight.add_argument("--emails", required=True)
    testflight.add_argument("--submit-beta-review", action="store_true")
    testflight.set_defaults(handler=testflight_command)
    android = commands.add_parser("android-internal-beta")
    android.add_argument("--service-account-path", required=True)
    android.add_argument("--package-name", required=True)
    android.add_argument("--aab-path", required=True)
    android.add_argument("--tester-google-group", required=True)
    android.set_defaults(handler=android_command)
    preflight = commands.add_parser("preflight")
    preflight.add_argument("--platform", choices=("ios", "android"), required=True)
    preflight.add_argument("--bundle-id")
    preflight.add_argument("--key-id")
    preflight.add_argument("--issuer-id")
    preflight.add_argument("--private-key-path")
    preflight.add_argument("--package-name")
    preflight.add_argument("--service-account-path")
    preflight.add_argument("--tester-google-group")
    preflight.set_defaults(handler=preflight_command)
    return result


def main() -> None:
    arguments = parser().parse_args()
    arguments.handler(arguments)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, urllib.error.URLError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
