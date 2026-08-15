// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_SYNC_MAHO_SYNC_RELAY_PROTOCOL_H_
#define MAHO_BROWSER_SYNC_MAHO_SYNC_RELAY_PROTOCOL_H_

#include <string_view>

namespace maho {

enum class MahoSyncRelayFrameKind {
  kInvalid,
  kEnvelope,
  kAuthOk,
  kAck,
  kCatchupDone,
};

MahoSyncRelayFrameKind ClassifyMahoSyncRelayFrame(std::string_view frame);

}  // namespace maho

#endif  // MAHO_BROWSER_SYNC_MAHO_SYNC_RELAY_PROTOCOL_H_
