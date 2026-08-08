// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_IMPORTERS_ESSENTIAL_IMPORTER_H_
#define MAHO_BROWSER_IMPORTER_IMPORTERS_ESSENTIAL_IMPORTER_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"

namespace maho {

class EssentialImporter {
 public:
  using ProgressCallback = base::RepeatingCallback<
      void(uint32_t type, int32_t count, const std::string& error, bool complete)>;

  EssentialImporter();
  ~EssentialImporter();

  EssentialImporter(const EssentialImporter&) = delete;
  EssentialImporter& operator=(const EssentialImporter&) = delete;

  void SetSelectedUrls(std::vector<std::string> urls);
  void Cancel();

  void FavoriteEssentialSites(const std::vector<std::string>& urls,
                              base::OnceClosure completion_callback);

 private:
  void OnEssentialSitesFavorited(base::OnceClosure completion_callback,
                                 ProgressCallback progress_callback,
                                 std::vector<std::string> valid_urls);

  std::vector<std::string> urls_;
  bool cancelled_ = false;

  [[maybe_unused]] base::SequenceChecker sequence_checker_;
  base::WeakPtrFactory<EssentialImporter> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_IMPORTERS_ESSENTIAL_IMPORTER_H_
