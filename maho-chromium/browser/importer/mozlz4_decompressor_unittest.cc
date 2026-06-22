// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/mozlz4_decompressor.h"

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::importer {
namespace {

// Helper to create a literal-only MozLz4 block
std::vector<uint8_t> CreateMozLz4(const std::string& json, bool valid_magic = true, uint32_t override_size = 0) {
  std::vector<uint8_t> compressed;
  // 8-byte magic: "mozLz40\0"
  const uint8_t magic[] = {'m', 'o', 'z', 'L', 'z', '4', '0', '\0'};
  const uint8_t bad_magic[] = {'b', 'a', 'd', 'L', 'z', '4', '0', '\0'};
  compressed.insert(compressed.end(), valid_magic ? magic : bad_magic, (valid_magic ? magic : bad_magic) + 8);

  // 4-byte LE uncompressed size
  uint32_t size = (override_size > 0) ? override_size : json.size();
  compressed.push_back(size & 0xff);
  compressed.push_back((size >> 8) & 0xff);
  compressed.push_back((size >> 16) & 0xff);
  compressed.push_back((size >> 24) & 0xff);

  // LZ4 literal-only block
  size_t L = json.size();
  if (L < 15) {
    compressed.push_back(static_cast<uint8_t>(L << 4));
  } else {
    compressed.push_back(0xf0);
    size_t remaining = L - 15;
    while (remaining >= 255) {
      compressed.push_back(255);
      remaining -= 255;
    }
    compressed.push_back(static_cast<uint8_t>(remaining));
  }
  compressed.insert(compressed.end(), json.begin(), json.end());
  return compressed;
}

TEST(MozLz4DecompressorTest, DecompressValidData) {
  std::string original = "Hello World! This is a test string for MozLz4 decompression.";
  std::vector<uint8_t> data = CreateMozLz4(original);

  auto decompressed = DecompressMozLz4FromData(data);
  ASSERT_TRUE(decompressed.has_value());
  EXPECT_EQ(*decompressed, original);
}

TEST(MozLz4DecompressorTest, DecompressWrongMagic) {
  std::string original = "Some data";
  std::vector<uint8_t> data = CreateMozLz4(original, /*valid_magic=*/false);

  auto decompressed = DecompressMozLz4FromData(data);
  EXPECT_FALSE(decompressed.has_value());
}

TEST(MozLz4DecompressorTest, DecompressOversized) {
  std::string original = "Some data";
  // Override uncompressed size to be 257MB (257 * 1024 * 1024)
  uint32_t oversized = 257 * 1024 * 1024;
  std::vector<uint8_t> data = CreateMozLz4(original, /*valid_magic=*/true, oversized);

  auto decompressed = DecompressMozLz4FromData(data);
  EXPECT_FALSE(decompressed.has_value());
}

TEST(MozLz4DecompressorTest, DecompressCorruptPayload) {
  std::string original = "Hello World!";
  std::vector<uint8_t> data = CreateMozLz4(original);
  // Corrupt the LZ4 payload (make it too short)
  data.pop_back();

  auto decompressed = DecompressMozLz4FromData(data);
  EXPECT_FALSE(decompressed.has_value());
}

TEST(MozLz4DecompressorTest, DecompressFromFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  std::string original = "File-based test content";
  std::vector<uint8_t> data = CreateMozLz4(original);

  base::FilePath file_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("test.jsonlz4"));
  ASSERT_TRUE(base::WriteFile(file_path, data));

  auto decompressed = DecompressMozLz4(file_path);
  ASSERT_TRUE(decompressed.has_value());
  EXPECT_EQ(*decompressed, original);
}

}  // namespace
}  // namespace maho::importer
