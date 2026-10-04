# Security Policy

## Reporting a vulnerability

**Do not open a public issue for a security problem.**

Report privately to: `admin@mahobrowser.com`

Please include:

- A description of the issue and its impact
- Steps to reproduce, or a proof of concept
- The affected version, platform, and build (official release vs. self-built)
- Whether the issue is already public anywhere

If you want an encrypted reply, say so in your first message and we will exchange keys.

## Scope

**In scope**

- The client source in this repository (browser overlay, Rust crates, WebUI, native shells)
- Build and release scripts that could compromise a distributed build
- Vulnerabilities reachable by web content, by a downloaded file, or by a local
  unprivileged process

**Out of scope**

- The hosted relay / account service, which is not part of this repository and
  has its own reporting path
- Third-party dependencies with their own security process (report upstream;
  tell us so we can track it)
- Issues that require an already-compromised machine, an unlocked profile with
  developer flags enabled, or physical access
- Support requests, crashes without a security impact, and missing hardening
  features that are documented as intentional

## What to expect

- We aim to acknowledge a report within a few business days.
- We will confirm the issue, keep you updated, and tell you when a fix ships.
- This project is maintained on a best-effort basis. There is **no bug bounty**
  and **no guaranteed response or fix deadline**. Please do not disclose
  publicly until a fix is available or we agree on a disclosure date.

## Credit

We credit reporters in release notes when a fix ships, unless you ask us not to.

## Supported versions

Security fixes target the latest release of the current major line. Older
releases and third-party builds are not supported. Because Chromium patches land
continuously upstream, staying current matters: a build that falls behind
upstream security fixes should be treated as unpatched.

## Handling of your report

- Reports are read by a small number of maintainers.
- We do not share your report outside the maintainer group without your consent,
  except as required to ship a fix with an upstream project.
- Please do not include personal data of third parties in a report.
