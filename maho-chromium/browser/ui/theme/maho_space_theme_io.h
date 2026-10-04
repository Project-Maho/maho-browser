// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_IO_H_
#define MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_IO_H_

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/values.h"

struct MahoCore;

namespace maho_theme {

std::optional<std::string> SpaceColorDictToHex(
    const base::DictValue& color);

std::optional<std::string> SerializeSpaceThemeJson(
    const base::DictValue& theme);

// Serializes the current theme for |space_id| as pretty-printed JSON.
std::optional<std::string> SerializeSpaceThemeJson(MahoCore* core,
                                                   const std::string& space_id);

// Writes the serialized theme for |space_id| to |path|.
bool ExportSpaceThemeToFile(MahoCore* core,
                         const std::string& space_id,
                         const base::FilePath& path);

// Reads and validates a theme JSON file before importing.
std::optional<std::string> ReadValidatedThemeJson(const base::FilePath& path);

// Applies a validated theme JSON payload to |space_id|.
bool ApplyThemeJsonToSpace(MahoCore* core,
                           const std::string& space_id,
                           const std::string& theme_json);

}  // namespace maho_theme

#endif  // MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_IO_H_
