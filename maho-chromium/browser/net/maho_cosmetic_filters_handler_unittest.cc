// Copyright 2026 Maho Browser. All rights reserved.

#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

std::string BuildPayloadForTest(const std::string& resources) {
  auto parsed = base::JSONReader::Read(resources);
  if (!parsed || !parsed->is_dict()) return std::string();
  const auto& dict = parsed->GetDict();
  base::DictValue payload;
  for (const char* key : {"hideSelectors", "proceduralActions", "exceptions"}) {
    if (const auto* list = dict.FindList(key)) payload.Set(key, list->Clone());
  }
  payload.Set("generichide", dict.FindBool("generichide").value_or(false));
  return base::WriteJson(base::Value(std::move(payload))).value_or(std::string());
}

TEST(MahoCosmeticFiltersHandlerTest, InitPayloadContainsAllCosmeticFields) {
  const auto payload = BuildPayloadForTest(
      R"({"hideSelectors":[".ad"],"proceduralActions":["{}"],"exceptions":[".ok"],"generichide":false})");
  auto parsed = base::JSONReader::Read(payload);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const auto& dict = parsed->GetDict();
  EXPECT_EQ(dict.FindList("hideSelectors")->size(), 1u);
  EXPECT_EQ(dict.FindList("proceduralActions")->size(), 1u);
  EXPECT_EQ(dict.FindList("exceptions")->size(), 1u);
  EXPECT_FALSE(dict.FindBool("generichide").value());
}

TEST(MahoCosmeticFiltersHandlerTest, InvalidAndStalePayloadsAreEmpty) {
  EXPECT_TRUE(BuildPayloadForTest("not-json").empty());
  EXPECT_TRUE(BuildPayloadForTest("{}").empty());
}

}
