// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_ACP_EVENT_DECODER_H_
#define MAHO_BROWSER_AI_MAHO_ACP_EVENT_DECODER_H_

#include <optional>

#include "base/containers/span.h"
#include "base/values.h"

// Decodes the length-prefixed JSON event blob produced by the maho-ffi
// ACP event callback. Wire format: 4-byte little-endian u32 length prefix
// followed by exactly `length` bytes of UTF-8 JSON payload.
//
// The JSON payload is expected to be a dict with at least a "kind" key.
class MahoAcpEventDecoder {
 public:
  // Attempts to decode a single event blob. Returns nullopt on any
  // malformation: truncated length prefix, length exceeding buffer,
  // invalid JSON, or non-dict JSON root.
  static std::optional<base::DictValue> Decode(
      base::span<const uint8_t> blob);

 private:
  MahoAcpEventDecoder() = delete;
};

#endif  // MAHO_BROWSER_AI_MAHO_ACP_EVENT_DECODER_H_
