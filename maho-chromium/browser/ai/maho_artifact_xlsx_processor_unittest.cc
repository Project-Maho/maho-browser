// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_artifact_xlsx_processor.h"

#include <zlib.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#ifndef MAHO_STANDALONE_TEST
#include "testing/gtest/include/gtest/gtest.h"
#endif

namespace maho::ai {
namespace {

// Helper: compress data with raw deflate for building synthetic test XLSX archives
std::string DeflateRaw(const std::string& data) {
  z_stream strm;
  std::memset(&strm, 0, sizeof(strm));
  if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    return "";
  }
  std::string output;
  output.resize(deflateBound(&strm, data.size()) + 64);
  strm.next_in =
      const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data()));
  strm.avail_in = static_cast<uInt>(data.size());
  strm.next_out = reinterpret_cast<Bytef*>(&output[0]);
  strm.avail_out = static_cast<uInt>(output.size());

  deflate(&strm, Z_FINISH);
  size_t compressed_len = output.size() - strm.avail_out;
  output.resize(compressed_len);
  deflateEnd(&strm);
  return output;
}

// Minimal zip builder for generating test XLSX files
std::string BuildTestZip(
    const std::vector<std::pair<std::string, std::string>>& files) {
  std::string zip_data;
  struct EntryMeta {
    std::string filename;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint32_t local_header_offset;
    uint32_t crc;
  };
  std::vector<EntryMeta> metas;

  for (const auto& [name, content] : files) {
    std::string comp = DeflateRaw(content);
    uint32_t offset = static_cast<uint32_t>(zip_data.size());
    uint32_t crc = crc32(
        0L, reinterpret_cast<const Bytef*>(content.data()), content.size());

    // Local file header (30 bytes)
    zip_data.push_back(0x50);
    zip_data.push_back(0x4b);
    zip_data.push_back(0x03);
    zip_data.push_back(0x04);
    zip_data.push_back(20);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(8);
    zip_data.push_back(0);  // deflate
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(crc & 0xff);
    zip_data.push_back((crc >> 8) & 0xff);
    zip_data.push_back((crc >> 16) & 0xff);
    zip_data.push_back((crc >> 24) & 0xff);

    uint32_t csz = static_cast<uint32_t>(comp.size());
    zip_data.push_back(csz & 0xff);
    zip_data.push_back((csz >> 8) & 0xff);
    zip_data.push_back((csz >> 16) & 0xff);
    zip_data.push_back((csz >> 24) & 0xff);

    uint32_t usz = static_cast<uint32_t>(content.size());
    zip_data.push_back(usz & 0xff);
    zip_data.push_back((usz >> 8) & 0xff);
    zip_data.push_back((usz >> 16) & 0xff);
    zip_data.push_back((usz >> 24) & 0xff);

    uint16_t fn_len = static_cast<uint16_t>(name.size());
    zip_data.push_back(fn_len & 0xff);
    zip_data.push_back((fn_len >> 8) & 0xff);
    zip_data.push_back(0);
    zip_data.push_back(0);

    zip_data.append(name);
    zip_data.append(comp);

    metas.push_back({name, csz, usz, offset, crc});
  }

  uint32_t cd_offset = static_cast<uint32_t>(zip_data.size());
  for (const auto& meta : metas) {
    // Central directory header (46 bytes)
    zip_data.push_back(0x50);
    zip_data.push_back(0x4b);
    zip_data.push_back(0x01);
    zip_data.push_back(0x02);
    zip_data.push_back(20);
    zip_data.push_back(0);
    zip_data.push_back(20);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(8);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);

    zip_data.push_back(meta.crc & 0xff);
    zip_data.push_back((meta.crc >> 8) & 0xff);
    zip_data.push_back((meta.crc >> 16) & 0xff);
    zip_data.push_back((meta.crc >> 24) & 0xff);

    zip_data.push_back(meta.comp_size & 0xff);
    zip_data.push_back((meta.comp_size >> 8) & 0xff);
    zip_data.push_back((meta.comp_size >> 16) & 0xff);
    zip_data.push_back((meta.comp_size >> 24) & 0xff);

    zip_data.push_back(meta.uncomp_size & 0xff);
    zip_data.push_back((meta.uncomp_size >> 8) & 0xff);
    zip_data.push_back((meta.uncomp_size >> 16) & 0xff);
    zip_data.push_back((meta.uncomp_size >> 24) & 0xff);

    uint16_t fn_len = static_cast<uint16_t>(meta.filename.size());
    zip_data.push_back(fn_len & 0xff);
    zip_data.push_back((fn_len >> 8) & 0xff);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);
    zip_data.push_back(0);

    zip_data.push_back(meta.local_header_offset & 0xff);
    zip_data.push_back((meta.local_header_offset >> 8) & 0xff);
    zip_data.push_back((meta.local_header_offset >> 16) & 0xff);
    zip_data.push_back((meta.local_header_offset >> 24) & 0xff);

    zip_data.append(meta.filename);
  }

  uint32_t cd_size = static_cast<uint32_t>(zip_data.size() - cd_offset);
  uint16_t total = static_cast<uint16_t>(metas.size());
  // EOCD (22 bytes)
  zip_data.push_back(0x50);
  zip_data.push_back(0x4b);
  zip_data.push_back(0x05);
  zip_data.push_back(0x06);
  zip_data.push_back(0);
  zip_data.push_back(0);
  zip_data.push_back(0);
  zip_data.push_back(0);
  zip_data.push_back(total & 0xff);
  zip_data.push_back((total >> 8) & 0xff);
  zip_data.push_back(total & 0xff);
  zip_data.push_back((total >> 8) & 0xff);
  zip_data.push_back(cd_size & 0xff);
  zip_data.push_back((cd_size >> 8) & 0xff);
  zip_data.push_back((cd_size >> 16) & 0xff);
  zip_data.push_back((cd_size >> 24) & 0xff);
  zip_data.push_back(cd_offset & 0xff);
  zip_data.push_back((cd_offset >> 8) & 0xff);
  zip_data.push_back((cd_offset >> 16) & 0xff);
  zip_data.push_back((cd_offset >> 24) & 0xff);
  zip_data.push_back(0);
  zip_data.push_back(0);

  return zip_data;
}

#ifndef MAHO_STANDALONE_TEST

TEST(MahoArtifactXlsxProcessorTest, ParsesValidMultiSheetXlsx) {
  std::vector<std::pair<std::string, std::string>> files = {
      {"xl/workbook.xml",
       "<workbook><sheets><sheet name=\"Sales\" sheetId=\"1\" "
       "r:id=\"rId1\"/><sheet name=\"Costs\" sheetId=\"2\" "
       "r:id=\"rId2\"/></sheets></workbook>"},
      {"xl/sharedStrings.xml",
       "<sst count=\"3\"><si><t>Q1</t></si><si><t>Q2</t></si><si><t>Revenue "
       "&amp; Growth</t></si></sst>"},
      {"xl/worksheets/sheet1.xml",
       "<worksheet><sheetData><row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c "
       "r=\"B1\" t=\"s\"><v>2</v></c></row><row r=\"2\"><c r=\"A2\" "
       "t=\"s\"><v>1</v></c><c r=\"B2\"><v>50000</v></c></row></sheetData></"
       "worksheet>"},
      {"xl/worksheets/sheet2.xml",
       "<worksheet><sheetData><row r=\"1\"><c r=\"A1\"><v>1000</v></c><c "
       "r=\"B1\"><v>2000</v></c></row></sheetData></worksheet>"}};

  std::string xlsx_zip = BuildTestZip(files);
  auto result = MahoArtifactXlsxProcessor::Process(xlsx_zip);
  ASSERT_TRUE(result.has_value()) << result.error();

  EXPECT_EQ(result->sheets.size(), 2u);
  EXPECT_EQ(result->sheets[0].name, "Sales");
  ASSERT_EQ(result->sheets[0].cells.size(), 2u);
  EXPECT_EQ(result->sheets[0].cells[0][0], "Q1");
  EXPECT_EQ(result->sheets[0].cells[0][1], "Revenue & Growth");
  EXPECT_EQ(result->sheets[0].cells[1][0], "Q2");
  EXPECT_EQ(result->sheets[0].cells[1][1], "50000");

  EXPECT_EQ(result->sheets[1].name, "Costs");
  ASSERT_EQ(result->sheets[1].cells.size(), 1u);
  EXPECT_EQ(result->sheets[1].cells[0][0], "1000");
  EXPECT_EQ(result->sheets[1].cells[0][1], "2000");

  std::string json = result->ToJson();
  EXPECT_NE(json.find("\"Sales\""), std::string::npos);
  EXPECT_NE(json.find("\"Revenue & Growth\""), std::string::npos);
}

TEST(MahoArtifactXlsxProcessorTest, ParsesInlineStringsAndBooleans) {
  std::vector<std::pair<std::string, std::string>> files = {
      {"xl/workbook.xml",
       "<workbook><sheets><sheet name=\"Flags\" sheetId=\"1\" "
       "r:id=\"rId1\"/></sheets></workbook>"},
      {"xl/worksheets/sheet1.xml",
       "<worksheet><sheetData><row r=\"1\"><c r=\"A1\" "
       "t=\"inlineStr\"><is><t>Status</t></is></c><c r=\"B1\" "
       "t=\"b\"><v>1</v></c></row></sheetData></worksheet>"}};

  std::string xlsx_zip = BuildTestZip(files);
  auto result = MahoArtifactXlsxProcessor::Process(xlsx_zip);
  ASSERT_TRUE(result.has_value()) << result.error();

  ASSERT_EQ(result->sheets.size(), 1u);
  EXPECT_EQ(result->sheets[0].name, "Flags");
  ASSERT_EQ(result->sheets[0].cells.size(), 1u);
  EXPECT_EQ(result->sheets[0].cells[0][0], "Status");
  EXPECT_EQ(result->sheets[0].cells[0][1], "TRUE");
}

TEST(MahoArtifactXlsxProcessorTest, FallbackToCsvOnPlainText) {
  std::string csv_data =
      "Product,Category,Price\nLaptop,Electronics,1200\nDesk,Furniture,350\n";
  auto result = MahoArtifactXlsxProcessor::Process(csv_data);
  ASSERT_TRUE(result.has_value()) << result.error();

  ASSERT_EQ(result->sheets.size(), 1u);
  EXPECT_EQ(result->sheets[0].name, "Sheet1");
  ASSERT_EQ(result->sheets[0].cells.size(), 3u);
  EXPECT_EQ(result->sheets[0].cells[0][0], "Product");
  EXPECT_EQ(result->sheets[0].cells[0][1], "Category");
  EXPECT_EQ(result->sheets[0].cells[0][2], "Price");
  EXPECT_EQ(result->sheets[0].cells[1][0], "Laptop");
  EXPECT_EQ(result->sheets[0].cells[1][2], "1200");
}

TEST(MahoArtifactXlsxProcessorTest, CorruptedZipReturnsTypedError) {
  std::string corrupt_zip = "PK\x03\x04\x00\x00\x00\x00randomcorruptedbytes";
  auto result = MahoArtifactXlsxProcessor::Process(corrupt_zip);
  EXPECT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("Corrupted or truncated XLSX"), std::string::npos);
}

TEST(MahoArtifactXlsxProcessorTest, EmptyDataReturnsTypedError) {
  auto result = MahoArtifactXlsxProcessor::Process("");
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), "Input XLSX data is empty");
}

TEST(MahoArtifactXlsxProcessorTest, NonZipBinaryGarbageReturnsTypedError) {
  std::string binary_garbage(
      "\x00\x01\x02\x03\x04\x05\x06\x07\x08\xff\xfe\xfd", 12);
  auto result = MahoArtifactXlsxProcessor::Process(binary_garbage);
  EXPECT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("Invalid XLSX format"), std::string::npos);
}

TEST(MahoArtifactXlsxProcessorTest, GeneratePreviewJsonProducesStructuredOutput) {
  std::string csv_data = "ColA,ColB\nVal1,Val2\n";
  auto preview_res = MahoArtifactXlsxProcessor::GeneratePreviewJson(csv_data);
  ASSERT_TRUE(preview_res.has_value()) << preview_res.error();
  EXPECT_EQ(*preview_res,
            "{\"sheets\":[{\"name\":\"Sheet1\",\"cells\":[[\"ColA\",\"ColB\"],["
            "\"Val1\",\"Val2\"]]}]}");
}

#endif  // !MAHO_STANDALONE_TEST

}  // namespace
}  // namespace maho::ai

#ifdef MAHO_STANDALONE_TEST
int main() {
  std::cout << "[RUN] MahoArtifactXlsxProcessor standalone tests..."
            << std::endl;

  using namespace maho::ai;

  // Test 1: Valid Multi-Sheet XLSX
  {
    std::vector<std::pair<std::string, std::string>> files = {
        {"xl/workbook.xml",
         "<workbook><sheets><sheet name=\"Sales\" sheetId=\"1\" "
         "r:id=\"rId1\"/><sheet name=\"Costs\" sheetId=\"2\" "
         "r:id=\"rId2\"/></sheets></workbook>"},
        {"xl/sharedStrings.xml",
         "<sst count=\"3\"><si><t>Q1</t></si><si><t>Q2</t></si><si><t>Revenue "
         "&amp; Growth</t></si></sst>"},
        {"xl/worksheets/sheet1.xml",
         "<worksheet><sheetData><row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c "
         "r=\"B1\" t=\"s\"><v>2</v></c></row><row r=\"2\"><c r=\"A2\" "
         "t=\"s\"><v>1</v></c><c r=\"B2\"><v>50000</v></c></row></sheetData></"
         "worksheet>"},
        {"xl/worksheets/sheet2.xml",
         "<worksheet><sheetData><row r=\"1\"><c r=\"A1\"><v>1000</v></c><c "
         "r=\"B1\"><v>2000</v></c></row></sheetData></worksheet>"}};

    std::string xlsx_zip = BuildTestZip(files);
    auto result = MahoArtifactXlsxProcessor::Process(xlsx_zip);
    assert(result.has_value());
    assert(result->sheets.size() == 2u);
    assert(result->sheets[0].name == "Sales");
    assert(result->sheets[0].cells.size() == 2u);
    assert(result->sheets[0].cells[0][0] == "Q1");
    assert(result->sheets[0].cells[0][1] == "Revenue & Growth");
    assert(result->sheets[0].cells[1][0] == "Q2");
    assert(result->sheets[0].cells[1][1] == "50000");

    assert(result->sheets[1].name == "Costs");
    assert(result->sheets[1].cells.size() == 1u);
    assert(result->sheets[1].cells[0][0] == "1000");
    assert(result->sheets[1].cells[0][1] == "2000");
  }

  // Test 2: Inline Strings and Booleans
  {
    std::vector<std::pair<std::string, std::string>> files = {
        {"xl/workbook.xml",
         "<workbook><sheets><sheet name=\"Flags\" sheetId=\"1\" "
         "r:id=\"rId1\"/></sheets></workbook>"},
        {"xl/worksheets/sheet1.xml",
         "<worksheet><sheetData><row r=\"1\"><c r=\"A1\" "
         "t=\"inlineStr\"><is><t>Status</t></is></c><c r=\"B1\" "
         "t=\"b\"><v>1</v></c></row></sheetData></worksheet>"}};

    std::string xlsx_zip = BuildTestZip(files);
    auto result = MahoArtifactXlsxProcessor::Process(xlsx_zip);
    assert(result.has_value());
    assert(result->sheets.size() == 1u);
    assert(result->sheets[0].name == "Flags");
    assert(result->sheets[0].cells.size() == 1u);
    assert(result->sheets[0].cells[0][0] == "Status");
    assert(result->sheets[0].cells[0][1] == "TRUE");
  }

  // Test 3: CSV Fallback
  {
    std::string csv_data =
        "Product,Category,Price\nLaptop,Electronics,1200\nDesk,Furniture,350\n";
    auto result = MahoArtifactXlsxProcessor::Process(csv_data);
    assert(result.has_value());
    assert(result->sheets.size() == 1u);
    assert(result->sheets[0].name == "Sheet1");
    assert(result->sheets[0].cells.size() == 3u);
    assert(result->sheets[0].cells[0][0] == "Product");
    assert(result->sheets[0].cells[0][1] == "Category");
    assert(result->sheets[0].cells[0][2] == "Price");
    assert(result->sheets[0].cells[1][0] == "Laptop");
    assert(result->sheets[0].cells[1][2] == "1200");
  }

  // Test 4: Corrupt Zip Error
  {
    std::string corrupt_zip =
        "PK\x03\x04\x00\x00\x00\x00randomcorruptedbytes";
    auto result = MahoArtifactXlsxProcessor::Process(corrupt_zip);
    assert(!result.has_value());
    assert(result.error().find("Corrupted or truncated XLSX") !=
           std::string::npos);
  }

  // Test 5: Empty Data Error
  {
    auto result = MahoArtifactXlsxProcessor::Process("");
    assert(!result.has_value());
    assert(result.error() == "Input XLSX data is empty");
  }

  // Test 6: Non-Zip Binary Error
  {
    std::string binary_garbage(
        "\x00\x01\x02\x03\x04\x05\x06\x07\x08\xff\xfe\xfd", 12);
    auto result = MahoArtifactXlsxProcessor::Process(binary_garbage);
    assert(!result.has_value());
    assert(result.error().find("Invalid XLSX format") != std::string::npos);
  }

  // Test 7: GeneratePreviewJson
  {
    std::string csv_data = "ColA,ColB\nVal1,Val2\n";
    auto preview_res = MahoArtifactXlsxProcessor::GeneratePreviewJson(csv_data);
    assert(preview_res.has_value());
    assert(*preview_res ==
           "{\"sheets\":[{\"name\":\"Sheet1\",\"cells\":[[\"ColA\",\"ColB\"],["
           "\"Val1\",\"Val2\"]]}]}");
  }

  std::cout << "[PASS] All MahoArtifactXlsxProcessor standalone tests passed!"
            << std::endl;
  return 0;
}
#endif
