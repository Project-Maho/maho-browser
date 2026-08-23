// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_artifact_registry.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "base/base64url.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/rand_util.h"
#include "base/task/thread_pool.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "net/base/filename_util.h"

namespace maho::ai {
namespace {

constexpr base::FilePath::CharType kArtifactDirectory[] =
    FILE_PATH_LITERAL("AIArtifacts");
constexpr base::FilePath::CharType kRegistryFilename[] =
    FILE_PATH_LITERAL("registry.json");
constexpr char kPreviewPurpose[] = "preview";
constexpr char kExportPurpose[] = "export";

class MahoArtifactRegistryFactory : public ProfileKeyedServiceFactory {
public:
  static MahoArtifactRegistryFactory *GetInstance() {
    static base::NoDestructor<MahoArtifactRegistryFactory> instance;
    return instance.get();
  }

  static MahoArtifactRegistry *GetForProfile(Profile *profile) {
    if (!profile || profile->IsOffTheRecord()) {
      return nullptr;
    }
    return static_cast<MahoArtifactRegistry *>(
        GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
  }

private:
  friend base::NoDestructor<MahoArtifactRegistryFactory>;

  MahoArtifactRegistryFactory()
      : ProfileKeyedServiceFactory(
            "MahoArtifactRegistry",
            ProfileSelections::BuildForRegularProfile()) {}
  ~MahoArtifactRegistryFactory() override = default;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext *context) const override {
    Profile *profile = Profile::FromBrowserContext(context);
    if (!profile || profile->IsOffTheRecord()) {
      return nullptr;
    }
    return std::make_unique<MahoArtifactRegistry>(profile);
  }
};

std::optional<MahoArtifact> ArtifactFromValue(const base::Value &value) {
  if (!value.is_dict()) {
    return std::nullopt;
  }
  const base::DictValue &dict = value.GetDict();
  const std::string *artifact_id = dict.FindString("artifact_id");
  const std::string *session_id = dict.FindString("session_id");
  const std::string *display_name = dict.FindString("display_name");
  const std::string *mime_type = dict.FindString("mime_type");
  const std::string *size_bytes = dict.FindString("size_bytes");
  const std::string *storage_rel_path = dict.FindString("storage_rel_path");
  const std::string *created_at_ms = dict.FindString("created_at_ms");
  if (!artifact_id || artifact_id->empty() || !session_id || !display_name ||
      !mime_type || !size_bytes || !storage_rel_path || !created_at_ms) {
    return std::nullopt;
  }

  MahoArtifact artifact;
  artifact.artifact_id = *artifact_id;
  artifact.session_id = *session_id;
  artifact.display_name = *display_name;
  artifact.mime_type = *mime_type;
  artifact.storage_rel_path = *storage_rel_path;
  if (!base::StringToUint64(*size_bytes, &artifact.size_bytes) ||
      !base::StringToInt64(*created_at_ms, &artifact.created_at_ms)) {
    return std::nullopt;
  }

  const std::string *kind_str = dict.FindString("kind");
  if (kind_str) {
    artifact.kind = MahoArtifactKindFromString(*kind_str);
  } else {
    artifact.kind = DeduceMahoArtifactKind(artifact.mime_type,
                                           artifact.display_name,
                                           artifact.storage_rel_path);
  }
  return artifact;
}

base::DictValue ArtifactToValue(const MahoArtifact &artifact) {
  base::DictValue value;
  value.Set("artifact_id", artifact.artifact_id);
  value.Set("session_id", artifact.session_id);
  value.Set("display_name", artifact.display_name);
  value.Set("mime_type", artifact.mime_type);
  value.Set("size_bytes", base::NumberToString(artifact.size_bytes));
  value.Set("storage_rel_path", artifact.storage_rel_path);
  value.Set("created_at_ms", base::NumberToString(artifact.created_at_ms));
  value.Set("kind", MahoArtifactKindToString(artifact.kind));
  return value;
}

std::string GenerateOpaqueToken(size_t byte_count) {
  std::vector<uint8_t> bytes = base::RandBytesAsVector(byte_count);
  std::string token;
  base::Base64UrlEncode(bytes, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &token);
  return token;
}

std::string PreserveExtension(const std::string &original_name,
                              const std::string &requested_name) {
  base::FilePath original = base::FilePath::FromUTF8Unsafe(original_name);
  base::FilePath requested = base::FilePath::FromUTF8Unsafe(requested_name);
  if (!requested.Extension().empty() || original.Extension().empty()) {
    return requested_name;
  }
  return requested.AddExtension(original.Extension()).AsUTF8Unsafe();
}

}  // namespace

const char* MahoArtifactKindToString(MahoArtifactKind kind) {
  switch (kind) {
    case MahoArtifactKind::kHtml:
      return "html";
    case MahoArtifactKind::kPdf:
      return "pdf";
    case MahoArtifactKind::kXlsx:
      return "xlsx";
    case MahoArtifactKind::kGeneric:
    default:
      return "generic";
  }
}

MahoArtifactKind MahoArtifactKindFromString(std::string_view str) {
  if (str == "html") return MahoArtifactKind::kHtml;
  if (str == "pdf") return MahoArtifactKind::kPdf;
  if (str == "xlsx") return MahoArtifactKind::kXlsx;
  return MahoArtifactKind::kGeneric;
}

namespace {

bool EndsWithCaseInsensitive(std::string_view str, std::string_view suffix) {
  if (str.size() < suffix.size()) return false;
  size_t offset = str.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    char a = str[offset + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a += ('a' - 'A');
    if (b >= 'A' && b <= 'Z') b += ('a' - 'A');
    if (a != b) return false;
  }
  return true;
}

}  // namespace

MahoArtifactKind DeduceMahoArtifactKind(std::string_view mime_type,
                                       std::string_view display_name,
                                       std::string_view storage_rel_path) {
  if (mime_type == "application/pdf" ||
      EndsWithCaseInsensitive(display_name, ".pdf") ||
      EndsWithCaseInsensitive(storage_rel_path, ".pdf")) {
    return MahoArtifactKind::kPdf;
  }
  if (mime_type == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" ||
      mime_type == "application/vnd.ms-excel" ||
      mime_type == "text/csv" ||
      EndsWithCaseInsensitive(display_name, ".xlsx") ||
      EndsWithCaseInsensitive(display_name, ".xls") ||
      EndsWithCaseInsensitive(display_name, ".csv") ||
      EndsWithCaseInsensitive(storage_rel_path, ".xlsx") ||
      EndsWithCaseInsensitive(storage_rel_path, ".xls") ||
      EndsWithCaseInsensitive(storage_rel_path, ".csv")) {
    return MahoArtifactKind::kXlsx;
  }
  if (mime_type == "text/html" ||
      mime_type == "application/xhtml+xml" ||
      EndsWithCaseInsensitive(display_name, ".html") ||
      EndsWithCaseInsensitive(display_name, ".htm") ||
      EndsWithCaseInsensitive(storage_rel_path, ".html") ||
      EndsWithCaseInsensitive(storage_rel_path, ".htm")) {
    return MahoArtifactKind::kHtml;
  }
  return MahoArtifactKind::kGeneric;
}

MahoArtifact::MahoArtifact() = default;
MahoArtifact::MahoArtifact(const MahoArtifact&) = default;
MahoArtifact& MahoArtifact::operator=(const MahoArtifact&) = default;
MahoArtifact::MahoArtifact(MahoArtifact&&) = default;
MahoArtifact& MahoArtifact::operator=(MahoArtifact&&) = default;
MahoArtifact::~MahoArtifact() = default;

MahoArtifactRegistry::Capability::Capability() = default;
MahoArtifactRegistry::Capability::Capability(const Capability&) = default;
MahoArtifactRegistry::Capability& MahoArtifactRegistry::Capability::operator=(
    const Capability&) = default;
MahoArtifactRegistry::Capability::Capability(Capability&&) = default;
MahoArtifactRegistry::Capability& MahoArtifactRegistry::Capability::operator=(
    Capability&&) = default;
MahoArtifactRegistry::Capability::~Capability() = default;

void EnsureMahoArtifactRegistryFactoryBuilt() {
  MahoArtifactRegistryFactory::GetInstance();
}

// static
MahoArtifactRegistry *MahoArtifactRegistry::GetForProfile(Profile *profile) {
  return MahoArtifactRegistryFactory::GetForProfile(profile);
}

MahoArtifactRegistry::MahoArtifactRegistry(Profile *profile,
                                           uint64_t storage_limit_bytes,
                                           const base::TickClock *tick_clock)
    : profile_(profile),
      artifact_root_(profile ? profile->GetPath().Append(kArtifactDirectory)
                             : base::FilePath()),
      registry_path_(artifact_root_.Append(kRegistryFilename)),
      storage_limit_bytes_(storage_limit_bytes), tick_clock_(tick_clock),
      io_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  // Load the persisted metadata index off the UI thread. Registrations that
  // race ahead of this reply are preserved: OnInitialLoad uses try_emplace.
  if (IsAllowed()) {
    io_runner_->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&MahoArtifactRegistry::LoadArtifactsFromDisk,
                       registry_path_),
        base::BindOnce(&MahoArtifactRegistry::OnInitialLoad,
                       weak_factory_.GetWeakPtr()));
  }
}

MahoArtifactRegistry::~MahoArtifactRegistry() = default;

base::expected<ArtifactId, std::string> MahoArtifactRegistry::RegisterArtifact(
    std::string session_id, std::string display_name, std::string mime_type,
    uint64_t size_bytes, std::string storage_rel_path, int64_t created_at_ms,
    std::optional<MahoArtifactKind> kind) {
  if (!IsAllowed()) {
    return base::unexpected(
        "Artifact registry is unavailable in private profiles");
  }
  if (session_id.empty()) {
    return base::unexpected("Session ID is required");
  }
  if (!net::IsSafePortablePathComponent(
          base::FilePath::FromUTF8Unsafe(display_name))) {
    return base::unexpected("Display name is unsafe");
  }
  if (!IsValidStoragePath(storage_rel_path)) {
    return base::unexpected("Storage path must be a safe relative path");
  }
  // Containment is already guaranteed lexically by IsValidStoragePath above
  // (rejects absolute paths, parent refs, and unsafe components). Do NOT call
  // ResolveContainedStoragePath here: RegisterArtifact runs on the UI thread
  // (adapter OnArtifactCreatedOnSequence) where blocking is disallowed, and that
  // helper calls base::NormalizeFilePath (blocking realpath) -> FATAL DCHECK.
  // Symlink-escape canonicalization stays on the read/serve path, which reads on
  // a MayBlock ThreadPool sequence.
  const base::FilePath register_candidate = artifact_root_.Append(
      base::FilePath::FromUTF8Unsafe(storage_rel_path));
  if (register_candidate != artifact_root_ &&
      !artifact_root_.IsParent(register_candidate)) {
    return base::unexpected("Storage path escapes the artifact root");
  }
  if (!EnsureLoaded()) {
    return base::unexpected("Artifact registry could not be loaded");
  }

  // Idempotent by (session_id, storage_rel_path): the same file written again or
  // an ArtifactCreated event re-delivered (retry/replay) updates the existing
  // entry in place and reuses its opaque id. This matches the Android JNI
  // queue's dedup so one fs_write never yields duplicate registry entries, UI
  // cards, or double storage accounting.
  for (auto& [existing_id, existing] : artifacts_) {
    if (existing.session_id == session_id &&
        existing.storage_rel_path == storage_rel_path) {
      existing.display_name = std::move(display_name);
      existing.mime_type = std::move(mime_type);
      existing.size_bytes = size_bytes;
      existing.created_at_ms = created_at_ms;
      existing.kind = kind.value_or(DeduceMahoArtifactKind(
          existing.mime_type, existing.display_name, existing.storage_rel_path));
      ScheduleSave();
      return base::ok(existing_id);
    }
  }

  ArtifactId artifact_id;
  do {
    artifact_id = GenerateOpaqueToken(16);
  } while (artifacts_.contains(artifact_id));

  MahoArtifact artifact;
  artifact.artifact_id = artifact_id;
  artifact.session_id = std::move(session_id);
  artifact.display_name = std::move(display_name);
  artifact.mime_type = std::move(mime_type);
  artifact.size_bytes = size_bytes;
  artifact.storage_rel_path = std::move(storage_rel_path);
  artifact.created_at_ms = created_at_ms;
  artifact.kind = kind.value_or(DeduceMahoArtifactKind(
      artifact.mime_type, artifact.display_name, artifact.storage_rel_path));
  artifacts_.emplace(artifact_id, std::move(artifact));
  PruneToStorageLimit();
  if (!artifacts_.contains(artifact_id)) {
    ScheduleSave();
    return base::unexpected("Artifact exceeds the profile storage limit");
  }
  ScheduleSave();
  return base::ok(std::move(artifact_id));
}

std::vector<MahoArtifact>
MahoArtifactRegistry::ListArtifacts(std::string_view session_id) {
  std::vector<MahoArtifact> result;
  if (!IsAllowed() || !EnsureLoaded()) {
    return result;
  }
  for (const auto &[id, artifact] : artifacts_) {
    if (session_id.empty() || artifact.session_id == session_id) {
      result.push_back(artifact);
    }
  }
  std::ranges::sort(result, {}, &MahoArtifact::created_at_ms);
  return result;
}

std::optional<MahoArtifact>
MahoArtifactRegistry::GetArtifact(std::string_view artifact_id) {
  if (!IsAllowed() || !EnsureLoaded()) {
    return std::nullopt;
  }
  auto it = artifacts_.find(artifact_id);
  return it == artifacts_.end() ? std::nullopt
                                : std::optional<MahoArtifact>(it->second);
}

base::expected<MahoArtifact, std::string>
MahoArtifactRegistry::RenameArtifact(std::string_view artifact_id,
                                     std::string display_name) {
  if (!IsAllowed()) {
    return base::unexpected(
        "Artifact registry is unavailable in private profiles");
  }
  if (!EnsureLoaded()) {
    return base::unexpected("Artifact registry could not be loaded");
  }
  auto it = artifacts_.find(artifact_id);
  if (it == artifacts_.end()) {
    return base::unexpected("Artifact not found");
  }

  display_name = PreserveExtension(it->second.display_name, display_name);
  if (!net::IsSafePortablePathComponent(
          base::FilePath::FromUTF8Unsafe(display_name))) {
    return base::unexpected("Display name is unsafe");
  }
  it->second.display_name = std::move(display_name);
  ScheduleSave();
  return it->second;
}

bool MahoArtifactRegistry::DeleteArtifact(std::string_view artifact_id) {
  if (!IsAllowed() || !EnsureLoaded()) {
    return false;
  }
  return EraseArtifact(std::string(artifact_id), /*save=*/true);
}

std::optional<base::FilePath>
MahoArtifactRegistry::ResolvePath(std::string_view artifact_id) {
  std::optional<MahoArtifact> artifact = GetArtifact(artifact_id);
  if (!artifact) {
    return std::nullopt;
  }
  return ResolveContainedStoragePath(artifact_root_, artifact->storage_rel_path);
}

void MahoArtifactRegistry::PrepareArtifactRoot() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (artifact_root_preparing_ || artifact_root_result_.has_value()) {
    return;
  }
  if (!IsAllowed()) {
    artifact_root_result_ = base::unexpected(
        "Artifact registry is unavailable in private profiles");
    return;
  }
  artifact_root_preparing_ = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&MahoArtifactRegistry::CreateArtifactRoot,
                     artifact_root_),
      base::BindOnce(&MahoArtifactRegistry::OnArtifactRootPrepared,
                     weak_factory_.GetWeakPtr()));
}

base::expected<base::FilePath, std::string>
MahoArtifactRegistry::ArtifactRootForTurn() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsAllowed()) {
    return base::unexpected(
        "Artifact registry is unavailable in private profiles");
  }
  if (!artifact_root_result_.has_value()) {
    return base::unexpected("Artifact root is not ready");
  }
  return *artifact_root_result_;
}

std::optional<std::string> MahoArtifactRegistry::IssueCapability(
    std::string_view artifact_id, std::string_view purpose,
    content::WebContents *issuing_web_contents) {
  if (!IsAllowed() || !issuing_web_contents || !IsValidPurpose(purpose) ||
      issuing_web_contents->GetBrowserContext() != profile_ ||
      !GetArtifact(artifact_id)) {
    return std::nullopt;
  }

  std::string token;
  do {
    token = GenerateOpaqueToken(16);
  } while (capabilities_.contains(token));
  Capability capability;
  capability.artifact_id = artifact_id;
  capability.purpose = purpose;
  capability.issuing_web_contents = issuing_web_contents->GetWeakPtr();
  capability.expires_at = tick_clock_->NowTicks() + kCapabilityTtl;
  capabilities_.emplace(token, std::move(capability));
  return token;
}

std::optional<MahoArtifact>
MahoArtifactRegistry::VerifyCapability(std::string_view token,
                                       std::string_view purpose,
                                       content::WebContents *web_contents) {
  if (!web_contents || web_contents->GetBrowserContext() != profile_) {
    return std::nullopt;
  }
  auto artifact = GetCapabilityArtifactForResponseMetadata(token, purpose);
  if (!artifact) {
    return std::nullopt;
  }
  auto it = capabilities_.find(token);
  if (it == capabilities_.end() ||
      it->second.issuing_web_contents.get() != web_contents) {
    return std::nullopt;
  }
  return artifact;
}

std::optional<MahoArtifact>
MahoArtifactRegistry::GetCapabilityArtifactForResponseMetadata(
    std::string_view token, std::string_view purpose) {
  if (!IsAllowed() || !IsValidPurpose(purpose)) {
    return std::nullopt;
  }
  auto it = capabilities_.find(token);
  if (it == capabilities_.end()) {
    return std::nullopt;
  }
  if (tick_clock_->NowTicks() >= it->second.expires_at ||
      !it->second.issuing_web_contents) {
    capabilities_.erase(it);
    return std::nullopt;
  }
  if (it->second.purpose != purpose) {
    return std::nullopt;
  }
  return GetArtifact(it->second.artifact_id);
}

bool MahoArtifactRegistry::ExpireCapabilityForTesting(std::string_view token) {
  auto it = capabilities_.find(token);
  if (it == capabilities_.end()) {
    return false;
  }
  it->second.expires_at = tick_clock_->NowTicks();
  return true;
}

void MahoArtifactRegistry::RevokeArtifactCapabilities(
    std::string_view artifact_id) {
  std::erase_if(capabilities_, [&](const auto &item) {
    return item.second.artifact_id == artifact_id;
  });
}

bool MahoArtifactRegistry::IsAllowed() const {
  return profile_ && !profile_->IsOffTheRecord();
}

bool MahoArtifactRegistry::EnsureLoaded() {
  // The metadata index is populated asynchronously (constructor +
  // OnInitialLoad). Callers operate on the in-memory map and never block on
  // disk I/O from the UI thread.
  loaded_ = true;
  return true;
}

// static
std::vector<MahoArtifact> MahoArtifactRegistry::LoadArtifactsFromDisk(
    base::FilePath registry_path) {
  std::vector<MahoArtifact> result;
  if (!base::PathExists(registry_path)) {
    return result;
  }
  std::string json;
  if (!base::ReadFileToString(registry_path, &json)) {
    return result;
  }
  std::optional<base::Value> root =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!root || !root->is_dict()) {
    return result;
  }
  const base::ListValue *entries = root->GetDict().FindList("artifacts");
  if (!entries) {
    return result;
  }
  for (const base::Value &value : *entries) {
    std::optional<MahoArtifact> artifact = ArtifactFromValue(value);
    if (!artifact || !IsValidStoragePath(artifact->storage_rel_path) ||
        !net::IsSafePortablePathComponent(
            base::FilePath::FromUTF8Unsafe(artifact->display_name))) {
      continue;
    }
    result.push_back(std::move(*artifact));
  }
  return result;
}

void MahoArtifactRegistry::OnInitialLoad(
    std::vector<MahoArtifact> loaded_artifacts) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  loaded_ = true;
  for (MahoArtifact &artifact : loaded_artifacts) {
    // try_emplace never clobbers an entry registered before this reply landed.
    artifacts_.try_emplace(artifact.artifact_id, std::move(artifact));
  }
}

void MahoArtifactRegistry::ScheduleSave() const {
  if (!IsAllowed()) {
    return;
  }
  base::ListValue entries;
  for (const auto &[id, artifact] : artifacts_) {
    entries.Append(ArtifactToValue(artifact));
  }
  base::DictValue root;
  root.Set("artifacts", std::move(entries));
  std::string json;
  if (!base::JSONWriter::WriteWithOptions(
          base::Value(std::move(root)), base::JSONWriter::OPTIONS_PRETTY_PRINT,
          &json)) {
    return;
  }
  io_runner_->PostTask(
      FROM_HERE, base::BindOnce(&MahoArtifactRegistry::WriteArtifactsToDisk,
                                artifact_root_, registry_path_,
                                std::move(json)));
}

// static
void MahoArtifactRegistry::WriteArtifactsToDisk(base::FilePath artifact_root,
                                                base::FilePath registry_path,
                                                std::string json) {
  if (!base::CreateDirectory(artifact_root)) {
    return;
  }
  base::ImportantFileWriter::WriteFileAtomically(registry_path, json);
}

// static
void MahoArtifactRegistry::CanonicalizeAndDeleteFile(
    base::FilePath artifact_root, std::string storage_rel_path) {
  std::optional<base::FilePath> path =
      ResolveContainedStoragePath(artifact_root, storage_rel_path);
  if (path) {
    base::DeleteFile(*path);
  }
}

void MahoArtifactRegistry::PruneToStorageLimit() {
  uint64_t total = 0;
  for (const auto &[id, artifact] : artifacts_) {
    total = artifact.size_bytes > std::numeric_limits<uint64_t>::max() - total
                ? std::numeric_limits<uint64_t>::max()
                : total + artifact.size_bytes;
  }
  while (total > storage_limit_bytes_ && !artifacts_.empty()) {
    auto oldest = std::ranges::min_element(
        artifacts_, [](const auto &left, const auto &right) {
          if (left.second.created_at_ms != right.second.created_at_ms) {
            return left.second.created_at_ms < right.second.created_at_ms;
          }
          return left.first < right.first;
        });
    const ArtifactId id = oldest->first;
    const uint64_t size = oldest->second.size_bytes;
    EraseArtifact(id, /*save=*/false);
    total = size > total ? 0 : total - size;
  }
}

bool MahoArtifactRegistry::EraseArtifact(const ArtifactId &artifact_id,
                                         bool save) {
  auto it = artifacts_.find(artifact_id);
  if (it == artifacts_.end()) {
    return false;
  }
  const std::string storage_rel_path = it->second.storage_rel_path;
  artifacts_.erase(it);
  RevokeArtifactCapabilities(artifact_id);
  io_runner_->PostTask(
      FROM_HERE, base::BindOnce(&MahoArtifactRegistry::CanonicalizeAndDeleteFile,
                                artifact_root_, storage_rel_path));
  if (save) {
    ScheduleSave();
  }
  return true;
}

// static
base::expected<base::FilePath, std::string>
MahoArtifactRegistry::CreateArtifactRoot(base::FilePath artifact_root) {
  if (!base::CreateDirectory(artifact_root)) {
    return base::unexpected("Artifact root could not be created");
  }
  base::FilePath canonical_root;
  if (!base::NormalizeFilePath(artifact_root, &canonical_root)) {
    return base::unexpected("Artifact root could not be canonicalized");
  }
  return canonical_root;
}

void MahoArtifactRegistry::OnArtifactRootPrepared(
    base::expected<base::FilePath, std::string> result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  artifact_root_preparing_ = false;
  artifact_root_result_ = std::move(result);
}

// static
std::optional<base::FilePath>
MahoArtifactRegistry::ResolveContainedStoragePath(
    const base::FilePath &artifact_root, std::string_view storage_rel_path) {
  if (!IsValidStoragePath(storage_rel_path)) {
    return std::nullopt;
  }
  base::FilePath canonical_root;
  if (!base::NormalizeFilePath(artifact_root, &canonical_root)) {
    return std::nullopt;
  }
  base::FilePath candidate = artifact_root.Append(
      base::FilePath::FromUTF8Unsafe(std::string(storage_rel_path)));
  base::FilePath canonical_candidate;
  if (!base::NormalizeFilePath(candidate, &canonical_candidate)) {
    return std::nullopt;
  }
  if (canonical_candidate != canonical_root &&
      !canonical_root.IsParent(canonical_candidate)) {
    return std::nullopt;
  }
  return canonical_candidate;
}

// static
bool MahoArtifactRegistry::IsValidStoragePath(
    std::string_view storage_rel_path) {
  if (storage_rel_path.empty()) {
    return false;
  }
  base::FilePath path = base::FilePath::FromUTF8Unsafe(
      std::string(storage_rel_path));
  if (path.IsAbsolute() || path.ReferencesParent() || path.EndsWithSeparator()) {
    return false;
  }
  const std::vector<base::FilePath::StringType> components =
      path.GetComponents();
  if (components.empty()) {
    return false;
  }
  for (const auto &component : components) {
    if (!net::IsSafePortablePathComponent(base::FilePath(component))) {
      return false;
    }
  }
  return true;
}

// static
bool MahoArtifactRegistry::IsValidPurpose(std::string_view purpose) {
  return purpose == kPreviewPurpose || purpose == kExportPurpose;
}

} // namespace maho::ai
