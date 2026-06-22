// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_CHROMIUM_AUTOFILL_PARSER_H_
#define MAHO_BROWSER_IMPORTER_CHROMIUM_AUTOFILL_PARSER_H_

#include <vector>

#include "base/files/file_path.h"
#include "components/autofill/core/common/form_field_data.h"

namespace maho::importer {

// Parses the "autofill" (autocomplete) table from a Chromium-family browser's
// "Web Data" SQLite database and returns FormFieldData entries suitable for
// passing to AutofillWebDataService::AddFormFields().
//
// |web_data_path| should point to the "Web Data" file.
// Returns an empty vector on failure.
std::vector<autofill::FormFieldData> ParseChromiumAutofillEntries(
    const base::FilePath& web_data_path);

}  // namespace maho::importer

#endif  // MAHO_BROWSER_IMPORTER_CHROMIUM_AUTOFILL_PARSER_H_
