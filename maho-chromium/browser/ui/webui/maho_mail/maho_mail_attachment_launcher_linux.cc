// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_attachment_launcher.h"

#include <utility>

#include "base/functional/bind.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"

namespace maho::mail {
namespace {

constexpr char kPortalService[] = "org.freedesktop.portal.Desktop";
constexpr char kPortalPath[] = "/org/freedesktop/portal/desktop";
constexpr char kPortalInterface[] = "org.freedesktop.portal.OpenURI";
constexpr char kPortalMethod[] = "OpenFile";

void DeleteAfterLaunch(
    std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment> attachment,
    AttachmentLaunchCallback callback,
    bool opened) {
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

void FinishLaunch(
    scoped_refptr<dbus::Bus> bus,
    base::File launch_file,
    std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment> attachment,
    AttachmentLaunchCallback callback,
    dbus::Response* response) {
  bus->ShutdownAndBlock();
  DeleteAfterLaunch(std::move(attachment), std::move(callback),
                    response != nullptr);
}

}  // namespace

void LaunchConsumedAttachment(
    std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment> attachment,
    AttachmentLaunchCallback callback) {
  base::File launch_file = attachment->DuplicateProtectionFile();
  if (!launch_file.IsValid()) {
    DeleteAfterLaunch(std::move(attachment), std::move(callback), false);
    return;
  }

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SESSION;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(std::move(options));
  dbus::ObjectProxy* portal =
      bus->GetObjectProxy(kPortalService, dbus::ObjectPath(kPortalPath));
  dbus::MethodCall method(kPortalInterface, kPortalMethod);
  dbus::MessageWriter writer(&method);
  writer.AppendString(std::string());
  writer.AppendFileDescriptor(launch_file.GetPlatformFile());
  dbus::MessageWriter dictionary(nullptr);
  writer.OpenArray("{sv}", &dictionary);
  writer.CloseContainer(&dictionary);

  // SCM_RIGHTS binds the portal request to this open file description. The
  // browser-owned pathname may be replaced after CallMethod begins without
  // changing the bytes received by the desktop portal.
  portal->CallMethod(
      &method, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::BindOnce(&FinishLaunch, bus, std::move(launch_file),
                     std::move(attachment), std::move(callback)));
}

}  // namespace maho::mail
