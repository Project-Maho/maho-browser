// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_MOZLZ4_DECOMPRESSOR_H_
#define MAHO_BROWSER_IMPORTER_MOZLZ4_DECOMPRESSOR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace maho::importer {

// mozLz4 format: 8-byte magic "mozLz40\0" + 4-byte LE uncompressed size + LZ4 block.
std::optional<std::string> DecompressMozLz4(const base::FilePath& path);
std::optional<std::string> DecompressMozLz4FromData(
    const std::vector<uint8_t>& data);

}  // namespace maho::importer

#endif  // MAHO_BROWSER_IMPORTER_MOZLZ4_DECOMPRESSOR_H_
