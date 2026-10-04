// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_attachment_launcher.h"

#import <AppKit/AppKit.h>

#include <sys/stat.h>

#include <memory>
#include <utility>

#include "base/apple/foundation_util.h"
#include "base/functional/bind.h"

namespace maho::mail {
namespace {

struct LaunchState {
  LaunchState(
      base::File launch_file,
      std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment>
          attachment,
      AttachmentLaunchCallback callback)
      : launch_file(std::move(launch_file)),
        attachment(std::move(attachment)),
        callback(std::move(callback)) {}

  base::File launch_file;
  std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment> attachment;
  AttachmentLaunchCallback callback;
};

void FinishLaunch(std::shared_ptr<LaunchState> state, bool opened) {
  MahoMailAttachmentRegistry::DeleteConsumedAttachment(
      std::move(state->attachment),
      base::BindOnce(
          [](AttachmentLaunchCallback callback, bool opened, bool deleted) {
            std::move(callback).Run(
                opened && deleted,
                opened ? (deleted ? std::string()
                                  : "attachment cleanup failed")
                       : "attachment open failed");
          },
          std::move(state->callback), opened));
}

bool ReferenceMatchesLaunchFile(NSURL* reference_url,
                                const base::File& launch_file) {
  struct stat descriptor_stat = {};
  struct stat reference_stat = {};
  return fstat(launch_file.GetPlatformFile(), &descriptor_stat) == 0 &&
         stat(reference_url.fileSystemRepresentation, &reference_stat) == 0 &&
         descriptor_stat.st_dev == reference_stat.st_dev &&
         descriptor_stat.st_ino == reference_stat.st_ino;
}

}  // namespace

void LaunchConsumedAttachment(
    std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment> attachment,
    AttachmentLaunchCallback callback) {
  base::File launch_file = attachment->DuplicateProtectionFile();
  if (!launch_file.IsValid()) {
    auto state = std::make_shared<LaunchState>(
        std::move(launch_file), std::move(attachment), std::move(callback));
    FinishLaunch(std::move(state), false);
    return;
  }

  NSURL* path_url = base::apple::FilePathToNSURL(attachment->path());
  NSURL* reference_url = [path_url fileReferenceURL];
  if (!reference_url ||
      !ReferenceMatchesLaunchFile(reference_url, launch_file)) {
    auto state = std::make_shared<LaunchState>(
        std::move(launch_file), std::move(attachment), std::move(callback));
    FinishLaunch(std::move(state), false);
    return;
  }

  // A file-reference URL is bound to the inode represented by |launch_file|;
  // replacing the directory entry after this point cannot redirect the open.
  NSWorkspaceOpenConfiguration* configuration =
      [NSWorkspaceOpenConfiguration configuration];
  auto state = std::make_shared<LaunchState>(
      std::move(launch_file), std::move(attachment), std::move(callback));
  [[NSWorkspace sharedWorkspace]
      openURL:reference_url
      configuration:configuration
      completionHandler:^(NSRunningApplication* application, NSError* error) {
        FinishLaunch(state, error == nil);
      }];
}

}  // namespace maho::mail
