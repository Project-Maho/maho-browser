// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/importers/essential_importer.h"
#include "maho/browser/importer/maho_space_ffi_client.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/components/maho_importer/maho_importer_constants.mojom.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/task/thread_pool.h"
#include "base/task/sequenced_task_runner.h"
#include "url/gurl.h"
#include "content/public/browser/browser_thread.h"

namespace maho {

EssentialImporter::EssentialImporter() {}

EssentialImporter::~EssentialImporter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void EssentialImporter::SetSelectedUrls(std::vector<std::string> urls) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  urls_ = std::move(urls);
}

void EssentialImporter::Cancel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  cancelled_ = true;
}

void EssentialImporter::FavoriteEssentialSites(const std::vector<std::string>& urls,
                                               base::OnceClosure completion_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](std::vector<std::string> urls) -> std::vector<std::string> {
            std::vector<std::string> valid_urls;
            for (const auto& url_str : urls) {
              GURL url(url_str);
              if (url.is_valid()) {
                valid_urls.push_back(url_str);
              }
            }
            return valid_urls;
          },
          urls),
      base::BindOnce(&EssentialImporter::OnEssentialSitesFavorited,
                     weak_factory_.GetWeakPtr(),
                     std::move(completion_callback),
                     ProgressCallback()));
}

void EssentialImporter::OnEssentialSitesFavorited(
    base::OnceClosure completion_callback,
    ProgressCallback progress_callback,
    std::vector<std::string> valid_urls) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  int32_t count = 0;

  if (!cancelled_) {
    auto* bridge = MahoSpaceProfileBridge::GetInstance();
    if (bridge) {
      std::string space_id = bridge->GetActiveSpaceId();
      if (!space_id.empty() && !valid_urls.empty()) {
        auto* client = MahoSpaceFFIClient::GetInstance();
        for (const auto& url : valid_urls) {
          std::string tab_id = client->CreateTab(space_id, url);
          if (!tab_id.empty()) {
            client->FavoriteTab(tab_id);
            count++;
          }
        }
        bridge->NotifyChanged();
      }
    }
  }

  if (completion_callback) {
    std::move(completion_callback).Run();
  }

  if (progress_callback) {
    progress_callback.Run(mojom::kImportEssential, count, "", true);
  }
}

}  // namespace maho
