// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_BACKEND_BOUNDARY_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_BACKEND_BOUNDARY_H_

#include <string>

#include "base/functional/callback.h"

namespace maho::mail {

using GenericBackendReply = base::OnceCallback<void(bool, std::string)>;
using GenericBackendInvoker =
    base::OnceCallback<void(const std::string&,
                            const std::string&,
                            GenericBackendReply)>;
using PgpExportInvoker =
    base::OnceCallback<void(const std::string&, bool, GenericBackendReply)>;

// Applies renderer-facing authorization and argument constraints before a
// generic backend command can reach the Mail service/helper boundary.
void DispatchGenericBackendCall(const std::string& command,
                                const std::string& args_json,
                                GenericBackendReply reply,
                                GenericBackendInvoker invoker);

// The renderer-facing typed export is public-only by construction.
void DispatchPublicPgpExport(const std::string& key_id,
                             GenericBackendReply reply,
                             PgpExportInvoker invoker);

}  // namespace maho::mail

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_BACKEND_BOUNDARY_H_
