> **DRAFT — Legal review recommended before production use.** Generated 2026-07-10. Placeholders resolved 2026-07-11 with global-service / US-data-processing defaults; a licensed attorney should still validate the international-transfer and rights provisions.

# Maho Browser — Privacy Policy

**Last updated:** July 11, 2026

This Privacy Policy explains how **Indo Yoon**, an individual developer ("we", "us", "our"), handles information in connection with **Maho Browser** ("Maho", "the Browser"), a proprietary desktop web browser for macOS, Windows, and Linux.

Our approach is simple: Maho is built to keep your data on your device. We collect as little as possible, and the few things we do collect are either opt-in or strictly necessary to deliver the Browser to you.

---

## 1. Introduction & Scope

This policy covers the Maho Browser desktop application and the services that support it (crash reporting, optional sync, and software updates). It does not cover third-party websites you visit through the Browser, which have their own privacy practices.

By using Maho Browser, you acknowledge the practices described here. Where we rely on your consent (for example, crash reporting), we ask for it explicitly and you can withdraw it at any time.

---

## 2. Information We Collect

We keep collection to a minimum. Here is exactly what may be collected, and under what conditions.

### 2.1 Crash Reports (Opt-In)

Maho uses **Crashpad**, Chromium's crash reporting system. Crash reporting is **off unless you turn it on**. You are asked during first-run setup, and you can toggle it at any time in the Browser's settings.

If you enable crash reporting, when Maho crashes it may send a **minidump** to `crashes.maho.app`. A minidump is a snapshot of the Browser's state at the moment of the crash. It may contain:

- The stack traces and memory of the threads that were running.
- The Maho version, operating system version, and CPU architecture.
- Small fragments of memory near the crash, which could incidentally include portions of data that was being processed at that moment (for example, part of a URL or page content).

Minidumps are used only to diagnose and fix crashes. We do not use them to build profiles of you or track your browsing.

### 2.2 Sync Data (Optional, User-Initiated)

Sync is **off by default**. It only activates if you explicitly enable it and create or sign into a Maho account.

If you enable sync, the following applies:

- The account and sync services run at `relay.mahobrowser.com`.
- Authentication uses **opaque tokens** (not JWTs). Tokens are **SHA-256 hashed at rest**.
- Data you choose to sync (which may include bookmarks, history, open tabs, settings, and similar browser data) is associated with your account so it can be delivered to your other devices.
- We collect the minimal account information needed to operate the service, such as your account identifier and sync payloads.

You control what syncs, and you can turn sync off or delete your account at any time (see Section 7 and Section 8).

### 2.3 Update Check Metadata

Maho checks for updates about every 24 hours against maho.app and its official release channel (GitHub Releases at `Project-Maho/release`).

These checks are ordinary HTTP requests. As with any web request, the update server may see standard information such as your **Maho version**, **operating system platform**, and the **IP address** the request originates from. We use this only to serve the correct update and to understand aggregate platform distribution. We do not use it to identify you personally.

---

## 3. Information We Do NOT Collect

We want to be equally clear about what we do **not** do:

- **No telemetry by default.** Maho does not collect usage analytics or behavioral telemetry in the background.
- **No ad tracking.** We do not track you across sites, build advertising profiles, or embed ad-tracking technology.
- **We do not sell your personal information.** Ever. (See the "Do Not Sell" statement in Section 7.)
- **Browser data import stays local.** On first run, you may import bookmarks, history, passwords, and cookies from other browsers (such as Chrome, Edge, Brave, Firefox, and Safari). This import is processed **entirely on your device**. Imported data is **not transmitted to any Maho server**.

---

## 4. How We Use Information

We use the limited information described above only for these purposes:

- **Crash reports:** to diagnose, reproduce, and fix stability problems in the Browser.
- **Sync data:** to store and deliver your synced browser data across your own devices, and to authenticate your account.
- **Update metadata:** to deliver the correct software update for your platform and version, and to understand aggregate platform distribution.

We do not use any of this information for advertising, profiling, or sale.

---

## 5. Data Storage & Security

### Local Storage

Your everyday browsing data — bookmarks, history, cookies, and saved passwords — is stored **locally on your device**, not on our servers. Typical locations are:

- **macOS:** `~/Library/Application Support/Maho/`
- **Windows:** `%APPDATA%\Maho\`
- **Linux:** `~/.config/maho/`

Passwords are stored using your operating system's protections where available. You are responsible for securing your own device.

### Server-Side Security

For the data that does reach our services (crash reports if enabled, sync data if enabled):

- Data in transit is protected with standard encryption (HTTPS/TLS).
- Sync authentication tokens are **opaque** and **SHA-256 hashed at rest**.
- We limit access to this data to what is necessary to operate the services.

No system is perfectly secure. While we take reasonable measures, we cannot guarantee absolute security.

---

## 6. Data Sharing

We do **not** sell your personal information, and we do not share it for advertising.

We rely on a small number of infrastructure providers ("processors") strictly to operate the Browser's supporting services:

- **Cloudflare, Inc.** for hosting, content delivery, and protection of our web and service endpoints (maho.app, `crashes.maho.app`, `relay.mahobrowser.com`). Data is processed on Cloudflare infrastructure located in the **United States**.
- **GitHub (GitHub Releases)** for distributing the Browser binary and update packages.

These providers process data only as needed to provide their infrastructure services to us. We may also disclose information if required by law, to comply with a valid legal process, or to protect the rights, safety, or property of users or the public.

---

## 7. Your Rights (GDPR & CCPA)

We aim to honor these rights for all users, regardless of where you live.

### For EU/EEA/UK Users (GDPR)

You have the right to:

- **Access** the personal data we hold about you.
- **Rectify** inaccurate data.
- **Erase** your data ("right to be forgotten").
- **Restrict** or **object** to certain processing.
- **Data portability** — receive your data in a structured, machine-readable format.
- **Withdraw consent** at any time (for example, turning off crash reporting), without affecting prior lawful processing.
- **Lodge a complaint** with your local data protection authority.

Our legal bases for processing include your **consent** (crash reporting, sync), and our **legitimate interests** and **contractual necessity** (delivering updates and operating the account service you requested).

### For California Users (CCPA/CPRA)

You have the right to:

- **Know** what personal information we collect and how it is used.
- **Access** and request a copy of that information.
- **Delete** your personal information.
- **Opt out of sale** of personal information.
- **Non-discrimination** for exercising your rights.

### Do Not Sell My Personal Information

**We do not sell your personal information, and we have not sold personal information.** There is nothing to opt out of, because no sale takes place. This statement is provided to satisfy CCPA/CPRA transparency requirements.

### How to Exercise Your Rights

- Most data lives on your device — you can delete it directly by clearing browser data or uninstalling Maho.
- For crash reporting, you can withdraw consent in settings at any time.
- For sync account data, you can delete your account from within the Browser, or contact us.
- To make a formal request, email **privacy@maho.app**. We may need to verify your identity before acting on a request.

---

## 8. Data Retention

We keep data only as long as needed:

- **Crash reports:** retained on a **30-day rolling** basis, then deleted.
- **Account and sync data:** retained until you **delete your account**, after which it is removed within a reasonable period, subject to backups and legal obligations.
- **Update check logs:** retained only briefly for operational and security purposes.
- **Local data on your device:** remains until you delete it or uninstall the Browser. It is under your control, not ours.

---

## 9. Children's Privacy

Maho Browser is **not directed at children under 13**, and we do not knowingly collect personal information from children under 13 (consistent with **COPPA**). In jurisdictions with a higher age of digital consent, that higher age applies to account creation (see the Terms of Service, Section 8).

If you believe a child under the applicable age has provided us personal information through a sync account or crash report, contact **privacy@maho.app** and we will delete it.

---

## 10. International Data Transfers

Maho is operated by an individual based outside the United States, while its supporting services and infrastructure providers process data **in the United States**. If you use Maho from the EU/EEA, the UK, or another region, your data (crash reports if enabled, sync data if enabled) will be transferred to and processed in the United States.

Where we transfer personal data internationally, we rely on appropriate safeguards permitted by applicable law, principally the **European Commission's Standard Contractual Clauses (SCCs)** and the UK International Data Transfer Addendum, as implemented by our infrastructure providers. By using the sync or crash reporting features, you understand that the associated data will be processed in the **United States**.

---

## 11. Changes to This Policy

We may update this Privacy Policy from time to time. When we make material changes, we will update the "Last updated" date and, where practical, provide notice through the Browser or at maho.app.

Your continued use of Maho after changes take effect constitutes acceptance of the updated policy.

---

## 12. Contact

For privacy questions or to exercise your rights, contact:

- **Email:** privacy@maho.app
- **Publisher:** Indo Yoon
- **Website:** maho.app

---

*Maho Browser is proprietary, closed-source software published by Indo Yoon. It incorporates Chromium and other open-source components under their respective licenses. This policy describes only Maho's own data practices.*
