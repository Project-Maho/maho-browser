// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_MAHO_UPDATE_SIGNATURE_WIN_H_
#define MAHO_BROWSER_UPDATES_MAHO_UPDATE_SIGNATURE_WIN_H_

#include <string_view>

#include "base/files/file_path.h"

namespace maho {
namespace updates {

bool VerifyAuthenticode(const base::FilePath& file_path,
                        std::string_view expected_signer_cn);

}  // namespace updates
}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_MAHO_UPDATE_SIGNATURE_WIN_H_
