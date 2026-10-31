// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_address_bar_context_menu.h"

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/menus/simple_menu_model.h"

MahoAddressBarContextMenu::MahoAddressBarContextMenu(
    Browser* browser,
    base::RepeatingClosure on_edit_address)
    : browser_(browser), on_edit_address_(std::move(on_edit_address)) {}

MahoAddressBarContextMenu::~MahoAddressBarContextMenu() = default;

std::unique_ptr<ui::SimpleMenuModel>
MahoAddressBarContextMenu::BuildMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);

  model->AddItem(IDC_MAHO_ADDR_EDIT, u"Edit Address");
  model->AddItem(IDC_MAHO_ADDR_COPY_URL, u"Copy URL");

  model->AddSeparator(ui::NORMAL_SEPARATOR);

  model->AddItem(IDC_MAHO_ADDR_COPY_MARKDOWN, u"Copy as Markdown Link");

  return model;
}

void MahoAddressBarContextMenu::ExecuteCommand(int command_id,
                                                int event_flags) {
  switch (command_id) {
    case IDC_MAHO_ADDR_EDIT:
      if (on_edit_address_) {
        on_edit_address_.Run();
      } else {
        LOG(INFO) << "MahoAddressBarContextMenu: Edit Address";
      }
      break;
    case IDC_MAHO_ADDR_COPY_URL: {
      content::WebContents* contents =
          browser_->GetTabStripModel()->GetActiveWebContents();
      if (contents) {
        ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
        writer.WriteText(
            base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
      }
      break;
    }
    case IDC_MAHO_ADDR_COPY_MARKDOWN: {
      content::WebContents* contents =
          browser_->GetTabStripModel()->GetActiveWebContents();
      if (contents) {
        std::string url = contents->GetVisibleURL().spec();
        std::u16string title = contents->GetTitle();
        std::u16string markdown =
            u"[" + title + u"](" + base::UTF8ToUTF16(url) + u")";
        ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
        writer.WriteText(markdown);
      }
      break;
    }
    default:
      break;
  }
}

bool MahoAddressBarContextMenu::IsCommandIdEnabled(int command_id) const {
  switch (command_id) {
    case IDC_MAHO_ADDR_EDIT:
      return !on_edit_address_.is_null();
    case IDC_MAHO_ADDR_COPY_URL:
    case IDC_MAHO_ADDR_COPY_MARKDOWN:
      return browser_->GetTabStripModel()->GetActiveWebContents() != nullptr;
    default:
      return false;
  }
}

bool MahoAddressBarContextMenu::IsCommandIdChecked(int command_id) const {
  return false;
}
