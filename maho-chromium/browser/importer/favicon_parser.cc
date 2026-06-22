// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/favicon_parser.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "components/favicon_base/favicon_usage_data.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

namespace maho::importer {

namespace {

bool MatchMimeType(const std::vector<uint8_t>& data) {
  if (data.size() < 4) {
    return false;
  }
  // PNG
  if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
    return true;
  }
  // ICO
  if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01 && data[3] == 0x00) {
    return true;
  }
  // JPEG
  if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    return true;
  }
  // GIF
  if (data.size() >= 6 &&
      data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8' &&
      (data[4] == '7' || data[4] == '9') && data[5] == 'a') {
    return true;
  }
  // WebP
  if (data.size() >= 12 &&
      data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
      data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
    return true;
  }
  // SVG (XML / text)
  std::string_view text(reinterpret_cast<const char*>(data.data()), std::min(data.size(), size_t(100)));
  if (text.find("<svg") != std::string_view::npos || text.find("<?xml") != std::string_view::npos) {
    return true;
  }

  return false;
}

bool IsValidFaviconData(const std::vector<uint8_t>& data) {
  if (data.size() > 1024 * 1024) {
    return false;
  }
  if (!MatchMimeType(data)) {
    return false;
  }
  // PNG dimension check
  if (data.size() >= 24 &&
      data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
    uint32_t width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    uint32_t height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    if (width > 256 || height > 256) {
      return false;
    }
  }
  // ICO dimension check
  if (data.size() >= 8 &&
      data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01 && data[3] == 0x00) {
    uint32_t num_images = data[4] | (data[5] << 8);
    if (data.size() >= 6 + num_images * 16) {
      for (uint32_t i = 0; i < num_images; ++i) {
        size_t entry_offset = 6 + i * 16;
        uint8_t w_byte = data[entry_offset];
        uint8_t h_byte = data[entry_offset + 1];
        uint32_t width = (w_byte == 0) ? 256 : w_byte;
        uint32_t height = (h_byte == 0) ? 256 : h_byte;
        if (width > 256 || height > 256) {
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace

favicon_base::FaviconUsageDataList ParseChromiumFavicons(
    const base::FilePath& favicons_db_path) {
  favicon_base::FaviconUsageDataList result;

  if (!base::PathExists(favicons_db_path)) {
    return result;
  }

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return result;
  }

  base::FilePath db_copy = temp_dir.GetPath().Append("Favicons");
  if (!base::CopyFile(favicons_db_path, db_copy)) {
    return result;
  }

  sql::Database db("MahoImporterFavicons");
  if (!db.Open(db_copy)) {
    return result;
  }

  if (!db.DoesTableExist("icon_mapping") ||
      !db.DoesTableExist("favicon_bitmaps") ||
      !db.DoesTableExist("favicons")) {
    return result;
  }

  sql::Statement stmt(db.GetUniqueStatement(
      "SELECT f.url, im.page_url, fb.image_data "
      "FROM icon_mapping im "
      "JOIN favicons f ON f.id = im.icon_id "
      "JOIN favicon_bitmaps fb ON fb.icon_id = f.id "
      "WHERE fb.image_data IS NOT NULL AND length(fb.image_data) > 0 "
      "ORDER BY f.url"));
  if (!stmt.is_valid()) {
    return result;
  }

  std::string current_favicon_url;
  favicon_base::FaviconUsageData* current_entry = nullptr;

  while (stmt.Step()) {
    std::string favicon_url = stmt.ColumnString(0);
    std::string page_url = stmt.ColumnString(1);
    std::vector<uint8_t> blob = stmt.ColumnBlobAsVector(2);

    if (!IsValidFaviconData(blob)) {
      continue;
    }

    if (favicon_url != current_favicon_url) {
      result.emplace_back();
      current_entry = &result.back();
      current_entry->favicon_url = GURL(favicon_url);
      current_entry->png_data.assign(blob.begin(), blob.end());
      current_favicon_url = favicon_url;
    }

    if (current_entry) {
      GURL url(page_url);
      if (url.is_valid()) {
        current_entry->urls.insert(url);
      }
    }
  }

  return result;
}

favicon_base::FaviconUsageDataList ParseFirefoxFavicons(
    const base::FilePath& favicons_db_path) {
  favicon_base::FaviconUsageDataList result;

  if (!base::PathExists(favicons_db_path)) {
    return result;
  }

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return result;
  }

  base::FilePath db_copy = temp_dir.GetPath().Append("favicons.sqlite");
  if (!base::CopyFile(favicons_db_path, db_copy)) {
    return result;
  }

  sql::Database db("MahoImporterFirefoxFavicons");
  if (!db.Open(db_copy)) {
    return result;
  }

  if (!db.DoesTableExist("moz_icons") ||
      !db.DoesTableExist("moz_icons_to_pages") ||
      !db.DoesTableExist("moz_pages_w_icons")) {
    return result;
  }

  sql::Statement stmt(db.GetUniqueStatement(
      "SELECT mi.icon_url, mp.page_url, mi.data "
      "FROM moz_icons mi "
      "JOIN moz_icons_to_pages itp ON itp.icon_id = mi.id "
      "JOIN moz_pages_w_icons mp ON mp.id = itp.page_id "
      "WHERE mi.data IS NOT NULL AND length(mi.data) > 0 "
      "ORDER BY mi.icon_url"));
  if (!stmt.is_valid()) {
    return result;
  }

  std::string current_icon_url;
  favicon_base::FaviconUsageData* current_entry = nullptr;

  while (stmt.Step()) {
    std::string icon_url = stmt.ColumnString(0);
    std::string page_url = stmt.ColumnString(1);
    std::vector<uint8_t> blob = stmt.ColumnBlobAsVector(2);

    if (!IsValidFaviconData(blob)) {
      continue;
    }

    if (icon_url != current_icon_url) {
      result.emplace_back();
      current_entry = &result.back();
      current_entry->favicon_url = GURL(icon_url);
      current_entry->png_data.assign(blob.begin(), blob.end());
      current_icon_url = icon_url;
    }

    if (current_entry) {
      GURL url(page_url);
      if (url.is_valid()) {
        current_entry->urls.insert(url);
      }
    }
  }

  return result;
}

}  // namespace maho::importer
