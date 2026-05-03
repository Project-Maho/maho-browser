// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_COMMAND_MAHO_COMMAND_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_COMMAND_MAHO_COMMAND_PAGE_HANDLER_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/webui/maho_command/maho_command.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Browser;

class MahoCommandPageHandler : public maho_command::mojom::PageHandler {
 public:
  MahoCommandPageHandler(
      mojo::PendingReceiver<maho_command::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_command::mojom::Page> page,
      Browser* browser);

  MahoCommandPageHandler(const MahoCommandPageHandler&) = delete;
  MahoCommandPageHandler& operator=(const MahoCommandPageHandler&) = delete;

  ~MahoCommandPageHandler() override;

  // maho_command::mojom::PageHandler:
  void Query(const std::string& input, QueryCallback callback) override;
  void OpenSuggestion(int32_t index,
                      const std::string& url,
                      int32_t tab_index) override;
  void ExecuteAction(const std::string& action_id) override;

 private:
  std::string BuildTabsJson();

  static std::vector<maho_command::mojom::SuggestionPtr>
  SearchOnPool(const std::string& query, const std::string& tabs_json);

  void OnSearchResults(
      QueryCallback callback,
      std::vector<maho_command::mojom::SuggestionPtr> results);

  mojo::Receiver<maho_command::mojom::PageHandler> receiver_;
  mojo::Remote<maho_command::mojom::Page> page_;
  raw_ptr<Browser> browser_;

  base::WeakPtrFactory<MahoCommandPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_COMMAND_MAHO_COMMAND_PAGE_HANDLER_H_
