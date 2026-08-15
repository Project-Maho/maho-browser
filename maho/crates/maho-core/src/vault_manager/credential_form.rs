//! Browser-form detail carried alongside a login record so a Chromium
//! `password_manager::PasswordForm` survives store -> Vault -> store without
//! field loss (Task 4).
//!
//! These fields are NOT public metadata: they live inside the encrypted record
//! envelope (`VaultRecordPayload::form_details`) and are surfaced only to the
//! privileged password-store backend session, never through
//! `VaultItemPublicDto` (list/search/settings). `notes` can carry user text, so
//! the whole struct is `Zeroize`/`ZeroizeOnDrop` like the rest of the record.
//!
//! Every field is `#[serde(default)]`: a record written before this struct
//! existed decodes to `form_details: None`, and a record written by an older
//! build that lacks an individual field decodes that field to its default.
//! Legacy records therefore round-trip as "no stored form detail", and the
//! consumer (the C++ adapter) applies Chromium's own defaults.

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use zeroize::{Zeroize, ZeroizeOnDrop};

const CHROMIUM_WINDOWS_EPOCH_OFFSET_MICROS: i64 = 11_644_473_600_000_000;

mod chromium_time_option {
    use chrono::{DateTime, Utc};
    use serde::{Deserialize, Deserializer, Serializer};

    use super::CHROMIUM_WINDOWS_EPOCH_OFFSET_MICROS;

    pub(super) fn serialize<S>(
        value: &Option<DateTime<Utc>>,
        serializer: S,
    ) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let Some(value) = value else {
            return serializer.serialize_none();
        };
        if value.timestamp_subsec_nanos() % 1_000 != 0 {
            return Err(<S::Error as serde::ser::Error>::custom(
                "Chromium time must have microsecond precision",
            ));
        }
        let internal_value = value
            .timestamp_micros()
            .checked_add(CHROMIUM_WINDOWS_EPOCH_OFFSET_MICROS)
            .ok_or_else(|| {
                <S::Error as serde::ser::Error>::custom("Chromium internal time is out of range")
            })?;
        serializer.serialize_some(&internal_value.to_string())
    }

    pub(super) fn deserialize<'de, D>(deserializer: D) -> Result<Option<DateTime<Utc>>, D::Error>
    where
        D: Deserializer<'de>,
    {
        let Some(value) = Option::<String>::deserialize(deserializer)? else {
            return Ok(None);
        };
        let internal_value = value.parse::<i64>().map_err(|_| {
            <D::Error as serde::de::Error>::custom(
                "Chromium internal time must be a signed decimal string",
            )
        })?;
        let unix_micros = internal_value
            .checked_sub(CHROMIUM_WINDOWS_EPOCH_OFFSET_MICROS)
            .ok_or_else(|| {
                <D::Error as serde::de::Error>::custom("Chromium internal time is out of range")
            })?;
        DateTime::<Utc>::from_timestamp_micros(unix_micros)
            .map(Some)
            .ok_or_else(|| {
                <D::Error as serde::de::Error>::custom(
                    "Chromium internal time is outside the supported date range",
                )
            })
    }
}

mod decimal_u64 {
    use serde::{Deserialize, Deserializer, Serializer};

    pub(super) fn serialize<S>(value: &u64, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_str(&value.to_string())
    }

    pub(super) fn deserialize<'de, D>(deserializer: D) -> Result<u64, D::Error>
    where
        D: Deserializer<'de>,
    {
        String::deserialize(deserializer)?
            .parse::<u64>()
            .map_err(|_| {
                <D::Error as serde::de::Error>::custom(
                    "renderer id must be an unsigned decimal string",
                )
            })
    }
}

/// One alternative username element observed on the form.
#[derive(Clone, Default, Serialize, Deserialize, Zeroize, ZeroizeOnDrop)]
#[serde(rename_all = "camelCase")]
pub struct VaultAlternativeElement {
    #[serde(default)]
    pub value: String,
    #[serde(default, with = "decimal_u64")]
    pub field_renderer_id: u64,
    #[serde(default)]
    pub name: String,
}

/// One credential note (Chromium `PasswordNote`).
#[derive(Clone, Default, Serialize, Deserialize, Zeroize, ZeroizeOnDrop)]
#[serde(rename_all = "camelCase")]
pub struct VaultCredentialNote {
    #[serde(default)]
    pub unique_display_name: String,
    #[serde(default)]
    pub value: String,
    #[zeroize(skip)]
    #[serde(default, with = "chromium_time_option")]
    pub date_created: Option<DateTime<Utc>>,
    #[serde(default)]
    pub hide_by_default: bool,
}

/// Non-secret browser-form detail for a login record.
#[derive(Clone, Default, Serialize, Deserialize, Zeroize, ZeroizeOnDrop)]
#[serde(rename_all = "camelCase")]
pub struct VaultCredentialFormDetails {
    /// Chromium `PasswordForm::Scheme` as its integer value.
    #[serde(default)]
    pub scheme: u8,
    /// Exact signon realm as Chromium computed it (may be a non-web realm such
    /// as `android://` or a HTTP-auth realm, which `origins` cannot represent).
    #[serde(default)]
    pub signon_realm: String,
    #[serde(default)]
    pub url: String,
    #[serde(default)]
    pub action: String,
    #[serde(default)]
    pub federation_origin: String,
    #[serde(default)]
    pub submit_element: String,
    #[serde(default)]
    pub username_element: String,
    #[serde(default)]
    pub password_element: String,
    #[serde(default)]
    pub all_alternative_usernames: Vec<VaultAlternativeElement>,
    /// Chromium's own `date_created`, which is NOT the Vault row's `created_at`
    /// (the row records when Maho persisted the item; this records when the
    /// credential was created in the browser).
    #[zeroize(skip)]
    #[serde(default, with = "chromium_time_option")]
    pub date_created: Option<DateTime<Utc>>,
    #[zeroize(skip)]
    #[serde(default, with = "chromium_time_option")]
    pub date_last_used: Option<DateTime<Utc>>,
    #[zeroize(skip)]
    #[serde(default, with = "chromium_time_option")]
    pub date_password_modified: Option<DateTime<Utc>>,
    #[zeroize(skip)]
    #[serde(default, with = "chromium_time_option")]
    pub date_last_filled: Option<DateTime<Utc>>,
    #[zeroize(skip)]
    #[serde(default, with = "chromium_time_option")]
    pub date_received: Option<DateTime<Utc>>,
    #[serde(default)]
    pub blocked_by_user: bool,
    /// Chromium `PasswordForm::Type` as its integer value.
    #[serde(default)]
    pub credential_type: i32,
    #[serde(default)]
    pub times_used_in_html_form: i32,
    /// Chromium `PasswordForm::MatchType` bitmask, absent when unset.
    #[serde(default)]
    pub match_type: Option<u32>,
    /// Chromium `PasswordForm::Store` as its integer bitmask value.
    #[serde(default)]
    pub in_store: i32,
    #[serde(default)]
    pub skip_zero_click: bool,
    #[serde(default)]
    pub display_name: String,
    #[serde(default)]
    pub icon_url: String,
    /// Opaque JSON serialization of `autofill::FormData`, produced and consumed
    /// only by the C++ adapter. The Vault never interprets it.
    #[serde(default)]
    pub form_data: String,
    #[serde(default)]
    pub notes: Vec<VaultCredentialNote>,
}

#[cfg(test)]
mod tests {
    use chrono::{DateTime, Utc};
    use serde_json::{json, Value};

    use super::{VaultAlternativeElement, VaultCredentialFormDetails, VaultCredentialNote};

    fn time(unix_micros: i64) -> DateTime<Utc> {
        DateTime::from_timestamp_micros(unix_micros).expect("valid test timestamp")
    }

    #[test]
    fn chromium_internal_time_strings_round_trip_at_microsecond_precision() {
        let unix_micros = [
            1_700_000_000_123_456,
            1_700_000_001_234_567,
            1_700_000_002_345_678,
            1_700_000_003_456_789,
            1_700_000_004_567_890,
            1_700_000_005_678_901,
        ];
        let mut details = VaultCredentialFormDetails::default();
        details.date_created = Some(time(unix_micros[0]));
        details.date_last_used = Some(time(unix_micros[1]));
        details.date_last_filled = Some(time(unix_micros[2]));
        details.date_password_modified = Some(time(unix_micros[3]));
        details.date_received = Some(time(unix_micros[4]));
        let mut note = VaultCredentialNote::default();
        note.date_created = Some(time(unix_micros[5]));
        details.notes.push(note);

        let json = serde_json::to_value(&details).expect("serialize form details");
        assert_eq!(json["dateCreated"], "13344473600123456");
        assert_eq!(json["dateLastUsed"], "13344473601234567");
        assert_eq!(json["dateLastFilled"], "13344473602345678");
        assert_eq!(json["datePasswordModified"], "13344473603456789");
        assert_eq!(json["dateReceived"], "13344473604567890");
        assert_eq!(json["notes"][0]["dateCreated"], "13344473605678901");

        let decoded: VaultCredentialFormDetails =
            serde_json::from_value(json).expect("deserialize form details");
        assert_eq!(
            decoded
                .date_created
                .expect("date created")
                .timestamp_micros(),
            unix_micros[0]
        );
        assert_eq!(
            decoded
                .date_last_used
                .expect("date last used")
                .timestamp_micros(),
            unix_micros[1]
        );
        assert_eq!(
            decoded
                .date_last_filled
                .expect("date last filled")
                .timestamp_micros(),
            unix_micros[2]
        );
        assert_eq!(
            decoded
                .date_password_modified
                .expect("date password modified")
                .timestamp_micros(),
            unix_micros[3]
        );
        assert_eq!(
            decoded
                .date_received
                .expect("date received")
                .timestamp_micros(),
            unix_micros[4]
        );
        assert_eq!(
            decoded.notes[0]
                .date_created
                .expect("note date created")
                .timestamp_micros(),
            unix_micros[5]
        );
    }

    #[test]
    fn renderer_id_u64_max_round_trips_as_a_decimal_string() {
        let mut details = VaultCredentialFormDetails::default();
        let mut alternative = VaultAlternativeElement::default();
        alternative.field_renderer_id = u64::MAX;
        details.all_alternative_usernames.push(alternative);

        let json = serde_json::to_value(&details).expect("serialize form details");
        assert_eq!(
            json["allAlternativeUsernames"][0]["fieldRendererId"],
            Value::String(u64::MAX.to_string())
        );
        let decoded: VaultCredentialFormDetails =
            serde_json::from_value(json).expect("deserialize form details");
        assert_eq!(
            decoded.all_alternative_usernames[0].field_renderer_id,
            u64::MAX
        );
    }

    #[test]
    fn hidden_note_survives_form_details_json_round_trip() {
        let mut details = VaultCredentialFormDetails::default();
        let mut note = VaultCredentialNote::default();
        note.hide_by_default = true;
        details.notes.push(note);

        let json = serde_json::to_value(&details).expect("serialize form details");
        assert_eq!(json["notes"][0]["hideByDefault"], true);
        let decoded: VaultCredentialFormDetails =
            serde_json::from_value(json).expect("deserialize form details");
        assert!(decoded.notes[0].hide_by_default);
    }

    #[test]
    fn legacy_form_details_without_new_wire_fields_use_defaults() {
        let decoded: VaultCredentialFormDetails = serde_json::from_value(json!({
            "scheme": 1,
            "allAlternativeUsernames": [{
                "value": "legacy@example.test",
                "name": "legacy-user"
            }],
            "notes": [{
                "uniqueDisplayName": "legacy-note",
                "value": "legacy-value"
            }]
        }))
        .expect("legacy form details deserialize");

        assert!(decoded.date_created.is_none());
        assert!(decoded.date_last_used.is_none());
        assert!(decoded.date_last_filled.is_none());
        assert!(decoded.date_password_modified.is_none());
        assert!(decoded.date_received.is_none());
        assert_eq!(decoded.all_alternative_usernames[0].field_renderer_id, 0);
        assert!(decoded.notes[0].date_created.is_none());
        assert!(!decoded.notes[0].hide_by_default);
    }

    #[test]
    fn malformed_or_out_of_range_numeric_strings_fail_closed() {
        for value in [
            json!({"dateCreated": "not-a-number"}),
            json!({"dateCreated": "12.5"}),
            json!({"dateCreated": "9223372036854775808"}),
            json!({"dateCreated": "-9223372036854775808"}),
            json!({"dateCreated": 13344473600123456_i64}),
        ] {
            assert!(serde_json::from_value::<VaultCredentialFormDetails>(value).is_err());
        }

        for value in [
            json!({"allAlternativeUsernames": [{"fieldRendererId": "-1"}]}),
            json!({"allAlternativeUsernames": [{"fieldRendererId": "12.5"}]}),
            json!({
                "allAlternativeUsernames": [{
                    "fieldRendererId": "18446744073709551616"
                }]
            }),
            json!({"allAlternativeUsernames": [{"fieldRendererId": 42}]}),
        ] {
            assert!(serde_json::from_value::<VaultCredentialFormDetails>(value).is_err());
        }
    }
}
