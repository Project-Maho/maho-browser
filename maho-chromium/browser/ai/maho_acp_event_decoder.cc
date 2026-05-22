// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_acp_event_decoder.h"

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/numerics/byte_conversions.h"

// static
std::optional<base::DictValue> MahoAcpEventDecoder::Decode(
    base::span<const uint8_t> blob) {
  // Need at least 4 bytes for the length prefix.
  if (blob.size() < 4) {
    LOG(WARNING) << "MahoAcpEventDecoder: blob too short for length prefix ("
                 << blob.size() << " bytes)";
    return std::nullopt;
  }

  // Read 4-byte little-endian u32 length.
  uint32_t json_len = base::U32FromLittleEndian(blob.first<4>());

  // Validate that the declared length fits within the remaining buffer.
  auto payload = blob.subspan(4u);
  if (static_cast<size_t>(json_len) > payload.size()) {
    LOG(WARNING) << "MahoAcpEventDecoder: declared length " << json_len
                 << " exceeds available data " << payload.size();
    return std::nullopt;
  }

  // Extract the JSON payload.
  auto json_bytes = payload.first(static_cast<size_t>(json_len));
  std::string_view json_str(
      reinterpret_cast<const char*>(json_bytes.data()),
      json_bytes.size());

  auto parsed = base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    LOG(WARNING) << "MahoAcpEventDecoder: failed to parse JSON payload";
    return std::nullopt;
  }

  return std::move(parsed->GetDict());
}
