// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_artifact_preview_untrusted_ui.h"

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
#include "maho/browser/ai/maho_artifact_html_sanitizer.h"
#include "maho/browser/ai/maho_artifact_pdf_processor.h"
#include "maho/browser/ai/maho_artifact_registry.h"
#include "maho/browser/ai/maho_artifact_xlsx_processor.h"
#include "maho/browser/ui/webui/maho_webui_private_boundary.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace {

constexpr int64_t kMaxPreviewInputBytes = 8 * 1024 * 1024;
constexpr char kPreviewPurpose[] = "preview";

std::string ProcessArtifactForPreview(base::FilePath artifact_root,
                                      std::string storage_rel_path,
                                      maho::ai::MahoArtifactKind kind) {
  // Canonicalize + containment-check on this MayBlock sequence (realpath
  // resolution blocks and must not run on the UI thread). An escaped or
  // invalid path yields empty (sanitized) output, never the target file.
  std::optional<base::FilePath> path =
      maho::ai::MahoArtifactRegistry::ResolveContainedStoragePath(
          artifact_root, storage_rel_path);
  std::string contents;
  if (path) {
    base::ReadFileToStringWithMaxSize(*path, &contents, kMaxPreviewInputBytes);
  }

  switch (kind) {
    case maho::ai::MahoArtifactKind::kXlsx: {
      auto json_res =
          maho::ai::MahoArtifactXlsxProcessor::GeneratePreviewJson(contents);
      return json_res.value_or(
          "{\"sheets\":[],\"error\":\"Failed to parse spreadsheet preview\"}");
    }
    case maho::ai::MahoArtifactKind::kPdf: {
      auto pdf_res =
          maho::ai::MahoArtifactPdfProcessor::ValidateForPreview(contents);
      return pdf_res.value_or("");
    }
    case maho::ai::MahoArtifactKind::kHtml:
    case maho::ai::MahoArtifactKind::kGeneric:
    default:
      return maho::SanitizeArtifactHtml(contents);
  }
}

void RunGotDataWithString(content::URLDataSource::GotDataCallback callback,
                          std::string data) {
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(std::move(data)));
}

class ArtifactPreviewDataSource : public content::URLDataSource {
 public:
  explicit ArtifactPreviewDataSource(Profile* profile) : profile_(profile) {}
  ArtifactPreviewDataSource(const ArtifactPreviewDataSource&) = delete;
  ArtifactPreviewDataSource& operator=(const ArtifactPreviewDataSource&) =
      delete;
  ~ArtifactPreviewDataSource() override = default;

  std::string GetSource() override {
    return maho::kMahoArtifactPreviewUntrustedURL;
  }

  void StartDataRequest(const GURL& url,
                        const content::WebContents::Getter& wc_getter,
                        GotDataCallback callback) override {
    std::string token;
    net::GetValueForKeyInQuery(url, "cap", &token);
    content::WebContents* web_contents = wc_getter.Run();
    maho::ai::MahoArtifactRegistry* registry =
        (web_contents && profile_)
            ? maho::ai::MahoArtifactRegistry::GetForProfile(profile_)
            : nullptr;
    std::optional<maho::ai::MahoArtifact> artifact;
    if (registry && web_contents && !token.empty()) {
      artifact =
          registry->VerifyCapability(token, kPreviewPurpose, web_contents);
    }
    if (!artifact) {
      std::move(callback).Run(nullptr);
      return;
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&ProcessArtifactForPreview, registry->artifact_root(),
                       artifact->storage_rel_path, artifact->kind),
        base::BindOnce(&RunGotDataWithString, std::move(callback)));
  }

  std::string GetMimeType(const GURL& url) override {
    std::string token;
    net::GetValueForKeyInQuery(url, "cap", &token);
    maho::ai::MahoArtifactRegistry* registry =
        profile_ ? maho::ai::MahoArtifactRegistry::GetForProfile(profile_)
                 : nullptr;
    const std::optional<maho::ai::MahoArtifact> artifact =
        (registry && !token.empty())
            ? registry->GetCapabilityArtifactForResponseMetadata(
                  token, kPreviewPurpose)
            : std::nullopt;
    if (!artifact) {
      return "text/html";
    }
    switch (artifact->kind) {
      case maho::ai::MahoArtifactKind::kPdf:
        return "application/pdf";
      case maho::ai::MahoArtifactKind::kXlsx:
        return "application/json";
      case maho::ai::MahoArtifactKind::kHtml:
      case maho::ai::MahoArtifactKind::kGeneric:
      default:
        return "text/html";
    }
  }

  bool ShouldServeMimeTypeAsContentTypeHeader() override { return true; }

  bool ShouldReplaceExistingSource() override { return true; }
  bool AllowCaching() override { return false; }
  bool ShouldDenyXFrameOptions() override { return false; }

  std::string GetContentSecurityPolicy(
      network::mojom::CSPDirectiveName directive) override {
    switch (directive) {
      case network::mojom::CSPDirectiveName::DefaultSrc:
        return "default-src 'none';";
      case network::mojom::CSPDirectiveName::ScriptSrc:
        return "script-src 'none';";
      case network::mojom::CSPDirectiveName::ConnectSrc:
        return "connect-src 'none';";
      case network::mojom::CSPDirectiveName::ObjectSrc:
        return "object-src 'none';";
      case network::mojom::CSPDirectiveName::FrameSrc:
        return "frame-src 'none';";
      case network::mojom::CSPDirectiveName::FormAction:
        return "form-action 'none';";
      case network::mojom::CSPDirectiveName::BaseURI:
        return "base-uri 'none';";
      case network::mojom::CSPDirectiveName::StyleSrc:
        return "style-src 'unsafe-inline';";
      case network::mojom::CSPDirectiveName::ImgSrc:
        return "img-src data: blob:;";
      case network::mojom::CSPDirectiveName::Sandbox:
        return "sandbox;";
      case network::mojom::CSPDirectiveName::FrameAncestors:
        return "frame-ancestors chrome://maho-ai;";
      default:
        return content::URLDataSource::GetContentSecurityPolicy(directive);
    }
  }

 private:
  const raw_ptr<Profile> profile_;
};

}  // namespace

MahoArtifactPreviewUntrustedUIConfig::MahoArtifactPreviewUntrustedUIConfig()
    : WebUIConfig(content::kChromeUIUntrustedScheme,
                  maho::kMahoArtifactPreviewUntrustedHost) {}

MahoArtifactPreviewUntrustedUIConfig::~MahoArtifactPreviewUntrustedUIConfig() =
    default;

std::unique_ptr<content::WebUIController>
MahoArtifactPreviewUntrustedUIConfig::CreateWebUIController(
    content::WebUI* web_ui,
    const GURL& url) {
  return std::make_unique<MahoArtifactPreviewUntrustedUI>(web_ui);
}

bool MahoArtifactPreviewUntrustedUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoArtifactPreviewUntrustedUI)

MahoArtifactPreviewUntrustedUI::MahoArtifactPreviewUntrustedUI(
    content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::URLDataSource::Add(
      Profile::FromWebUI(web_ui),
      std::make_unique<ArtifactPreviewDataSource>(Profile::FromWebUI(web_ui)));
}

MahoArtifactPreviewUntrustedUI::~MahoArtifactPreviewUntrustedUI() = default;
