// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/sync/maho_sync_relay_protocol.h"

#include <optional>

#include "base/json/json_reader.h"
#include "base/values.h"

namespace maho {

MahoSyncRelayFrameKind ClassifyMahoSyncRelayFrame(std::string_view frame) {
  std::optional<base::Value> value =
      base::JSONReader::Read(frame, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    return MahoSyncRelayFrameKind::kInvalid;
  }

  const base::DictValue& dict = value->GetDict();
  const std::string* type = dict.FindString("type");
  if (!type) {
    const std::optional<int> protocol_version =
        dict.FindInt("protocol_version");
    const std::optional<int> relay_seq = dict.FindInt("relay_seq");
    const std::string* delivery_id = dict.FindString("delivery_id");
    const std::string* payload = dict.FindString("payload");
    return protocol_version && *protocol_version == 2 && relay_seq &&
                   *relay_seq >= 0 && delivery_id && !delivery_id->empty() &&
                   payload && !payload->empty()
               ? MahoSyncRelayFrameKind::kEnvelope
               : MahoSyncRelayFrameKind::kInvalid;
  }

  if (*type == "auth_ok") {
    return MahoSyncRelayFrameKind::kAuthOk;
  }
  if (*type == "catchup_done") {
    return MahoSyncRelayFrameKind::kCatchupDone;
  }
  if (*type == "error") {
    const std::string* msg = dict.FindString("msg");
    return msg && *msg == kMahoSyncRoomRetiredCode
               ? MahoSyncRelayFrameKind::kRoomRetired
               : MahoSyncRelayFrameKind::kInvalid;
  }
  if (*type == "ack") {
    const std::string* delivery_id = dict.FindString("delivery_id");
    const std::optional<int> seq = dict.FindInt("seq");
    return delivery_id && !delivery_id->empty() && seq && *seq >= 0
               ? MahoSyncRelayFrameKind::kAck
               : MahoSyncRelayFrameKind::kInvalid;
  }
  return MahoSyncRelayFrameKind::kInvalid;
}

bool IsMahoSyncRoomRetiredResponse(int http_status, std::string_view body) {
  // Only a body that names room_retired counts: a bare 410 (proxy, other
  // route) must not stop Sync and demand the recovery phrase.
  if (http_status != 410) {
    return false;
  }
  std::optional<base::Value> value =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    return false;
  }
  const base::DictValue& dict = value->GetDict();
  const std::string* code = dict.FindString("code");
  if (!code) {
    code = dict.FindString("error");
  }
  return code && *code == kMahoSyncRoomRetiredCode;
}

}  // namespace maho
