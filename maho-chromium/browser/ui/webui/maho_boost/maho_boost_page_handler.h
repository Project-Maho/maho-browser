// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_BOOST_MAHO_BOOST_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_BOOST_MAHO_BOOST_PAGE_HANDLER_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class BrowserWindowInterface;

#include "maho/browser/maho_private_context_policy.h"

namespace content {
class WebContents;
}

namespace maho {
class MahoBoostWindowController;
}  // namespace maho

class MahoBoostPageHandler : public maho_boost::mojom::PageHandler {
 public:
  MahoBoostPageHandler(
      mojo::PendingReceiver<maho_boost::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_boost::mojom::PageObserver> page,
      const std::string& domain,
      base::WeakPtr<maho::MahoBoostWindowController> controller,
      BrowserWindowInterface* browser_window,
      content::WebContents* host_web_contents,
      content::WebContents* target_web_contents);
  MahoBoostPageHandler(const MahoBoostPageHandler&) = delete;
  MahoBoostPageHandler& operator=(const MahoBoostPageHandler&) = delete;
  ~MahoBoostPageHandler() override;

  void FireEditorKilled();

  static bool IsBoostAuthorizedForDomainForTesting(
      const std::string& domain,
      const std::string& boost_id);

  base::WeakPtr<MahoBoostPageHandler> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  void GetDomain(GetDomainCallback callback) override;
  void ListBoosts(ListBoostsCallback callback) override;
  void GetBoost(const std::string& boost_id,
                GetBoostCallback callback) override;
  void GetActiveBoost(GetActiveBoostCallback callback) override;

  void CreateTempBoost(CreateTempBoostCallback callback) override;
  void CommitBoost(const std::string& boost_id,
                   CommitBoostCallback callback) override;
  void DiscardBoost(const std::string& boost_id,
                    DiscardBoostCallback callback) override;

  void UpdateBoost(const std::string& boost_id,
                   maho_boost::mojom::BoostUpdatePtr changes,
                   UpdateBoostCallback callback) override;
  void DeleteBoost(const std::string& boost_id,
                   DeleteBoostCallback callback) override;
  void SetActiveBoost(const std::optional<std::string>& boost_id,
                      SetActiveBoostCallback callback) override;
  void ComposeCss(const std::string& boost_id,
                  ComposeCssCallback callback) override;

  void ShuffleBoost(const std::string& boost_id,
                    ShuffleBoostCallback callback) override;
  void ResetBoost(const std::string& boost_id,
                  ResetBoostCallback callback) override;
  void ExportBoost(const std::string& boost_id,
                   ExportBoostCallback callback) override;
  void ImportBoost(const std::string& json,
                   ImportBoostCallback callback) override;
  void CloseDialog(CloseDialogCallback callback) override;
  void HostCloseFinished(HostCloseFinishedCallback callback) override;
  void SetHostCloseState(const std::optional<std::string>& boost_id,
                         bool dirty,
                         SetHostCloseStateCallback callback) override;
  void RequestModeResize(maho_boost::mojom::WindowMode mode,
                         RequestModeResizeCallback callback) override;

  void EnterZapMode(const std::string& boost_id,
                    EnterZapModeCallback callback) override;
  void ExitZapMode(ExitZapModeCallback callback) override;
  void EnterPickerMode(const std::string& boost_id,
                       EnterPickerModeCallback callback) override;
  void ExitPickerMode(ExitPickerModeCallback callback) override;
  void AppendZapSelector(const std::string& boost_id,
                         const std::string& selector,
                         AppendZapSelectorCallback callback) override;
  void RemoveZapSelector(const std::string& boost_id,
                         const std::string& selector,
                         RemoveZapSelectorCallback callback) override;
  void OpenInspector(OpenInspectorCallback callback) override;
  void GetSystemFonts(GetSystemFontsCallback callback) override;

  void OnCreateTempBoostDone(CreateTempBoostCallback callback,
                             maho_boost::mojom::BoostInfoPtr result);
  void OnUpdateBoostDone(UpdateBoostCallback callback,
                         maho_boost::mojom::BoostInfoPtr result);
  void OnDeleteBoostDone(DeleteBoostCallback callback, bool success);
  void OnSetActiveBoostDone(std::optional<std::string> boost_id,
                             SetActiveBoostCallback callback,
                             std::pair<std::string, bool> result);
  void OnDiscardBoostDone(DiscardBoostCallback callback,
                           std::string prev_active_id);
  void OnShuffleBoostDone(ShuffleBoostCallback callback,
                           maho_boost::mojom::BoostInfoPtr result);
  void OnResetBoostDone(ResetBoostCallback callback,
                         maho_boost::mojom::BoostInfoPtr result);
  void OnImportBoostDone(ImportBoostCallback callback,
                          maho_boost::mojom::BoostInfoPtr result);
  void OnAppendZapDone(AppendZapSelectorCallback callback,
                        maho_boost::mojom::BoostInfoPtr result);
  void OnRemoveZapDone(RemoveZapSelectorCallback callback,
                        maho_boost::mojom::BoostInfoPtr result);
  void OnGetFontListDone(GetSystemFontsCallback callback,
                          base::ListValue fonts);

  void OnEnterZapModeDone(EnterZapModeCallback callback,
                           base::WeakPtr<content::WebContents> captured_wc,
                           std::vector<std::string> zap_selectors);

  void OnContentScriptEvent(const std::string& type,
                             const std::string& selector,
                             const std::string& msg);

  content::WebContents* GetTargetTab();
  void CleanupTargetEditMode();
  void OnPipeDisconnected();

  mojo::Receiver<maho_boost::mojom::PageHandler> receiver_;
  mojo::Remote<maho_boost::mojom::PageObserver> page_;
  std::string domain_;
  base::WeakPtr<maho::MahoBoostWindowController> controller_;

  std::string current_zap_boost_id_;
  std::string current_picker_boost_id_;
  bool awaiting_zap_start_notification_ = false;

  MahoPrivateContextToken context_token_;
  base::WeakPtr<content::WebContents> host_web_contents_;
  base::WeakPtr<content::WebContents> target_web_contents_;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoBoostPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_BOOST_MAHO_BOOST_PAGE_HANDLER_H_
