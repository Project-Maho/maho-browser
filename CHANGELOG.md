# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **[SECURITY]** CC7.1 Audit Logging: Added `target = "maho_agent::audit"` logging for session start, session end (with duration), tool allows, and BYOK key access events.
- **[SECURITY]** SOC2 compliance tests: Added integration tests validating fail-closed behavior on missing keys, permission callback gating, and secure storage callback invocation.
- **[SECURITY]** Added Dependabot configuration for `maho/`, `maho/relay`, `mail/`, `website/` and chromium resource subdirectories.
- **[SECURITY]** Created `SECURITY.md` vulnerability disclosure policy and SLA.
- **[SECURITY]** Drafted data classification, data retention/deletion policies, and agent threat model documentation.

### Security Review Sign-off
- **Review Date:** Pending
- **Security Lead Sign-off:** Pending
