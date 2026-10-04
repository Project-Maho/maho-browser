use std::str::FromStr;

use chrono::Utc;
use maho_types::vault::{
    TotpAlgorithm, TotpDigits, TotpMetadata, TotpPeriodSeconds, VaultHealthReport, VaultItemId,
    VaultItemKind, VaultItemPublicDto, VaultRangeDeleteResult, VaultRevision, VaultSchemaVersion,
    VaultTotpCode,
};
use zeroize::Zeroizing;

use super::crud_mutate_use::{
    active_item, encrypt_updated_row, ensure_revision, generate_totp, persist_update,
};
use super::repository::VaultRepository;
use super::VaultRuntime;
use crate::vault_manager::record::{public_dto, RecordSecret, VaultRecordPayload};
use crate::vault_manager::VaultCrudError;

impl VaultRuntime {
    pub(crate) fn mutate_record(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
        revision: VaultRevision,
        mutate: impl FnOnce(&mut VaultRecordPayload) -> Result<(), VaultCrudError>,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        self.with_manager(|manager| {
            let (row, mut record) = active_item(manager, repository, &id)?;
            ensure_revision(&row, revision)?;
            mutate(&mut record)?;
            record.metadata.has_notes = record.has_notes();
            let updated = encrypt_updated_row(manager, &row, &id, &record)?;
            let dto = public_dto(&updated, &record)?;
            persist_update(manager, repository, &updated, row.revision, &id)?;
            Ok(dto)
        })
        .map_err(VaultCrudError::from)?
    }

    pub(crate) fn empty_trash(
        &self,
        repository: &VaultRepository<'_>,
    ) -> Result<VaultRangeDeleteResult, VaultCrudError> {
        self.with_manager(|manager| {
            let timestamp = Utc::now().to_rfc3339();
            let mut candidates = Vec::new();
            for (row, record) in super::crud_query::retained_records(manager, repository)? {
                if record.metadata.trashed_at.is_some() {
                    candidates.push(super::backend_batch::tombstone_candidate(
                        manager, row, &timestamp,
                    )?);
                }
            }
            let tombstoned_ids = candidates
                .iter()
                .map(|candidate| {
                    VaultItemId::from_str(&candidate.id)
                        .map_err(|_| VaultCrudError::MalformedPayload)
                })
                .collect::<Result<Vec<_>, _>>()?;
            let remaining = repository
                .tombstone_items(&candidates)
                .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
            manager.set_item_count(remaining);
            Ok(VaultRangeDeleteResult {
                schema_version: VaultSchemaVersion::CURRENT,
                count: u64::try_from(tombstoned_ids.len())
                    .map_err(|_| VaultCrudError::MalformedPayload)?,
                tombstoned_ids,
            })
        })
        .map_err(VaultCrudError::from)?
    }

    pub(crate) fn get_notes(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
    ) -> Result<Zeroizing<String>, VaultCrudError> {
        self.with_manager(|manager| {
            let (_, record) = active_item(manager, repository, &id)?;
            // Trashed items keep their secrets sealed until restored.
            if record.metadata.trashed_at.is_some() {
                return Err(VaultCrudError::ItemNotFound);
            }
            match &record.secret {
                RecordSecret::Login { .. } => {
                    let mut notes = Zeroizing::new(String::new());
                    if let Some(details) = &record.form_details {
                        for note in &details.notes {
                            if !notes.is_empty() {
                                notes.push('\n');
                            }
                            notes.push_str(&note.value);
                        }
                    }
                    Ok(notes)
                }
                RecordSecret::SecureItem { notes, .. } => {
                    Ok(Zeroizing::new(notes.clone().unwrap_or_default()))
                }
                _ => Err(VaultCrudError::ItemKindMismatch),
            }
        })
        .map_err(VaultCrudError::from)?
    }

    /// Returns the stored (unmasked) username of an active login item. The
    /// public DTO only carries a masked hint, so detail views and edit forms
    /// read the real value through this sealed-read path.
    pub(crate) fn get_username(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
    ) -> Result<Zeroizing<String>, VaultCrudError> {
        self.with_manager(|manager| {
            let (_, record) = active_item(manager, repository, &id)?;
            if record.metadata.trashed_at.is_some() {
                return Err(VaultCrudError::ItemNotFound);
            }
            match &record.secret {
                RecordSecret::Login { .. } => {
                    Ok(Zeroizing::new(record.username.clone().unwrap_or_default()))
                }
                RecordSecret::SecureItem { .. } => Ok(Zeroizing::new(String::new())),
                _ => Err(VaultCrudError::ItemKindMismatch),
            }
        })
        .map_err(VaultCrudError::from)?
    }

    pub(crate) fn set_login_totp(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
        revision: VaultRevision,
        secret: &str,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        self.mutate_record(repository, id, revision, |record| {
            if record.metadata.item_kind != VaultItemKind::Login {
                return Err(VaultCrudError::ItemKindMismatch);
            }
            let (seed, metadata) = parse_totp(secret)?;
            record.totp_seed = Some(seed.to_vec());
            record.metadata.totp = Some(metadata);
            Ok(())
        })
    }

    pub(crate) fn totp_code(
        &self,
        repository: &VaultRepository<'_>,
        id: VaultItemId,
        timestamp: u64,
    ) -> Result<VaultTotpCode, VaultCrudError> {
        self.with_manager(|manager| {
            let (_, record) = active_item(manager, repository, &id)?;
            if record.metadata.trashed_at.is_some() {
                return Err(VaultCrudError::ItemNotFound);
            }
            let seed = match &record.secret {
                RecordSecret::Login { .. } => record
                    .totp_seed
                    .as_deref()
                    .ok_or(VaultCrudError::FieldMismatch)?,
                RecordSecret::Totp { seed } => seed.as_slice(),
                _ => return Err(VaultCrudError::ItemKindMismatch),
            };
            let metadata = record
                .metadata
                .totp
                .as_ref()
                .ok_or(VaultCrudError::MalformedPayload)?;
            let period = u64::from(metadata.period_seconds.value());
            if period == 0 {
                return Err(VaultCrudError::MalformedPayload);
            }
            Ok(VaultTotpCode {
                code: generate_totp(
                    seed,
                    timestamp,
                    metadata.algorithm,
                    period,
                    u32::from(u8::from(metadata.digits)),
                ),
                seconds_remaining: period - timestamp % period,
                period,
            })
        })
        .map_err(VaultCrudError::from)?
    }

    pub(crate) fn health_report(
        &self,
        repository: &VaultRepository<'_>,
    ) -> Result<VaultHealthReport, VaultCrudError> {
        self.with_manager(|manager| {
            let mut report = VaultHealthReport {
                weak: Vec::new(),
                reused: Vec::new(),
                total_logins: 0,
            };
            let mut passwords: Vec<(Zeroizing<String>, Vec<VaultItemId>)> = Vec::new();
            for (row, record) in super::crud_query::active_records(manager, repository)? {
                let RecordSecret::Login { password } = &record.secret else {
                    continue;
                };
                let id =
                    VaultItemId::from_str(&row.id).map_err(|_| VaultCrudError::MalformedPayload)?;
                report.total_logins += 1;
                if crate::vault_generator::estimate_strength(password).score <= 1 {
                    report.weak.push(id);
                }
                if let Some((_, ids)) = passwords
                    .iter_mut()
                    .find(|(existing, _)| existing.as_str() == password)
                {
                    ids.push(id);
                } else {
                    passwords.push((Zeroizing::new(password.clone()), vec![id]));
                }
            }
            report.reused = passwords
                .into_iter()
                .filter_map(|(_, ids)| (ids.len() > 1).then_some(ids))
                .collect();
            Ok(report)
        })
        .map_err(VaultCrudError::from)?
    }
}

fn parse_totp(secret: &str) -> Result<(Zeroizing<Vec<u8>>, TotpMetadata), VaultCrudError> {
    let mut metadata = TotpMetadata {
        issuer: String::new(),
        account_label: String::new(),
        algorithm: TotpAlgorithm::Sha1,
        digits: TotpDigits::Six,
        period_seconds: TotpPeriodSeconds::new(30),
        created_at: Utc::now(),
        last_used_at: None,
    };
    let mut encoded = Zeroizing::new(secret.trim().to_string());
    if secret.trim().starts_with("otpauth://") {
        let uri =
            reqwest::Url::parse(secret.trim()).map_err(|_| VaultCrudError::MalformedPayload)?;
        if uri.host_str() != Some("totp") {
            return Err(VaultCrudError::MalformedPayload);
        }
        metadata.account_label = uri.path().trim_start_matches('/').to_string();
        encoded.clear();
        let mut seen = std::collections::HashSet::new();
        for (key, value) in uri.query_pairs() {
            if !seen.insert(key.to_string()) {
                return Err(VaultCrudError::MalformedPayload);
            }
            match key.as_ref() {
                "secret" => encoded.push_str(&value),
                "issuer" => metadata.issuer = value.into_owned(),
                "algorithm" => {
                    metadata.algorithm = match value.as_ref() {
                        "SHA1" => TotpAlgorithm::Sha1,
                        "SHA256" => TotpAlgorithm::Sha256,
                        "SHA512" => TotpAlgorithm::Sha512,
                        _ => return Err(VaultCrudError::MalformedPayload),
                    }
                }
                "digits" => {
                    metadata.digits = match value.as_ref() {
                        "6" => TotpDigits::Six,
                        "8" => TotpDigits::Eight,
                        _ => return Err(VaultCrudError::MalformedPayload),
                    }
                }
                "period" => {
                    let period = value
                        .parse::<u32>()
                        .map_err(|_| VaultCrudError::MalformedPayload)?;
                    if period == 0 {
                        return Err(VaultCrudError::MalformedPayload);
                    }
                    metadata.period_seconds = TotpPeriodSeconds::new(period);
                }
                _ => {}
            }
        }
    }
    let mut seed = Zeroizing::new(Vec::new());
    let mut accumulator = 0u32;
    let mut bits = 0u32;
    let raw = encoded.trim_end_matches('=');
    for byte in raw.bytes() {
        let value = match byte.to_ascii_uppercase() {
            b'A'..=b'Z' => byte.to_ascii_uppercase() - b'A',
            b'2'..=b'7' => byte - b'2' + 26,
            _ => return Err(VaultCrudError::MalformedPayload),
        };
        accumulator = (accumulator << 5) | u32::from(value);
        bits += 5;
        if bits >= 8 {
            bits -= 8;
            seed.push(
                u8::try_from((accumulator >> bits) & 255)
                    .map_err(|_| VaultCrudError::MalformedPayload)?,
            );
        }
        accumulator &= (1 << bits) - 1;
    }
    if seed.is_empty() || bits >= 5 || accumulator != 0 {
        return Err(VaultCrudError::MalformedPayload);
    }
    Ok((seed, metadata))
}
