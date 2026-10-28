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
  // {"type":"error","msg":"room_retired"}: the account moved to client-held
  // keys; this room is closed for good.
  kRoomRetired,
};

// Relay error code for a room retired by a client-held switch (WebSocket
// error frame / close reason, and HTTP 410 {"code":"room_retired"}).
inline constexpr char kMahoSyncRoomRetiredCode[] = "room_retired";

MahoSyncRelayFrameKind ClassifyMahoSyncRelayFrame(std::string_view frame);

// True for an HTTP push/pull/snapshot reply that means the room is retired:
// status 410 with a JSON body whose "code" (or "error") is "room_retired".
bool IsMahoSyncRoomRetiredResponse(int http_status, std::string_view body);

}  // namespace maho

#endif  // MAHO_BROWSER_SYNC_MAHO_SYNC_RELAY_PROTOCOL_H_
