// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_LINK_PREVIEW_MAHO_LINK_PREVIEW_MANAGER_H_
#define MAHO_BROWSER_UI_LINK_PREVIEW_MAHO_LINK_PREVIEW_MANAGER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "maho/browser/maho_private_context_policy.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget_observer.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace views {
class Widget;
}  // namespace views

class PrefService;
class Profile;

class MahoLinkPreviewManager
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoLinkPreviewManager>,
      public views::WidgetObserver {
 public:
  ~MahoLinkPreviewManager() override;

  MahoLinkPreviewManager(const MahoLinkPreviewManager&) = delete;
  MahoLinkPreviewManager& operator=(const MahoLinkPreviewManager&) = delete;

  // Test-only accessors.  Keep minimal — do not extend beyond what the
  // interactive UI tests strictly require.
  views::Widget* bubble_widget_for_testing() const { return bubble_widget_; }
  void HandleLinkHoverForTesting(const std::string& url,
                                 const std::string& text,
                                 const gfx::Rect& rect) {
    HandleLinkHover(url, text, rect);
  }
  void HandleLinkDismissForTesting() { HandleLinkDismiss(); }

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void OnDidAddMessageToConsole(
      content::RenderFrameHost* source_frame,
      blink::mojom::ConsoleMessageLevel log_level,
      const std::u16string& message,
      int32_t line_no,
      const std::u16string& source_id,
      const std::optional<std::u16string>& untrusted_stack_trace) override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  friend class content::WebContentsUserData<MahoLinkPreviewManager>;
  explicit MahoLinkPreviewManager(content::WebContents* web_contents);

  void InjectHoverScript();
  void HandleLinkHover(const std::string& url,
                       const std::string& text,
                       const gfx::Rect& rect);
  void HandleLinkDismiss();

  static std::string HandleEventOnBackground(const std::string& event_json);
  void OnRustEventResponse(uint64_t generation,
                           const std::string& rust_response_json);
  void MakeLlmRequest(uint64_t generation,
                      const std::string& body_json,
                      const std::string& api_key);
  void OnLlmResponse(uint64_t generation,
                     std::optional<std::string> response_body);
  void ShowBubble(const std::u16string& text, const gfx::Rect& anchor_rect);
  void CloseBubble();

  // R-9: revalidates the kAI token bound to this manager's observed
  // WebContents + owning browser. Rebuilt on each hover and rechecked before
  // every core/network/bubble step so OTR previews never touch core/network.
  bool IsAiAllowed();

  std::unique_ptr<MahoPrivateContextToken> token_;
  raw_ptr<PrefService> prefs_ = nullptr;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<network::SimpleURLLoader> url_loader_;
  raw_ptr<views::Widget> bubble_widget_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      bubble_widget_observation_{this};
  gfx::Rect pending_anchor_rect_;
  uint64_t generation_ = 0;
  uint64_t pending_generation_ = 0;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoLinkPreviewManager> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // MAHO_BROWSER_UI_LINK_PREVIEW_MAHO_LINK_PREVIEW_MANAGER_H_
