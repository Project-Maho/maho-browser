// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_attachment_launcher.h"

#include "base/functional/bind.h"
#include "chrome/browser/platform_util.h"

namespace maho::mail {
namespace {

void FinishLaunch(
    std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment> attachment,
    AttachmentLaunchCallback callback,
    platform_util::OpenOperationResult result) {
  const bool opened = result == platform_util::OPEN_SUCCEEDED;
  MahoMailAttachmentRegistry::DeleteConsumedAttachment(
      std::move(attachment),
      base::BindOnce(
          [](AttachmentLaunchCallback callback, bool opened, bool deleted) {
            std::move(callback).Run(
                opened && deleted,
                opened ? (deleted ? std::string()
                                  : "attachment cleanup failed")
                       : "attachment open failed");
          },
          std::move(callback), opened));
}

}  // namespace

void LaunchConsumedAttachment(
    std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment> attachment,
    AttachmentLaunchCallback callback) {
  const base::FilePath path = attachment->path();
  platform_util::OpenItem(
      nullptr, path, platform_util::OPEN_FILE,
      base::BindOnce(&FinishLaunch, std::move(attachment),
                     std::move(callback)));
}

}  // namespace maho::mail
