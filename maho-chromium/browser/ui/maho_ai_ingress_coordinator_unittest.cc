// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/maho_ai_ingress_coordinator.h"

#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/metrics/histogram_tester.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

class MahoAiIngressCoordinatorTest : public BrowserWithTestWindowTest {
 public:
  MahoAiIngressCoordinatorTest() = default;
  ~MahoAiIngressCoordinatorTest() override = default;

  void TearDown() override {
    MahoAiIngressCoordinator::RemoveFromBrowser(browser());
    BrowserWithTestWindowTest::TearDown();
  }

 protected:
  static maho_ai::mojom::AskMahoDispatchPtr MakeDispatch(
      const std::string& id,
      const std::string& query) {
    return maho_ai::mojom::AskMahoDispatch::New(
        id, query, maho_ai::mojom::AskMahoSource::kCommandPalette, true,
        maho_ai::mojom::InteractionMode::kAssistant,
        maho_ai::mojom::AskMahoContextIntent::kNone, std::nullopt);
  }

  struct TestConsumer {
    std::vector<maho_ai::mojom::AskMahoDispatch> dispatches;
    std::vector<uint64_t> delivery_ids;
    std::vector<MahoAiIngressCoordinator::AcceptanceCallback> accept_callbacks;

    MahoAiIngressCoordinator::DeliveryCallback GetCallback() {
      return base::BindRepeating(
          [](TestConsumer* self, const maho_ai::mojom::AskMahoDispatch& d,
             uint64_t delivery_id,
             MahoAiIngressCoordinator::AcceptanceCallback accept_cb) {
            self->dispatches.push_back(*d.Clone());
            self->delivery_ids.push_back(delivery_id);
            self->accept_callbacks.push_back(std::move(accept_cb));
          },
          base::Unretained(this));
    }
  };
};

// --- FIFO, Gating and Retention ---

TEST_F(MahoAiIngressCoordinatorTest, ColdRetentionAndFIFO) {
  base::HistogramTester histograms;
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  EXPECT_FALSE(coordinator->HasConsumer());

  coordinator->Dispatch(MakeDispatch("id1", "q1"));
  coordinator->Dispatch(MakeDispatch("id2", "q2"));
  coordinator->Dispatch(MakeDispatch("id3", "q3"));

  histograms.ExpectBucketCount("Maho.AI.Ingress.Dispatched", true, 3);
  histograms.ExpectUniqueSample("Maho.AI.Ingress.Deduplicated", true, 0);

  TestConsumer consumer;
  auto reg = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar, consumer.GetCallback());
  EXPECT_TRUE(coordinator->HasConsumer());

  // Only the first one is delivered because of FIFO transaction gating.
  ASSERT_EQ(1u, consumer.dispatches.size());
  EXPECT_EQ("q1", consumer.dispatches[0].query);
  EXPECT_EQ("id1", consumer.dispatches[0].request_id);

  // Accept it.
  std::move(consumer.accept_callbacks[0]).Run("session1");
  histograms.ExpectUniqueSample("Maho.AI.Ingress.Accepted", true, 1);

  // Second is still NOT delivered since A is not terminal.
  EXPECT_EQ(1u, consumer.dispatches.size());

  // Make it terminal.
  coordinator->NotifyTerminal(consumer.delivery_ids[0]);
  histograms.ExpectUniqueSample("Maho.AI.Ingress.Terminal", true, 1);

  // Now "q2" is delivered.
  ASSERT_EQ(2u, consumer.dispatches.size());
  EXPECT_EQ("q2", consumer.dispatches[1].query);
}

TEST_F(MahoAiIngressCoordinatorTest, DuplicateIgnoredForever) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());

  TestConsumer consumer;
  auto reg = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar, consumer.GetCallback());
  EXPECT_TRUE(coordinator->HasConsumer());

  coordinator->Dispatch(MakeDispatch("id1", "q1"));
  ASSERT_EQ(1u, consumer.dispatches.size());

  std::move(consumer.accept_callbacks[0]).Run("session1");
  coordinator->NotifyTerminal(consumer.delivery_ids[0]);

  // Re-dispatching "id1" should be ignored because seen_request_ids_ is
  // lifetime.
  coordinator->Dispatch(MakeDispatch("id1", "q1_retry"));
  EXPECT_EQ(1u, consumer.dispatches.size());
  reg.Reset();
  EXPECT_FALSE(coordinator->HasConsumer());
}

TEST_F(MahoAiIngressCoordinatorTest, StaleAcceptTerminalNoOp) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());

  TestConsumer consumer;
  auto reg = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar, consumer.GetCallback());

  coordinator->Dispatch(MakeDispatch("id1", "q1"));
  ASSERT_EQ(1u, consumer.dispatches.size());

  // Late/stale accept/terminal for other IDs should not crash/affect state.
  coordinator->NotifyTerminal(consumer.delivery_ids[0] + 1);
  std::move(consumer.accept_callbacks[0]).Run("session1");

  // Late terminal callback is still fine.
  coordinator->NotifyTerminal(consumer.delivery_ids[0]);

  // Now we should be able to deliver next.
  coordinator->Dispatch(MakeDispatch("id2", "q2"));
  ASSERT_EQ(2u, consumer.dispatches.size());
}

TEST_F(MahoAiIngressCoordinatorTest, LateAcceptDoesNotOverwriteMapping) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());

  TestConsumer consumer;
  auto reg = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar, consumer.GetCallback());

  coordinator->Dispatch(MakeDispatch("idA", "qA"));
  ASSERT_EQ(1u, consumer.dispatches.size());

  auto acceptA = std::move(consumer.accept_callbacks[0]);

  // Terminal A, which delivers B.
  coordinator->NotifyTerminal(consumer.delivery_ids[0]);
  coordinator->Dispatch(MakeDispatch("idB", "qB"));
  ASSERT_EQ(2u, consumer.dispatches.size());

  // Late accept of A should be no-op.
  std::move(acceptA).Run("sessionA");
}

TEST_F(MahoAiIngressCoordinatorTest,
       ReentrantTerminalAfterAcceptanceKeepsNextTransactionActive) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<std::string> delivered_request_ids;
  std::vector<uint64_t> delivery_ids;

  auto reg = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindRepeating(
          [](MahoAiIngressCoordinator* coordinator,
             std::vector<std::string>* delivered_request_ids,
             std::vector<uint64_t>* delivery_ids,
             const maho_ai::mojom::AskMahoDispatch& dispatch,
             uint64_t delivery_id,
             MahoAiIngressCoordinator::AcceptanceCallback accept_callback) {
            delivered_request_ids->push_back(dispatch.request_id);
            delivery_ids->push_back(delivery_id);
            if (dispatch.request_id == "idA") {
              std::move(accept_callback).Run("sessionA");
              coordinator->Dispatch(MakeDispatch("idB", "qB"));
              coordinator->NotifyTerminal(delivery_id);
            } else if (dispatch.request_id == "idB") {
              std::move(accept_callback).Run("sessionB");
            }
          },
          coordinator, &delivered_request_ids, &delivery_ids));

  coordinator->Dispatch(MakeDispatch("idA", "qA"));

  ASSERT_EQ(2u, delivered_request_ids.size());
  EXPECT_EQ("idA", delivered_request_ids[0]);
  EXPECT_EQ("idB", delivered_request_ids[1]);

  coordinator->Dispatch(MakeDispatch("idC", "qC"));
  EXPECT_EQ(2u, delivered_request_ids.size());

  coordinator->NotifyTerminal(delivery_ids[1]);
  ASSERT_EQ(3u, delivered_request_ids.size());
  EXPECT_EQ("idC", delivered_request_ids[2]);
}

TEST_F(MahoAiIngressCoordinatorTest,
       SynchronousTerminalDuringDeliveryRepumpsQueuedRequest) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  std::vector<std::string> delivered_request_ids;

  auto reg = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      base::BindRepeating(
          [](MahoAiIngressCoordinator* coordinator,
             std::vector<std::string>* delivered_request_ids,
             const maho_ai::mojom::AskMahoDispatch& dispatch,
             uint64_t delivery_id,
             MahoAiIngressCoordinator::AcceptanceCallback) {
            delivered_request_ids->push_back(dispatch.request_id);
            if (dispatch.request_id == "idA") {
              coordinator->Dispatch(MakeDispatch("idB", "qB"));
              coordinator->NotifyTerminal(delivery_id);
            }
          },
          coordinator, &delivered_request_ids));

  coordinator->Dispatch(MakeDispatch("idA", "qA"));

  ASSERT_EQ(2u, delivered_request_ids.size());
  EXPECT_EQ("idA", delivered_request_ids[0]);
  EXPECT_EQ("idB", delivered_request_ids[1]);
}

// --- Priority & Fallback ---

TEST_F(MahoAiIngressCoordinatorTest, PriorityAndFallback) {
  base::HistogramTester histograms;
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());

  TestConsumer sidebar;
  TestConsumer floating;

  auto reg_sidebar = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar, sidebar.GetCallback());

  auto reg_floating = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kFloating,
      floating.GetCallback());

  coordinator->Dispatch(MakeDispatch("id1", "q1"));

  // Delivered to floating (highest priority).
  ASSERT_EQ(1u, floating.dispatches.size());
  EXPECT_TRUE(sidebar.dispatches.empty());

  // Floating unregisters while unaccepted. Requeues and delivers to sidebar
  // fallback.
  reg_floating.Reset();
  histograms.ExpectUniqueSample("Maho.AI.Ingress.Requeued", true, 1);

  ASSERT_EQ(1u, sidebar.dispatches.size());
  EXPECT_EQ("q1", sidebar.dispatches[0].query);
}

TEST_F(MahoAiIngressCoordinatorTest, AcceptedNoRedelivery) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());

  TestConsumer floating;
  TestConsumer sidebar;

  auto reg_floating = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kFloating,
      floating.GetCallback());
  auto reg_sidebar = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar, sidebar.GetCallback());

  coordinator->Dispatch(MakeDispatch("id1", "q1"));
  ASSERT_EQ(1u, floating.dispatches.size());

  // Accept it.
  std::move(floating.accept_callbacks[0]).Run("session1");

  // Floating unregisters, but since it is already accepted, it must NOT
  // redeliver to sidebar.
  reg_floating.Reset();
  EXPECT_TRUE(sidebar.dispatches.empty());
}

TEST_F(MahoAiIngressCoordinatorTest,
       StaleTerminalFromSupersededDeliveryDoesNotTerminateRedelivery) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());

  TestConsumer sidebar;
  TestConsumer floating;
  auto sidebar_registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar, sidebar.GetCallback());
  auto floating_registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kFloating,
      floating.GetCallback());

  coordinator->Dispatch(MakeDispatch("request-x", "x"));
  coordinator->Dispatch(MakeDispatch("request-y", "y"));
  ASSERT_EQ(1u, floating.dispatches.size());
  ASSERT_EQ(1u, floating.delivery_ids.size());
  const uint64_t delivery_g1 = floating.delivery_ids[0];
  EXPECT_NE(0u, delivery_g1);

  floating_registration.Reset();
  ASSERT_EQ(1u, sidebar.dispatches.size());
  ASSERT_EQ(1u, sidebar.delivery_ids.size());
  EXPECT_EQ("request-x", sidebar.dispatches[0].request_id);
  const uint64_t delivery_g2 = sidebar.delivery_ids[0];
  EXPECT_NE(0u, delivery_g2);
  EXPECT_NE(delivery_g1, delivery_g2);

  coordinator->NotifyTerminal(delivery_g1);
  EXPECT_EQ(1u, sidebar.dispatches.size());

  coordinator->NotifyTerminal(delivery_g2);
  ASSERT_EQ(2u, sidebar.dispatches.size());
  EXPECT_EQ("request-y", sidebar.dispatches[1].request_id);
}

TEST_F(MahoAiIngressCoordinatorTest,
       StaleAcceptFromSupersededDeliveryDoesNotMutateRedelivery) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());

  TestConsumer fallback;
  TestConsumer floating_g2;
  TestConsumer floating_g1;
  auto fallback_registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar, fallback.GetCallback());
  auto floating_g2_registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kFloating,
      floating_g2.GetCallback());
  auto floating_g1_registration = coordinator->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kFloating,
      floating_g1.GetCallback());

  coordinator->Dispatch(MakeDispatch("request-x", "x"));
  ASSERT_EQ(1u, floating_g1.dispatches.size());
  ASSERT_EQ(1u, floating_g1.delivery_ids.size());
  const uint64_t delivery_g1 = floating_g1.delivery_ids[0];

  floating_g1_registration.Reset();
  ASSERT_EQ(1u, floating_g2.dispatches.size());
  ASSERT_EQ(1u, floating_g2.delivery_ids.size());
  const uint64_t delivery_g2 = floating_g2.delivery_ids[0];
  EXPECT_NE(0u, delivery_g1);
  EXPECT_NE(0u, delivery_g2);
  EXPECT_NE(delivery_g1, delivery_g2);

  std::move(floating_g1.accept_callbacks[0]).Run("stale-session");
  floating_g2_registration.Reset();

  ASSERT_EQ(1u, fallback.dispatches.size());
  ASSERT_EQ(1u, fallback.delivery_ids.size());
  EXPECT_EQ("request-x", fallback.dispatches[0].request_id);
  EXPECT_NE(0u, fallback.delivery_ids[0]);
  EXPECT_NE(delivery_g2, fallback.delivery_ids[0]);
}

// --- Basic Isolation & Teardown ---

TEST_F(MahoAiIngressCoordinatorTest, DispatchIsIsolatedPerBrowser) {
  std::unique_ptr<Browser> browser2 =
      CreateBrowser(profile(), browser()->GetType(), false);
  auto* coord1 = MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  auto* coord2 =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser2.get());

  TestConsumer consumer1;
  auto reg1 =
      coord1->RegisterConsumer(MahoAiIngressCoordinator::ConsumerType::kSidebar,
                               consumer1.GetCallback());

  TestConsumer consumer2;
  auto reg2 =
      coord2->RegisterConsumer(MahoAiIngressCoordinator::ConsumerType::kSidebar,
                               consumer2.GetCallback());

  coord1->Dispatch(MakeDispatch("id1", "for_browser_1"));

  EXPECT_EQ(1u, consumer1.dispatches.size());
  EXPECT_TRUE(consumer2.dispatches.empty());

  MahoAiIngressCoordinator::RemoveFromBrowser(browser2.get());
}

TEST_F(MahoAiIngressCoordinatorTest, RemoveFromBrowserTearsDownSynchronously) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  base::WeakPtr<MahoAiIngressCoordinator> weak_coordinator =
      coordinator->GetWeakPtr();
  EXPECT_NE(nullptr, MahoAiIngressCoordinator::FromBrowser(browser()));
  EXPECT_TRUE(weak_coordinator);

  MahoAiIngressCoordinator::RemoveFromBrowser(browser());
  EXPECT_EQ(nullptr, MahoAiIngressCoordinator::FromBrowser(browser()));
  EXPECT_FALSE(weak_coordinator);
}

TEST_F(MahoAiIngressCoordinatorTest,
       RetainedWeakPtrTerminalsOnlyItsExactLiveCoordinator) {
  std::unique_ptr<Browser> browser2 =
      CreateBrowser(profile(), browser()->GetType(), false);
  auto* coordinator1 =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  auto* coordinator2 =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser2.get());
  base::WeakPtr<MahoAiIngressCoordinator> weak_coordinator1 =
      coordinator1->GetWeakPtr();

  TestConsumer consumer1;
  auto registration1 = coordinator1->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      consumer1.GetCallback());
  TestConsumer consumer2;
  auto registration2 = coordinator2->RegisterConsumer(
      MahoAiIngressCoordinator::ConsumerType::kSidebar,
      consumer2.GetCallback());

  coordinator1->Dispatch(MakeDispatch("browser1-a", "a"));
  coordinator1->Dispatch(MakeDispatch("browser1-b", "b"));
  coordinator2->Dispatch(MakeDispatch("browser2-a", "a"));
  coordinator2->Dispatch(MakeDispatch("browser2-b", "b"));
  ASSERT_EQ(1u, consumer1.delivery_ids.size());
  ASSERT_EQ(1u, consumer2.delivery_ids.size());

  weak_coordinator1->NotifyTerminal(consumer1.delivery_ids[0]);

  ASSERT_EQ(2u, consumer1.dispatches.size());
  EXPECT_EQ("browser1-b", consumer1.dispatches[1].request_id);
  EXPECT_EQ(1u, consumer2.dispatches.size());

  MahoAiIngressCoordinator::RemoveFromBrowser(browser2.get());
}

TEST_F(MahoAiIngressCoordinatorTest,
       OnBrowserClosedSchedulesDeferredRemovalNotSynchronousDelete) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());
  ASSERT_NE(nullptr, coordinator);

  coordinator->OnBrowserClosed(browser());

  EXPECT_NE(nullptr, MahoAiIngressCoordinator::FromBrowser(browser()));

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(nullptr, MahoAiIngressCoordinator::FromBrowser(browser()));
}

TEST_F(MahoAiIngressCoordinatorTest,
       SubscriptionDestructionIsSafeAfterCoordinatorTeardown) {
  auto* coordinator =
      MahoAiIngressCoordinator::GetOrCreateForBrowser(browser());

  TestConsumer consumer;
  {
    auto reg = coordinator->RegisterConsumer(
        MahoAiIngressCoordinator::ConsumerType::kSidebar,
        consumer.GetCallback());
    MahoAiIngressCoordinator::RemoveFromBrowser(browser());
  }

  EXPECT_EQ(nullptr, MahoAiIngressCoordinator::FromBrowser(browser()));
}

}  // namespace maho
