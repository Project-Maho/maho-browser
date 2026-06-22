// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/chromium_autofill_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "components/autofill/core/common/form_field_data.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::importer {
namespace {

TEST(ChromiumAutofillParserTest, ParseEmptyOrMissing) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath fake_db = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Web Data"));
  auto results = ParseChromiumAutofillEntries(fake_db);
  EXPECT_TRUE(results.empty());
}

TEST(ChromiumAutofillParserTest, ParseOldSchema) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath db_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Web Data"));
  sql::Database db("Web");
  ASSERT_TRUE(db.Open(db_path));

  ASSERT_TRUE(db.Execute("CREATE TABLE autofill ("
                         "name TEXT, value TEXT, count INTEGER)"));
  ASSERT_TRUE(db.Execute("INSERT INTO autofill (name, value, count) VALUES ('email', 'test@example.com', 5)"));
  ASSERT_TRUE(db.Execute("INSERT INTO autofill (name, value, count) VALUES ('username', 'testuser', 10)"));
  db.Close();

  auto results = ParseChromiumAutofillEntries(db_path);
  ASSERT_EQ(results.size(), 2u);

  // Sorted by count DESC
  EXPECT_EQ(results[0].name(), u"username");
  EXPECT_EQ(results[0].value(), u"testuser");

  EXPECT_EQ(results[1].name(), u"email");
  EXPECT_EQ(results[1].value(), u"test@example.com");
}

TEST(ChromiumAutofillParserTest, ParseNewSchema) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath db_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Web Data"));
  sql::Database db("Web");
  ASSERT_TRUE(db.Open(db_path));

  // local_addresses
  ASSERT_TRUE(db.Execute("CREATE TABLE local_addresses ("
                         "guid TEXT, first_name TEXT, email TEXT, use_count INTEGER)"));
  ASSERT_TRUE(db.Execute("INSERT INTO local_addresses (guid, first_name, email, use_count) "
                         "VALUES ('guid1', 'John', 'john@example.com', 1)"));

  // local_credit_cards
  ASSERT_TRUE(db.Execute("CREATE TABLE local_credit_cards ("
                         "guid TEXT, name_on_card TEXT, use_count INTEGER, card_number_encrypted BLOB)"));
  ASSERT_TRUE(db.Execute("INSERT INTO local_credit_cards (guid, name_on_card, use_count, card_number_encrypted) "
                         "VALUES ('guid2', 'John Doe', 1, x'01020304')"));
  db.Close();

  auto results = ParseChromiumAutofillEntries(db_path);
  // We expect first_name, email, and name_on_card to be extracted as FormFieldData.
  // guid, use_count, card_number_encrypted should be excluded.
  ASSERT_EQ(results.size(), 3u);

  bool found_first_name = false;
  bool found_email = false;
  bool found_name_on_card = false;

  for (const auto& entry : results) {
    if (entry.name() == u"first_name") {
      EXPECT_EQ(entry.value(), u"John");
      found_first_name = true;
    } else if (entry.name() == u"email") {
      EXPECT_EQ(entry.value(), u"john@example.com");
      found_email = true;
    } else if (entry.name() == u"name_on_card") {
      EXPECT_EQ(entry.value(), u"John Doe");
      found_name_on_card = true;
    }
  }

  EXPECT_TRUE(found_first_name);
  EXPECT_TRUE(found_email);
  EXPECT_TRUE(found_name_on_card);
}

}  // namespace
}  // namespace maho::importer
