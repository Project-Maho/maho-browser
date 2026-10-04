// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_ARTIFACT_REGISTRY_H_
#define MAHO_BROWSER_AI_MAHO_ARTIFACT_REGISTRY_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/default_tick_clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "components/keyed_service/core/keyed_service.h"

class Profile;

namespace content {
class WebContents;
}

namespace maho::ai {

// Registers the profile-keyed service factory. This must run during browser
// startup before Chromium seals the BrowserContext keyed-service topology.
void EnsureMahoArtifactRegistryFactoryBuilt();

using ArtifactId = std::string;

enum class MahoArtifactKind {
  kGeneric = 0,
  kHtml = 1,
  kPdf = 2,
  kXlsx = 3,
};

const char* MahoArtifactKindToString(MahoArtifactKind kind);
MahoArtifactKind MahoArtifactKindFromString(std::string_view str);
MahoArtifactKind DeduceMahoArtifactKind(std::string_view mime_type,
                                       std::string_view display_name,
                                       std::string_view storage_rel_path);

struct MahoArtifact {
  MahoArtifact();
  MahoArtifact(const MahoArtifact&);
  MahoArtifact& operator=(const MahoArtifact&);
  MahoArtifact(MahoArtifact&&);
  MahoArtifact& operator=(MahoArtifact&&);
  ~MahoArtifact();

  ArtifactId artifact_id;
  std::string session_id;
  std::string display_name;
  std::string mime_type;
  uint64_t size_bytes = 0;
  std::string storage_rel_path;
  int64_t created_at_ms = 0;
  MahoArtifactKind kind = MahoArtifactKind::kGeneric;

  bool operator==(const MahoArtifact &) const = default;
};

class MahoArtifactRegistry : public KeyedService {
public:
  static constexpr uint64_t kDefaultStorageLimitBytes = 1ull << 30;
  static constexpr base::TimeDelta kCapabilityTtl = base::Minutes(15);

  static MahoArtifactRegistry *GetForProfile(Profile *profile);

  explicit MahoArtifactRegistry(
      Profile *profile,
      uint64_t storage_limit_bytes = kDefaultStorageLimitBytes,
      const base::TickClock *tick_clock =
          base::DefaultTickClock::GetInstance());
  ~MahoArtifactRegistry() override;

  MahoArtifactRegistry(const MahoArtifactRegistry &) = delete;
  MahoArtifactRegistry &operator=(const MahoArtifactRegistry &) = delete;

  base::expected<ArtifactId, std::string>
  RegisterArtifact(std::string session_id, std::string display_name,
                   std::string mime_type, uint64_t size_bytes,
                   std::string storage_rel_path, int64_t created_at_ms,
                   std::optional<MahoArtifactKind> kind = std::nullopt);
  std::vector<MahoArtifact> ListArtifacts(std::string_view session_id);
  std::optional<MahoArtifact> GetArtifact(std::string_view artifact_id);
  base::expected<MahoArtifact, std::string>
  RenameArtifact(std::string_view artifact_id, std::string display_name);
  bool DeleteArtifact(std::string_view artifact_id);
  // Resolves an artifact id to its canonical on-disk path. Performs BLOCKING
  // realpath resolution (base::NormalizeFilePath) and therefore MUST run on a
  // MayBlock sequence, never the UI thread. Production serve/delete paths do
  // NOT call this; they use the static ResolveContainedStoragePath() inside
  // their own MayBlock task. Retained for test/off-thread callers only.
  std::optional<base::FilePath> ResolvePath(std::string_view artifact_id);

  // Starts fresh-profile root preparation on a MayBlock worker. Turn startup
  // reads ArtifactRootForTurn() and fails closed while pending or after error.
  void PrepareArtifactRoot();
  base::expected<base::FilePath, std::string> ArtifactRootForTurn() const;

  std::optional<std::string>
  IssueCapability(std::string_view artifact_id, std::string_view purpose,
                  content::WebContents *issuing_web_contents);
  std::optional<MahoArtifact>
  VerifyCapability(std::string_view token, std::string_view purpose,
                   content::WebContents *web_contents);
  // URLDataSource response headers are computed before StartDataRequest()
  // receives the requesting WebContents. This metadata-only lookup lets the
  // export source derive a safe Content-Type name parameter from the registry;
  // body delivery still requires full WebContents-bound verification.
  std::optional<MahoArtifact> GetCapabilityArtifactForResponseMetadata(
      std::string_view token, std::string_view purpose);
  void RevokeArtifactCapabilities(std::string_view artifact_id);

  bool ExpireCapabilityForTesting(std::string_view token);

  const base::FilePath &artifact_root() const { return artifact_root_; }

  // Canonicalizes |artifact_root|/|storage_rel_path| and returns it only when
  // it stays lexically and symlink-resolved inside |artifact_root|. Pure and
  // thread-agnostic: callers must invoke it on a MayBlock sequence because it
  // performs realpath resolution (base::NormalizeFilePath).
  static std::optional<base::FilePath> ResolveContainedStoragePath(
      const base::FilePath &artifact_root, std::string_view storage_rel_path);

private:
  struct Capability {
    Capability();
    Capability(const Capability&);
    Capability& operator=(const Capability&);
    Capability(Capability&&);
    Capability& operator=(Capability&&);
    ~Capability();

    ArtifactId artifact_id;
    std::string purpose;
    base::WeakPtr<content::WebContents> issuing_web_contents;
    base::TimeTicks expires_at;
  };

  bool IsAllowed() const;
  bool EnsureLoaded();
  // Reads and validates the persisted index off the UI thread.
  static std::vector<MahoArtifact> LoadArtifactsFromDisk(
      base::FilePath registry_path);
  void OnInitialLoad(std::vector<MahoArtifact> loaded_artifacts);
  // Serializes the in-memory index and writes it off the UI thread
  // (best-effort persistence; the in-memory map is the session-of-record).
  void ScheduleSave() const;
  static void WriteArtifactsToDisk(base::FilePath artifact_root,
                                   base::FilePath registry_path,
                                   std::string json);
  static void CanonicalizeAndDeleteFile(base::FilePath artifact_root,
                                        std::string storage_rel_path);
  void PruneToStorageLimit();
  bool EraseArtifact(const ArtifactId &artifact_id, bool save);
  static base::expected<base::FilePath, std::string> CreateArtifactRoot(
      base::FilePath artifact_root);
  void OnArtifactRootPrepared(
      base::expected<base::FilePath, std::string> result);
  static bool IsValidStoragePath(std::string_view storage_rel_path);
  static bool IsValidPurpose(std::string_view purpose);

  const raw_ptr<Profile> profile_;
  const base::FilePath artifact_root_;
  const base::FilePath registry_path_;
  const uint64_t storage_limit_bytes_;
  const raw_ptr<const base::TickClock> tick_clock_;
  scoped_refptr<base::SequencedTaskRunner> io_runner_;

  SEQUENCE_CHECKER(sequence_checker_);
  bool artifact_root_preparing_ = false;
  std::optional<base::expected<base::FilePath, std::string>>
      artifact_root_result_;
  bool loaded_ = false;
  std::map<ArtifactId, MahoArtifact, std::less<>> artifacts_;
  std::map<std::string, Capability, std::less<>> capabilities_;
  base::WeakPtrFactory<MahoArtifactRegistry> weak_factory_{this};
};

} // namespace maho::ai

#endif // MAHO_BROWSER_AI_MAHO_ARTIFACT_REGISTRY_H_
