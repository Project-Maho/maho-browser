// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_notes/maho_notes_page_handler.h"

#include <utility>
#include <vector>

MahoNotesPageHandler::MahoNotesPageHandler(
    mojo::PendingReceiver<maho_notes::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_notes::mojom::Page> page)
    : receiver_(this, std::move(receiver)), page_(std::move(page)) {}

MahoNotesPageHandler::~MahoNotesPageHandler() = default;

void MahoNotesPageHandler::GetNotes(GetNotesCallback callback) {
  std::vector<maho_notes::mojom::NotePtr> notes;

  auto n1 = maho_notes::mojom::Note::New();
  n1->id = "1";
  n1->title = "Welcome to Maho Notes";
  n1->content = "Use this space to capture thoughts while browsing.";
  n1->url = "chrome://maho-notes/";
  n1->created_at = 1700000000.0;
  n1->updated_at = 1700000000.0;
  notes.push_back(std::move(n1));

  auto n2 = maho_notes::mojom::Note::New();
  n2->id = "2";
  n2->title = "Research: Web Standards";
  n2->content = "Interesting findings about the latest CSS features.";
  n2->url = "https://developer.mozilla.org/";
  n2->created_at = 1700100000.0;
  n2->updated_at = 1700200000.0;
  notes.push_back(std::move(n2));

  std::move(callback).Run(std::move(notes));
}

void MahoNotesPageHandler::CreateNote(const std::string& title,
                                      const std::string& content,
                                      const std::string& url,
                                      CreateNoteCallback callback) {
  auto note = maho_notes::mojom::Note::New();
  note->id = "new-1";
  note->title = title;
  note->content = content;
  note->url = url;
  note->created_at = 1700300000.0;
  note->updated_at = 1700300000.0;
  std::move(callback).Run(std::move(note));
}

void MahoNotesPageHandler::UpdateNote(const std::string& id,
                                      const std::string& title,
                                      const std::string& content,
                                      UpdateNoteCallback callback) {
  std::move(callback).Run(true);
}

void MahoNotesPageHandler::DeleteNote(const std::string& id,
                                      DeleteNoteCallback callback) {
  std::move(callback).Run(true);
}
