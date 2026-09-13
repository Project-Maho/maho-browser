// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_attachment_launcher.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "components/dbus/xdg/request.h"
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
    bool opened,
    std::string open_error) {
  MahoMailAttachmentRegistry::DeleteConsumedAttachment(
      std::move(attachment),
      base::BindOnce(
          [](AttachmentLaunchCallback callback, bool opened,
             std::string open_error, bool deleted) {
            std::move(callback).Run(
                opened && deleted,
                opened ? (deleted ? std::string()
                                  : "attachment cleanup failed")
                       : std::move(open_error));
          },
          std::move(callback), opened, std::move(open_error)));
}

// Owns the xdg-desktop-portal request, the protection file, and the consumed
// attachment until the portal delivers a terminal response. The OpenFile
// method reply only carries a request handle; success, user cancellation, and
// portal failure all arrive later on the request object's Response signal, so
// completion must never be reported from the method reply.
class PortalOpenRequest {
 public:
  static void Start(
      std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment>
          attachment,
      AttachmentLaunchCallback callback) {
    base::File launch_file = attachment->DuplicateProtectionFile();
    if (!launch_file.IsValid()) {
      DeleteAfterLaunch(std::move(attachment), std::move(callback), false,
                        "attachment protection unavailable");
      return;
    }

    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SESSION;
    scoped_refptr<dbus::Bus> bus = new dbus::Bus(std::move(options));
    dbus::ObjectProxy* portal =
        bus->GetObjectProxy(kPortalService, dbus::ObjectPath(kPortalPath));

    auto* request = new PortalOpenRequest(
        bus, std::move(launch_file), std::move(attachment), std::move(callback));
    request->Send(portal);
  }

 private:
  PortalOpenRequest(
      scoped_refptr<dbus::Bus> bus,
      base::File launch_file,
      std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment>
          attachment,
      AttachmentLaunchCallback callback)
      : bus_(std::move(bus)),
        launch_file_(std::move(launch_file)),
        attachment_(std::move(attachment)),
        callback_(std::move(callback)) {}

  void Send(dbus::ObjectProxy* portal) {
    // Duplicate instead of borrowing: base::ScopedFD owns the fd it wraps.
    // Keeping `launch_file_` open until the terminal response also preserves
    // the write/delete protection across the SCM_RIGHTS handoff, since the
    // portal receives a dup of this open file description.
    base::File duplicate = launch_file_.Duplicate();
    if (!duplicate.IsValid()) {
      DeleteAfterLaunch(std::move(attachment_), std::move(callback_), false,
                        "attachment open failed");
      DeleteSelf();
      return;
    }
    base::ScopedFD fd(duplicate.TakePlatformFile());

    request_ = std::make_unique<dbus_xdg::Request>(
        bus_, portal, kPortalInterface, kPortalMethod, dbus_xdg::Dictionary(),
        base::BindOnce(&PortalOpenRequest::OnResponse, base::Unretained(this)),
        std::string(), std::move(fd));
  }

  void OnResponse(
      base::expected<dbus_xdg::Dictionary, dbus_xdg::ResponseError> results) {
    const bool opened = results.has_value();
    std::string open_error;
    if (!opened) {
      open_error =
          results.error() == dbus_xdg::ResponseError::kRequestCancelledByUser
              ? "attachment open cancelled"
              : "attachment open failed";
    }
    DeleteAfterLaunch(std::move(attachment_), std::move(callback_), opened,
                      std::move(open_error));
    DeleteSelf();
  }

  void DeleteSelf() {
    // Can run while request_'s own Finish() frame is still on the stack;
    // deferred destruction keeps the Request dtor out of its own callback.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([](PortalOpenRequest* self) { delete self; },
                                  base::Unretained(this)));
    bus_ = nullptr;
  }

  scoped_refptr<dbus::Bus> bus_;
  base::File launch_file_;
  std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment> attachment_;
  AttachmentLaunchCallback callback_;
  std::unique_ptr<dbus_xdg::Request> request_;
};

}  // namespace

void LaunchConsumedAttachment(
    std::unique_ptr<MahoMailAttachmentRegistry::ConsumedAttachment> attachment,
    AttachmentLaunchCallback callback) {
  PortalOpenRequest::Start(std::move(attachment), std::move(callback));
}

}  // namespace maho::mail
