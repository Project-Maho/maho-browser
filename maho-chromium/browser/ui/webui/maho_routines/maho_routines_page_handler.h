// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_PAGE_HANDLER_H_

#include "maho/browser/ui/webui/maho_routines/maho_routines.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class MahoRoutinesPageHandler : public maho_routines::mojom::PageHandler {
 public:
  MahoRoutinesPageHandler(
      mojo::PendingReceiver<maho_routines::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_routines::mojom::Page> page);
  MahoRoutinesPageHandler(const MahoRoutinesPageHandler&) = delete;
  MahoRoutinesPageHandler& operator=(const MahoRoutinesPageHandler&) = delete;
  ~MahoRoutinesPageHandler() override;

  void ListRoutines(ListRoutinesCallback callback) override;
  void RunRoutine(const std::string& id, RunRoutineCallback callback) override;
  void GetUserTier(GetUserTierCallback callback) override;

 private:
  mojo::Receiver<maho_routines::mojom::PageHandler> receiver_;
  mojo::Remote<maho_routines::mojom::Page> page_;
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_PAGE_HANDLER_H_
