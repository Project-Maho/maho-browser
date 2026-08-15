# Mobile beta distribution

Maho mobile beta distribution is limited to TestFlight and Google Play internal
testing. It does not create an App Store submission or a Google Play production
release.

## iOS TestFlight

Run `Beta iOS TestFlight` manually from the repository's Actions page. The
workflow creates or uses the supplied external beta group, attaches the processed
build, invites the supplied email addresses, and submits the build for Apple beta
review. Apple approval is required before external testers can install it.

Before the first run:

1. Use the existing `com.maho.browser` App ID when creating the Maho app record
   in App Store Connect. Do not create a separate `dev.maho.browser` beta app.
2. Complete the app's TestFlight beta review contact and test-information fields.
3. Configure `APP_STORE_CONNECT_KEY_ID`, `APP_STORE_CONNECT_ISSUER_ID`, and
   `APP_STORE_CONNECT_PRIVATE_KEY` in the protected `mobile-beta` GitHub
   environment.
4. Run the read-only preflight:

```bash
cd maho
python3 scripts/beta_distribution.py preflight \
  --platform ios \
  --key-id "$APP_STORE_CONNECT_KEY_ID" \
  --issuer-id "$APP_STORE_CONNECT_ISSUER_ID" \
  --private-key-path "$APP_STORE_CONNECT_PRIVATE_KEY_PATH" \
  --bundle-id com.maho.browser
```

The workflow stops before archiving if the app record is missing or the API key
cannot read it.

## Android internal testing lane

Android beta access uses the Google Play internal testing track. The Play API
only supports Google Groups for testers, not individual email lists; the group
owner manages individual membership separately.

Before the first run:

1. Create `dev.maho.browser` in Google Play Console and enable the internal
   testing track.
2. Create a Google Group for the tester cohort.
3. Create a Play Developer API service account with the minimum permissions to
   manage the app's internal testing track, then configure its JSON key as
   `GOOGLE_PLAY_SERVICE_ACCOUNT_JSON` in the protected `mobile-beta` environment.
4. Configure the Android signing secrets used by the workflow.
5. Run the read-only credential preflight:

```bash
cd maho
python3 scripts/beta_distribution.py preflight \
  --platform android \
  --service-account-path "$GOOGLE_PLAY_SERVICE_ACCOUNT_PATH" \
  --package-name dev.maho.browser \
  --tester-google-group mobile-beta-testers@example.com
```

The `Beta Android Internal Testing` workflow uploads only to `internal`, adds the
given Google Group to that track, and commits that edit. It never uses the
production track.

## Store-release hold

Neither beta workflow submits an App Store release, requests an App Store review,
or targets the Google Play production track. Store launch remains a separate,
explicit release decision.
