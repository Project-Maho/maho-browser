// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/chromium_autofill_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "components/autofill/core/common/form_field_data.h"
#include "sql/database.h"
#include "sql/statement.h"

#include <unordered_set>

namespace maho::importer {

namespace {

void ParseTableIntoFields(sql::Database& db,
                          const std::string& table_name,
                          const std::unordered_set<std::string>& exclude_cols,
                          std::vector<autofill::FormFieldData>& entries) {
  if (!db.DoesTableExist(table_name.c_str())) {
    return;
  }

  std::string pragma_query = "PRAGMA table_info(" + table_name + ")";
  sql::Statement info_stmt(db.GetUniqueStatement(pragma_query));
  if (!info_stmt.is_valid()) {
    return;
  }

  std::vector<std::string> columns;
  while (info_stmt.Step()) {
    std::string col_name = info_stmt.ColumnString(1);
    if (exclude_cols.count(col_name) == 0) {
      columns.push_back(col_name);
    }
  }

  if (columns.empty()) {
    return;
  }

  std::string query = "SELECT ";
  for (size_t i = 0; i < columns.size(); ++i) {
    query += columns[i];
    if (i + 1 < columns.size()) {
      query += ", ";
    }
  }
  query += " FROM " + table_name;

  sql::Statement select_stmt(db.GetUniqueStatement(query));
  if (!select_stmt.is_valid()) {
    return;
  }

  while (select_stmt.Step()) {
    for (size_t i = 0; i < columns.size(); ++i) {
      std::u16string value = select_stmt.ColumnString16(i);
      if (!value.empty()) {
        autofill::FormFieldData field;
        field.set_name(base::UTF8ToUTF16(columns[i]));
        field.set_value(std::move(value));
        entries.push_back(std::move(field));
      }
    }
  }
}

}  // namespace

std::vector<autofill::FormFieldData> ParseChromiumAutofillEntries(
    const base::FilePath& web_data_path) {
  std::vector<autofill::FormFieldData> entries;

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    LOG(WARNING) << "Failed to create temp dir for Web Data";
    return entries;
  }

  base::FilePath temp_db_path = temp_dir.GetPath().AppendASCII("Web Data");
  if (!base::CopyFile(web_data_path, temp_db_path)) {
    LOG(WARNING) << "Failed to copy Web Data to temp";
    return entries;
  }

  sql::Database db("MahoImporterChromiumAutofill");
  if (!db.Open(temp_db_path)) {
    LOG(WARNING) << "Failed to open Web Data: " << temp_db_path;
    return entries;
  }

  if (db.DoesTableExist("autofill")) {
    sql::Statement stmt(db.GetUniqueStatement(
        "SELECT name, value FROM autofill ORDER BY count DESC"));
    if (stmt.is_valid()) {
      while (stmt.Step()) {
        std::u16string name = stmt.ColumnString16(0);
        std::u16string value = stmt.ColumnString16(1);

        if (name.empty() || value.empty()) {
          continue;
        }

        autofill::FormFieldData field;
        field.set_name(std::move(name));
        field.set_value(std::move(value));
        entries.push_back(std::move(field));
      }
    }
  }

  std::unordered_set<std::string> address_excludes = {
      "guid", "use_count", "use_date", "date_modified", "language_code",
      "date_created", "record_type", "id"
  };
  ParseTableIntoFields(db, "local_addresses", address_excludes, entries);

  std::unordered_set<std::string> card_excludes = {
      "guid", "use_count", "use_date", "date_modified", "card_number_encrypted",
      "billing_address_id", "id"
  };
  ParseTableIntoFields(db, "local_credit_cards", card_excludes, entries);

  return entries;
}

}  // namespace maho::importer
