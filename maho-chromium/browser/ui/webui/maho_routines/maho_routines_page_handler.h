// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_PAGE_HANDLER_H_

#include <optional>
#include <string>

#include "maho/browser/ui/webui/maho_routines/maho_routines.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class BrowserWindowInterface;

#include "maho/browser/maho_private_context_policy.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"

class MahoRoutinesPageHandler : public maho_routines::mojom::PageHandler {
 public:
  MahoRoutinesPageHandler(
      mojo::PendingReceiver<maho_routines::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_routines::mojom::Page> page,
      BrowserWindowInterface* browser_window,
      content::WebContents* web_contents);
  MahoRoutinesPageHandler(const MahoRoutinesPageHandler&) = delete;
  MahoRoutinesPageHandler& operator=(const MahoRoutinesPageHandler&) = delete;
  ~MahoRoutinesPageHandler() override;

  void ListRoutines(ListRoutinesCallback callback) override;
  void ListAllRoutines(ListAllRoutinesCallback callback) override;
  void CreateRoutine(const std::string& name,
                     const std::string& prompt,
                     const std::optional<std::string>& schedule,
                     const std::optional<std::string>& trigger,
                     CreateRoutineCallback callback) override;
  void DeleteRoutine(const std::string& id,
                     DeleteRoutineCallback callback) override;
  void RunRoutine(const std::string& id, RunRoutineCallback callback) override;
  void GetUserTier(GetUserTierCallback callback) override;

  void CompleteRoutine(RunRoutineCallback callback, std::string json);
  void FailRoutine(RunRoutineCallback callback, std::string error);

 private:
  mojo::Receiver<maho_routines::mojom::PageHandler> receiver_;
  mojo::Remote<maho_routines::mojom::Page> page_;
  MahoPrivateContextToken context_token_;
  base::WeakPtr<content::WebContents> host_web_contents_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoRoutinesPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_PAGE_HANDLER_H_
