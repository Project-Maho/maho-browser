// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/importer/mozlz4_decompressor.h"

#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/logging.h"

namespace maho::importer {

namespace {

constexpr uint8_t kMozLz4Magic[] = {'m', 'o', 'z', 'L', 'z', '4', '0', '\0'};
constexpr size_t kMagicSize = 8;
constexpr size_t kHeaderSize = kMagicSize + 4;

// LZ4 block decompression. Returns false on malformed input.
bool Lz4BlockDecompress(const uint8_t* src,
                        size_t src_size,
                        uint8_t* dst,
                        size_t dst_capacity,
                        size_t* out_size) {
  size_t si = 0;
  size_t di = 0;

  while (si < src_size) {
    uint8_t token = src[si++];
    size_t literal_len = token >> 4;

    if (literal_len == 15) {
      while (si < src_size && src[si] == 255) {
        literal_len += 255;
        si++;
      }
      if (si >= src_size) return false;
      literal_len += src[si++];
    }

    if (si + literal_len > src_size || di + literal_len > dst_capacity) {
      return false;
    }
    memcpy(dst + di, src + si, literal_len);
    si += literal_len;
    di += literal_len;

    if (si >= src_size) break;

    if (si + 2 > src_size) return false;
    size_t offset = static_cast<size_t>(src[si]) |
                    (static_cast<size_t>(src[si + 1]) << 8);
    si += 2;
    if (offset == 0 || offset > di) return false;

    size_t match_len = (token & 0x0F) + 4;
    if (match_len == 19) {
      while (si < src_size && src[si] == 255) {
        match_len += 255;
        si++;
      }
      if (si >= src_size) return false;
      match_len += src[si++];
    }

    if (di + match_len > dst_capacity) return false;

    size_t match_pos = di - offset;
    for (size_t i = 0; i < match_len; i++) {
      dst[di++] = dst[match_pos + i];
    }
  }

  *out_size = di;
  return true;
}

}  // namespace

std::optional<std::string> DecompressMozLz4FromData(
    const std::vector<uint8_t>& data) {
  if (data.size() < kHeaderSize) {
    return std::nullopt;
  }

  if (memcmp(data.data(), kMozLz4Magic, kMagicSize) != 0) {
    return std::nullopt;
  }

  uint32_t uncompressed_size =
      static_cast<uint32_t>(data[kMagicSize]) |
      (static_cast<uint32_t>(data[kMagicSize + 1]) << 8) |
      (static_cast<uint32_t>(data[kMagicSize + 2]) << 16) |
      (static_cast<uint32_t>(data[kMagicSize + 3]) << 24);

  constexpr uint32_t kMaxDecompressedSize = 256 * 1024 * 1024;
  if (uncompressed_size > kMaxDecompressedSize) {
    return std::nullopt;
  }

  std::vector<uint8_t> output(uncompressed_size);
  size_t actual_size = 0;

  if (!Lz4BlockDecompress(data.data() + kHeaderSize,
                          data.size() - kHeaderSize, output.data(),
                          uncompressed_size, &actual_size)) {
    return std::nullopt;
  }

  return std::string(reinterpret_cast<const char*>(output.data()), actual_size);
}

std::optional<std::string> DecompressMozLz4(const base::FilePath& path) {
  std::string raw;
  if (!base::ReadFileToString(path, &raw)) {
    return std::nullopt;
  }

  std::vector<uint8_t> data(raw.begin(), raw.end());
  return DecompressMozLz4FromData(data);
}

}  // namespace maho::importer
