// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_NOTES_MAHO_NOTES_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_NOTES_MAHO_NOTES_PAGE_HANDLER_H_

#include <string>

#include "maho/browser/ui/webui/maho_notes/maho_notes.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class MahoNotesPageHandler : public maho_notes::mojom::PageHandler {
 public:
  MahoNotesPageHandler(
      mojo::PendingReceiver<maho_notes::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_notes::mojom::Page> page);
  MahoNotesPageHandler(const MahoNotesPageHandler&) = delete;
  MahoNotesPageHandler& operator=(const MahoNotesPageHandler&) = delete;
  ~MahoNotesPageHandler() override;

  void GetNotes(GetNotesCallback callback) override;
  void CreateNote(const std::string& title,
                  const std::string& content,
                  const std::string& url,
                  CreateNoteCallback callback) override;
  void UpdateNote(const std::string& id,
                  const std::string& title,
                  const std::string& content,
                  UpdateNoteCallback callback) override;
  void DeleteNote(const std::string& id,
                  DeleteNoteCallback callback) override;

 private:
  mojo::Receiver<maho_notes::mojom::PageHandler> receiver_;
  mojo::Remote<maho_notes::mojom::Page> page_;
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_NOTES_MAHO_NOTES_PAGE_HANDLER_H_
