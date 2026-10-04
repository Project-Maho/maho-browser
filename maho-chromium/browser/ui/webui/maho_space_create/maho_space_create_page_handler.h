// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CREATE_MAHO_SPACE_CREATE_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CREATE_MAHO_SPACE_CREATE_PAGE_HANDLER_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/ui/webui/maho_space_create/maho_space_create.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Browser;

namespace content {
class WebContents;
}

class MahoSpaceCreatePageHandler
    : public maho_space_create::mojom::PageHandler {
 public:
  using CommitCallback = base::OnceCallback<void(const std::string& theme_json)>;
  using CancelCallback = base::OnceClosure;

  MahoSpaceCreatePageHandler(
      mojo::PendingReceiver<maho_space_create::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_space_create::mojom::Page> page,
      Browser* browser,
      content::WebContents* web_contents);
  MahoSpaceCreatePageHandler(const MahoSpaceCreatePageHandler&) = delete;
  MahoSpaceCreatePageHandler& operator=(const MahoSpaceCreatePageHandler&) =
      delete;
  ~MahoSpaceCreatePageHandler() override;

  // Must be called immediately after CreatePageHandler returns.
  // The dialog wrapper uses this to receive commit/cancel signals.
  void SetDialogCallbacks(CommitCallback on_commit, CancelCallback on_cancel);

 private:
  void PreviewTheme(
      maho_space_create::mojom::ThemeSelectionPtr selection) override;
  void CommitTheme(
      maho_space_create::mojom::ThemeSelectionPtr selection) override;
  void CancelTheme() override;

  mojo::Receiver<maho_space_create::mojom::PageHandler> receiver_;
  mojo::Remote<maho_space_create::mojom::Page> page_;
  raw_ptr<Browser> browser_;
  raw_ptr<content::WebContents> web_contents_;
  CommitCallback commit_callback_;
  CancelCallback cancel_callback_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoSpaceCreatePageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CREATE_MAHO_SPACE_CREATE_PAGE_HANDLER_H_
