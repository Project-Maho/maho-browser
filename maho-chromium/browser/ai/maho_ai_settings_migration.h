// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AI_SETTINGS_MIGRATION_H_
#define MAHO_BROWSER_AI_MAHO_AI_SETTINGS_MIGRATION_H_

class Profile;
class PrefService;

namespace os_crypt_async {
class Encryptor;
}

namespace maho {

void MigrateAiSettingsFromMacOSDefaults(Profile* profile);
void MigrateLegacyGoogleProvider(PrefService* prefs);
void MigrateModelsSettingsV1(Profile* profile);

// Central entry point called from MahoBrowserMainExtraParts::PostProfileInit
void MigrateAiSettings(Profile* profile);

// Synchronous helper for testing Models settings v1 migration
bool MigrateModelsSettingsV1ForTesting(
    PrefService* prefs,
    const os_crypt_async::Encryptor* encryptor = nullptr);

}  // namespace maho

#endif  // MAHO_BROWSER_AI_MAHO_AI_SETTINGS_MIGRATION_H_
