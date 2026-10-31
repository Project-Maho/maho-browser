// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_ad_block_tab_helper.h"
#include "maho/browser/net/maho_ad_block_request_util.h"

#include <memory>

#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "services/network/public/cpp/resource_request.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

TEST(MahoAdBlockRequestUtilTest, CurrentWorkerDestinationsRemainScripts) {
  for (const auto destination : {
           network::mojom::RequestDestination::kScript,
           network::mojom::RequestDestination::kWorker,
           network::mojom::RequestDestination::kSharedWorker,
           network::mojom::RequestDestination::kServiceWorker,
           network::mojom::RequestDestination::kAudioWorklet,
           network::mojom::RequestDestination::kPaintWorklet}) {
    SCOPED_TRACE(static_cast<int>(destination));
    EXPECT_STREQ("script", maho::MapRequestDestinationToFilterType(destination));
  }
}

TEST(MahoAdBlockRequestUtilTest, FetchLikeOverridesDestinationAndKeepalive) {
  network::ResourceRequest request;
  request.destination = network::mojom::RequestDestination::kEmpty;
  EXPECT_STREQ("other", maho::MapRequestToFilterType(request));
  request.keepalive = true;
  EXPECT_STREQ("ping", maho::MapRequestToFilterType(request));
  request.is_fetch_like_api = true;
  EXPECT_STREQ("xmlhttprequest", maho::MapRequestToFilterType(request));
  request.destination = network::mojom::RequestDestination::kScript;
  EXPECT_STREQ("xmlhttprequest", maho::MapRequestToFilterType(request));
  request.is_fetch_like_api = false;
  EXPECT_STREQ("script", maho::MapRequestToFilterType(request));
}

MahoBlockedRequestInfo MakeBlockedRequestInfo(
    content::GlobalRenderFrameHostId page_id,
    network::mojom::RequestDestination destination,
    bool is_outermost_main_frame) {
  MahoBlockedRequestInfo info;
  info.page_id = page_id;
  info.destination = destination;
  info.is_outermost_main_frame = is_outermost_main_frame;
  info.request_type = "document";
  return info;
}

content::GlobalRenderFrameHostId MakeDistinctPageId(
    content::GlobalRenderFrameHostId current_page_id) {
  return content::GlobalRenderFrameHostId(
      current_page_id.child_id, current_page_id.frame_routing_id + 1);
}

class MahoAdBlockTabHelperTest : public content::RenderViewHostTestHarness {
 protected:
  std::unique_ptr<content::BrowserContext> CreateBrowserContext() override {
    return TestingProfile::Builder().Build();
  }

  MahoAdBlockTabHelper* CreateHelperAndNavigate(const GURL& url) {
    MahoAdBlockTabHelper::CreateForWebContents(web_contents());
    content::WebContentsTester::For(web_contents())->NavigateAndCommit(url);
    return MahoAdBlockTabHelper::FromWebContents(web_contents());
  }

  content::GlobalRenderFrameHostId CurrentPageId() {
    return web_contents()->GetPrimaryPage().GetMainDocument().GetGlobalId();
  }
};

TEST_F(MahoAdBlockTabHelperTest, RecordBlockedRequestCountsMainFrameDocument) {
  MahoAdBlockTabHelper* helper =
      CreateHelperAndNavigate(GURL("https://a.example/"));
  ASSERT_NE(helper, nullptr);
  const content::GlobalRenderFrameHostId page_id = CurrentPageId();

  // RED intent: the old IncrementBlockedCount() API has no request destination,
  // main-frame-context, or page identity, so it cannot enforce G1 filtering.
  EXPECT_EQ(helper->blocked_count(), 0u);
  helper->RecordBlockedRequest(MakeBlockedRequestInfo(
      page_id, network::mojom::RequestDestination::kDocument,
      /*is_outermost_main_frame=*/true));

  EXPECT_EQ(helper->blocked_count(), 1u);
}

TEST_F(MahoAdBlockTabHelperTest,
       RecordBlockedRequestIgnoresNonMainFrameDocumentContexts) {
  MahoAdBlockTabHelper* helper =
      CreateHelperAndNavigate(GURL("https://a.example/"));
  ASSERT_NE(helper, nullptr);
  const content::GlobalRenderFrameHostId page_id = CurrentPageId();

  // RED intent: worker, service-worker, frame, script, empty, and non-outermost
  // document records all arrive on the block path but must not affect the page
  // shield count.
  const network::mojom::RequestDestination ignored_destinations[] = {
      network::mojom::RequestDestination::kWorker,
      network::mojom::RequestDestination::kSharedWorker,
      network::mojom::RequestDestination::kServiceWorker,
      network::mojom::RequestDestination::kIframe,
      network::mojom::RequestDestination::kFrame,
      network::mojom::RequestDestination::kScript,
      network::mojom::RequestDestination::kEmpty,
  };
  for (const auto destination : ignored_destinations) {
    SCOPED_TRACE(testing::Message()
                 << "destination=" << static_cast<int>(destination));
    helper->RecordBlockedRequest(
        MakeBlockedRequestInfo(page_id, destination,
                               /*is_outermost_main_frame=*/true));
    EXPECT_EQ(helper->blocked_count(), 0u);
  }

  helper->RecordBlockedRequest(MakeBlockedRequestInfo(
      page_id, network::mojom::RequestDestination::kDocument,
      /*is_outermost_main_frame=*/false));

  EXPECT_EQ(helper->blocked_count(), 0u);
}

TEST_F(MahoAdBlockTabHelperTest,
       RecordBlockedRequestIgnoresStalePageIdentityAndResetsOnPrimaryPageChanged) {
  MahoAdBlockTabHelper* helper =
      CreateHelperAndNavigate(GURL("https://a.example/"));
  ASSERT_NE(helper, nullptr);
  const content::GlobalRenderFrameHostId page_a_id = CurrentPageId();

  helper->RecordBlockedRequest(MakeBlockedRequestInfo(
      page_a_id, network::mojom::RequestDestination::kDocument,
      /*is_outermost_main_frame=*/true));
  ASSERT_EQ(helper->blocked_count(), 1u);

  content::WebContentsTester::For(web_contents())
      ->NavigateAndCommit(GURL("https://b.example/"));
  const content::GlobalRenderFrameHostId page_b_id = CurrentPageId();
  content::GlobalRenderFrameHostId stale_page_id = page_a_id;
  if (stale_page_id == page_b_id) {
    // Some lightweight RFH tests can reuse the same RenderFrameHost across
    // synthetic commits; use an explicit non-current id to exercise the stale
    // callback protection without depending on process-swap policy.
    stale_page_id = MakeDistinctPageId(page_b_id);
  }

  // RED intent: a new primary page resets the visible count, and late callbacks
  // from the previous page must not pollute the new page's shield state.
  EXPECT_EQ(helper->blocked_count(), 0u);
  helper->RecordBlockedRequest(MakeBlockedRequestInfo(
      stale_page_id, network::mojom::RequestDestination::kDocument,
      /*is_outermost_main_frame=*/true));

  EXPECT_EQ(helper->blocked_count(), 0u);
}

}  // namespace
