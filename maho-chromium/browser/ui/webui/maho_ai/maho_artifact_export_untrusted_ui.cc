// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_artifact_export_untrusted_ui.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted_memory.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ai/maho_artifact_registry.h"
#include "maho/browser/ui/webui/maho_webui_private_boundary.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace {

constexpr int64_t kMaxExportBytes = 64 * 1024 * 1024;
constexpr char kExportPurpose[] = "export";

std::string ReadArtifactBytes(base::FilePath artifact_root,
                              std::string storage_rel_path) {
  // Canonicalize + containment-check on this MayBlock sequence; an escaped or
  // invalid path yields empty bytes, never the target file.
  std::optional<base::FilePath> path =
      maho::ai::MahoArtifactRegistry::ResolveContainedStoragePath(
          artifact_root, storage_rel_path);
  std::string contents;
  if (path) {
    base::ReadFileToStringWithMaxSize(*path, &contents, kMaxExportBytes);
  }
  return contents;
}

std::string GetCapabilityToken(const GURL& url) {
  std::string token;
  net::GetValueForKeyInQuery(url, "cap", &token);
  return token;
}

std::string QuoteMimeNameParameter(std::string_view display_name) {
  std::string quoted;
  quoted.reserve(display_name.size() + 2);
  quoted.push_back('"');
  for (char character : display_name) {
    if (character == '"' || character == '\\') {
      quoted.push_back('\\');
    }
    quoted.push_back(character);
  }
  quoted.push_back('"');
  return quoted;
}

void RunGotDataWithString(content::URLDataSource::GotDataCallback callback,
                          std::string data) {
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(std::move(data)));
}

class ArtifactExportDataSource : public content::URLDataSource {
 public:
  explicit ArtifactExportDataSource(Profile* profile) : profile_(profile) {}
  ArtifactExportDataSource(const ArtifactExportDataSource&) = delete;
  ArtifactExportDataSource& operator=(const ArtifactExportDataSource&) = delete;
  ~ArtifactExportDataSource() override = default;

  std::string GetSource() override {
    return maho::kMahoArtifactExportUntrustedURL;
  }

  void StartDataRequest(const GURL& url,
                        const content::WebContents::Getter& wc_getter,
                        GotDataCallback callback) override {
    const std::string token = GetCapabilityToken(url);
    content::WebContents* web_contents = wc_getter.Run();
    maho::ai::MahoArtifactRegistry* registry =
        (web_contents && profile_)
            ? maho::ai::MahoArtifactRegistry::GetForProfile(profile_)
            : nullptr;
    std::optional<maho::ai::MahoArtifact> artifact;
    if (registry && web_contents && !token.empty()) {
      artifact =
          registry->VerifyCapability(token, kExportPurpose, web_contents);
    }
    if (!artifact) {
      std::move(callback).Run(nullptr);
      return;
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&ReadArtifactBytes, registry->artifact_root(),
                       artifact->storage_rel_path),
        base::BindOnce(&RunGotDataWithString, std::move(callback)));
  }

  std::string GetMimeType(const GURL& url) override {
    maho::ai::MahoArtifactRegistry* registry =
        profile_ ? maho::ai::MahoArtifactRegistry::GetForProfile(profile_)
                 : nullptr;
    const std::optional<maho::ai::MahoArtifact> artifact =
        registry ? registry->GetCapabilityArtifactForResponseMetadata(
                       GetCapabilityToken(url), kExportPurpose)
                 : std::nullopt;
    if (!artifact) {
      return "application/octet-stream";
    }
    return "application/octet-stream; name=" +
           QuoteMimeNameParameter(artifact->display_name);
  }

  bool ShouldReplaceExistingSource() override { return true; }
  bool AllowCaching() override { return false; }
  bool ShouldServeMimeTypeAsContentTypeHeader() override { return true; }
  bool ShouldDenyXFrameOptions() override { return true; }

  std::string GetContentSecurityPolicy(
      network::mojom::CSPDirectiveName directive) override {
    switch (directive) {
      case network::mojom::CSPDirectiveName::DefaultSrc:
        return "default-src 'none';";
      case network::mojom::CSPDirectiveName::Sandbox:
        return "sandbox;";
      default:
        return content::URLDataSource::GetContentSecurityPolicy(directive);
    }
  }

 private:
  const raw_ptr<Profile> profile_;
};

}  // namespace

MahoArtifactExportUntrustedUIConfig::MahoArtifactExportUntrustedUIConfig()
    : WebUIConfig(content::kChromeUIUntrustedScheme,
                  maho::kMahoArtifactExportUntrustedHost) {}

MahoArtifactExportUntrustedUIConfig::~MahoArtifactExportUntrustedUIConfig() =
    default;

std::unique_ptr<content::WebUIController>
MahoArtifactExportUntrustedUIConfig::CreateWebUIController(
    content::WebUI* web_ui,
    const GURL& url) {
  return std::make_unique<MahoArtifactExportUntrustedUI>(web_ui);
}

bool MahoArtifactExportUntrustedUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoArtifactExportUntrustedUI)

MahoArtifactExportUntrustedUI::MahoArtifactExportUntrustedUI(
    content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::URLDataSource::Add(
      Profile::FromWebUI(web_ui),
      std::make_unique<ArtifactExportDataSource>(Profile::FromWebUI(web_ui)));
}

MahoArtifactExportUntrustedUI::~MahoArtifactExportUntrustedUI() = default;
