// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_ATTACHMENT_LAUNCHER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_ATTACHMENT_LAUNCHER_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "maho/browser/mail_helper/maho_mail_attachment_registry.h"

namespace maho::mail {

using AttachmentLaunchCallback = base::OnceCallback<void(bool, std::string)>;

// Opens the validated attachment with the platform's default application.
// The implementation owns |attachment| through the platform handoff. On a
// failed open it deletes the launch copy before replying; on success it leaves
// the copy for the viewer and the registry's stale-copy sweep deletes it.
void LaunchConsumedAttachment(
    std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment> attachment,
    AttachmentLaunchCallback callback);

}  // namespace maho::mail

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_ATTACHMENT_LAUNCHER_H_
