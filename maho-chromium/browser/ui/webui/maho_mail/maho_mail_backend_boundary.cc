// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_backend_boundary.h"

#include <utility>

namespace maho::mail {
namespace {

constexpr char kPrivatePgpExportDenial[] =
    R"({"error":{"code":"private_pgp_export_denied","message":"Private PGP key export is not available through CallBackend"}})";

}  // namespace

void DispatchGenericBackendCall(const std::string& command,
                                const std::string& args_json,
                                GenericBackendReply reply,
                                GenericBackendInvoker invoker) {
  if (command == "ExportPgpKey") {
    std::move(reply).Run(false, kPrivatePgpExportDenial);
    return;
  }

  std::move(invoker).Run(command, args_json, std::move(reply));
}

void DispatchPublicPgpExport(const std::string& key_id,
                             GenericBackendReply reply,
                             PgpExportInvoker invoker) {
  std::move(invoker).Run(key_id, false, std::move(reply));
}

}  // namespace maho::mail
