// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_content_blocker_update_service.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/stringprintf.h"
#include "base/test/task_environment.h"
#include "base/test/test_io_thread.h"
#include "base/values.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/maho_core_holder.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_loader_factory.mojom-forward.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace maho {
namespace {

net::IPAddress PublicPeer() {
  return net::IPAddress(93, 184, 216, 34);  // example.com
}
net::IPAddress PrivatePeer() {
  return net::IPAddress(10, 0, 0, 1);
}

// The dedicated test main runs a plain base::TestSuite, so mojo (needed by
// SimpleURLLoader::DownloadAsStream) must be initialized here, once, for the
// whole process before any task environment exists.
class MojoTestEnvironment : public testing::Environment {
 public:
  void SetUp() override {
    mojo::core::Init();
    io_thread_ =
        std::make_unique<base::TestIOThread>(base::TestIOThread::kAutoStart);
    ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
        io_thread_->task_runner(),
        mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
  }
  void TearDown() override {
    ipc_support_.reset();
    io_thread_.reset();
  }

 private:
  std::unique_ptr<base::TestIOThread> io_thread_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
};

testing::Environment* const g_mojo_env =
    testing::AddGlobalTestEnvironment(new MojoTestEnvironment);

// Captured FilterListUpdateResponse handed to the apply boundary.
struct AppliedOutcome {
  std::string list_id;
  int status = -1;
  bool has_body = false;
  std::string body;
};

class MahoContentBlockerUpdateServiceTest : public testing::Test {
 protected:
  MahoContentBlockerUpdateServiceTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  ~MahoContentBlockerUpdateServiceTest() override {
    MahoContentBlockerUpdateService::SetDesignatedUpdater(nullptr);
  }

  std::unique_ptr<MahoContentBlockerUpdateService> MakeService() {
    scoped_refptr<network::SharedURLLoaderFactory> factory =
        test_url_loader_factory_.GetSafeWeakWrapper();
    auto service =
        std::make_unique<MahoContentBlockerUpdateService>(std::move(factory));
    service->SetApplyHooksForTesting(
        base::BindRepeating(&MahoContentBlockerUpdateServiceTest::CaptureApply,
                            base::Unretained(this)),
        base::BindRepeating(&MahoContentBlockerUpdateServiceTest::OnCompile,
                            base::Unretained(this)));
    return service;
  }

  void Designate(MahoContentBlockerUpdateService* service) {
    MahoContentBlockerUpdateService::SetDesignatedUpdater(service);
  }

  std::string CaptureApply(const std::string& json) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    AppliedOutcome outcome;
    if (parsed && parsed->is_dict()) {
      const base::DictValue& dict = parsed->GetDict();
      if (const std::string* id = dict.FindString("listId")) {
        outcome.list_id = *id;
      }
      outcome.status = dict.FindInt("statusCode").value_or(-1);
      if (const std::string* body = dict.FindString("body")) {
        outcome.has_body = true;
        outcome.body = *body;
      }
    }
    applied_.push_back(std::move(outcome));
    return apply_result_json_;
  }
  void OnCompile() { ++compile_count_; }

  network::mojom::URLResponseHeadPtr MakeHead(int status,
                                              const net::IPAddress& peer) {
    auto head = network::mojom::URLResponseHead::New();
    head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
        net::HttpUtil::AssembleRawHeaders(
            base::StringPrintf("HTTP/1.1 %d Status\n\n", status)));
    head->remote_endpoint = net::IPEndPoint(peer, 443);
    return head;
  }

  void AddResponse(const std::string& url,
                   const std::string& body,
                   int status,
                   const net::IPAddress& peer,
                   int net_error = net::OK) {
    test_url_loader_factory_.AddResponse(
        GURL(url), MakeHead(status, peer), body,
        network::URLLoaderCompletionStatus(net_error));
  }

  int64_t NowSeconds() const {
    return static_cast<int64_t>(base::Time::Now().InSecondsFSinceUnixEpoch());
  }

  content::BrowserTaskEnvironment task_environment_;
  network::TestURLLoaderFactory test_url_loader_factory_;
  std::vector<AppliedOutcome> applied_;
  std::string apply_result_json_ =
      R"({"success":false,"compileRequired":false})";
  int compile_count_ = 0;
};

TEST_F(MahoContentBlockerUpdateServiceTest, ConstructionDoesNotFetch) {
  auto service = MakeService();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
  EXPECT_EQ(0, test_url_loader_factory_.NumPending());
  EXPECT_TRUE(applied_.empty());
}

TEST_F(MahoContentBlockerUpdateServiceTest, SixteenMiBBodyAccepted) {
  auto service = MakeService();
  const std::string url = "https://lists.example/big.txt";
  const std::string body(MahoContentBlockerUpdateService::kMaxBodySize, 'a');
  AddResponse(url, body, 200, PublicPeer());

  service->FetchListForTesting("big", url);
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, applied_.size());
  EXPECT_EQ("big", applied_[0].list_id);
  EXPECT_EQ(200, applied_[0].status);
  EXPECT_TRUE(applied_[0].has_body);
  EXPECT_EQ(MahoContentBlockerUpdateService::kMaxBodySize,
            applied_[0].body.size());
  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
}

TEST_F(MahoContentBlockerUpdateServiceTest, OverSixteenMiBBodyRejected) {
  auto service = MakeService();
  const std::string url = "https://lists.example/toobig.txt";
  const std::string body(MahoContentBlockerUpdateService::kMaxBodySize + 1,
                         'a');
  AddResponse(url, body, 200, PublicPeer());

  service->FetchListForTesting("toobig", url);
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, applied_.size());
  EXPECT_EQ(0, applied_[0].status);
  EXPECT_FALSE(applied_[0].has_body);
  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
}

TEST_F(MahoContentBlockerUpdateServiceTest, MaxFourConcurrentAndQueuesFifth) {
  auto service = MakeService();
  for (int i = 0; i < 5; ++i) {
    service->FetchListForTesting(
        base::StringPrintf("list%d", i),
        base::StringPrintf("https://list%d.example/f.txt", i));
  }
  task_environment_.RunUntilIdle();

  EXPECT_EQ(MahoContentBlockerUpdateService::kMaxInFlightDownloads,
            service->GetActiveRequestCountForTesting());
  EXPECT_EQ(1u, service->GetPendingQueueSizeForTesting());
  EXPECT_EQ(4, test_url_loader_factory_.NumPending());

  AddResponse("https://list0.example/f.txt", "body", 200, PublicPeer());
  task_environment_.RunUntilIdle();

  EXPECT_EQ(MahoContentBlockerUpdateService::kMaxInFlightDownloads,
            service->GetActiveRequestCountForTesting());
  EXPECT_EQ(0u, service->GetPendingQueueSizeForTesting());
  EXPECT_TRUE(
      test_url_loader_factory_.IsPending("https://list4.example/f.txt"));
}

TEST_F(MahoContentBlockerUpdateServiceTest, DuplicateListSuppressed) {
  auto service = MakeService();
  service->FetchListForTesting("dup", "https://a.example/f.txt");
  service->FetchListForTesting("dup", "https://b.example/f.txt");
  task_environment_.RunUntilIdle();

  EXPECT_EQ(1u, service->GetActiveRequestCountForTesting());
  EXPECT_EQ(0u, service->GetPendingQueueSizeForTesting());
  EXPECT_EQ(1, test_url_loader_factory_.NumPending());
}

TEST_F(MahoContentBlockerUpdateServiceTest, ExactHttpsRequired) {
  auto service = MakeService();
  // wss is "cryptographic" but not https; it must be rejected.
  service->FetchListForTesting("wss", "wss://lists.example/f.txt");
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, applied_.size());
  EXPECT_EQ(0, applied_[0].status);
  EXPECT_FALSE(applied_[0].has_body);
  EXPECT_EQ(0, test_url_loader_factory_.NumPending());
}

TEST_F(MahoContentBlockerUpdateServiceTest, UnsafeInitialTargetsRejected) {
  auto service = MakeService();
  service->FetchListForTesting("http", "http://lists.example/f.txt");
  service->FetchListForTesting("localhost", "https://localhost/f.txt");
  service->FetchListForTesting("loopback", "https://127.0.0.1/f.txt");
  service->FetchListForTesting("private", "https://10.0.0.1/f.txt");
  service->FetchListForTesting("linklocal", "https://169.254.1.1/f.txt");
  service->FetchListForTesting("ipv6loop", "https://[::1]/f.txt");
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
  EXPECT_EQ(0, test_url_loader_factory_.NumPending());
  EXPECT_EQ(6u, applied_.size());
  for (const auto& outcome : applied_) {
    EXPECT_EQ(0, outcome.status);
    EXPECT_FALSE(outcome.has_body);
  }
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       UnsafeRedirectForwardedAsFailureNeverBody) {
  auto service = MakeService();
  const GURL initial("https://cdn.example/list.txt");

  net::RedirectInfo redirect;
  redirect.status_code = 301;
  redirect.new_url = GURL("http://downgrade.example/list.txt");
  network::TestURLLoaderFactory::Redirects redirects;
  redirects.emplace_back(redirect, MakeHead(301, PublicPeer()));

  test_url_loader_factory_.AddResponse(
      initial, MakeHead(200, PublicPeer()), "SHOULD-NOT-BE-APPLIED",
      network::URLLoaderCompletionStatus(net::OK), std::move(redirects));

  service->FetchListForTesting("easylist", initial.spec());
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, applied_.size());
  EXPECT_EQ(0, applied_[0].status);
  EXPECT_FALSE(applied_[0].has_body);
  EXPECT_NE("SHOULD-NOT-BE-APPLIED", applied_[0].body);
  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
}

TEST_F(MahoContentBlockerUpdateServiceTest, PrivateRemoteEndpointRejected) {
  auto service = MakeService();
  const std::string url = "https://public-looking.example/f.txt";
  AddResponse(url, "blocked-body", 200, PrivatePeer());

  service->FetchListForTesting("sneaky", url);
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, applied_.size());
  EXPECT_EQ(0, applied_[0].status);
  EXPECT_FALSE(applied_[0].has_body);
}

TEST_F(MahoContentBlockerUpdateServiceTest, PublicRemoteEndpointAccepted) {
  auto service = MakeService();
  const std::string url = "https://public.example/f.txt";
  AddResponse(url, "! rules", 200, PublicPeer());

  service->FetchListForTesting("ok", url);
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, applied_.size());
  EXPECT_EQ(200, applied_[0].status);
  EXPECT_TRUE(applied_[0].has_body);
  EXPECT_EQ("! rules", applied_[0].body);
}

TEST_F(MahoContentBlockerUpdateServiceTest, Http200WithNetErrorIsFailure) {
  auto service = MakeService();
  const std::string url = "https://public.example/f.txt";
  AddResponse(url, "partial", 200, PublicPeer(), net::ERR_FAILED);

  service->FetchListForTesting("truncated", url);
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, applied_.size());
  EXPECT_NE(200, applied_[0].status);
  EXPECT_FALSE(applied_[0].has_body);
}

TEST_F(MahoContentBlockerUpdateServiceTest, NotModified304SafePath) {
  auto service = MakeService();
  const std::string url = "https://public.example/f.txt";
  AddResponse(url, "", 304, PublicPeer());

  service->FetchListForTesting("nm", url);
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, applied_.size());
  EXPECT_EQ(304, applied_[0].status);
  EXPECT_FALSE(applied_[0].has_body);
}

TEST_F(MahoContentBlockerUpdateServiceTest, RebuildTrueTriggersCompile) {
  auto service = MakeService();
  apply_result_json_ = R"({"success":true,"compileRequired":true})";
  const std::string url = "https://public.example/f.txt";
  AddResponse(url, "! rules", 200, PublicPeer());

  service->FetchListForTesting("r", url);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(1, compile_count_);
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       CandidateRejectedDoesNotCompileAndSurfacesOutcome) {
  auto service = MakeService();
  apply_result_json_ =
      R"({"success":false,"compileRequired":false,"error":{"code":"candidate_rejected"}})";
  const std::string url = "https://public.example/f.txt";
  AddResponse(url, "! rules", 200, PublicPeer());

  service->FetchListForTesting("nr", url);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0, compile_count_);
  ASSERT_EQ(1u, applied_.size());
  EXPECT_EQ(200, applied_[0].status);
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       NotModifiedOrUnchangedTypedResultsDoNotCompile) {
  auto service = MakeService();
  const std::string url = "https://public.example/f.txt";
  AddResponse(url, "", 304, PublicPeer());

  apply_result_json_ = R"({"success":true,"compileRequired":false})";
  service->FetchListForTesting("not-modified", url);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0, compile_count_);

  AddResponse(url, "! rules", 200, PublicPeer());
  service->FetchListForTesting("unchanged", url);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0, compile_count_);
  ASSERT_EQ(2u, applied_.size());
  EXPECT_EQ(304, applied_[0].status);
  EXPECT_EQ(200, applied_[1].status);
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       PersistenceOrInstallFailureResultDoesNotCompile) {
  auto service = MakeService();
  apply_result_json_ =
      R"({"success":false,"compileRequired":false,"error":{"code":"install_rejected"}})";
  const std::string url = "https://public.example/f.txt";
  AddResponse(url, "! rules", 200, PublicPeer());

  service->FetchListForTesting("persistence", url);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0, compile_count_);
  ASSERT_EQ(1u, applied_.size());
  EXPECT_EQ(200, applied_[0].status);
}

TEST(MahoCoreHolderInstallResultTest,
     PersistenceAndInstallFailuresAreNeverSuccessful) {
  EXPECT_FALSE(maho::IsContentBlockerInstallResultSuccess(
      R"({"success":false,"error":{"code":"persistence_failed"}})"));
  EXPECT_FALSE(maho::IsContentBlockerInstallResultSuccess(
      R"({"success":false,"error":{"code":"install_rejected"}})"));
  EXPECT_FALSE(maho::IsContentBlockerInstallResultSuccess(
      R"({"success":true,"error":{"code":"persistence_failed"}})"));
  EXPECT_FALSE(maho::IsContentBlockerInstallResultSuccess(R"({"error":{}})"));
  EXPECT_FALSE(maho::IsContentBlockerInstallResultSuccess("not-json"));
  EXPECT_TRUE(
      maho::IsContentBlockerInstallResultSuccess(R"({"success":true})"));
}

TEST_F(MahoContentBlockerUpdateServiceTest, EarliestDueTimestampWins) {
  auto service = MakeService();
  const int64_t now = NowSeconds();
  const std::string lists = base::StringPrintf(
      R"({"id":"a","name":"a","url":"https://a.example/f","enabled":true,)"
      R"("ruleCount":0,"failureCount":1,"lastAttemptTimestamp":%lld,)"
      R"("nextRetryTimestamp":%lld},)"
      R"({"id":"b","name":"b","url":"https://b.example/f","enabled":true,)"
      R"("ruleCount":0,"failureCount":0,"lastSuccessTimestamp":%lld})",
      static_cast<long long>(now), static_cast<long long>(now + 100),
      static_cast<long long>(now));
  service->SetStateProviderForTesting(base::BindRepeating(
      [](std::string json) { return json; },
      base::StringPrintf(
          R"({"mode":"native","lists":[%s],"exceptions":[],"health":{},)"
          R"("engineGeneration":0})",
          lists.c_str())));
  Designate(service.get());

  service->CheckForUpdates();
  // Neither list is due now, so nothing fetched; timer targets list a (+100s).
  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
  EXPECT_EQ(base::Seconds(100),
            task_environment_.NextMainThreadPendingTaskDelay());
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       HealthyListDoesNotHideFailingRetry) {
  auto service = MakeService();
  const int64_t now = NowSeconds();
  // a is healthy (due in 24h); b is failing with a 500s retry. The next check
  // must honor b's failure schedule, not a's success cadence.
  const std::string lists = base::StringPrintf(
      R"({"id":"a","name":"a","url":"https://a.example/f","enabled":true,)"
      R"("ruleCount":0,"failureCount":0,"lastSuccessTimestamp":%lld},)"
      R"({"id":"b","name":"b","url":"https://b.example/f","enabled":true,)"
      R"("ruleCount":0,"failureCount":1,"lastAttemptTimestamp":%lld,)"
      R"("nextRetryTimestamp":%lld})",
      static_cast<long long>(now), static_cast<long long>(now),
      static_cast<long long>(now + 500));
  service->SetStateProviderForTesting(base::BindRepeating(
      [](std::string json) { return json; },
      base::StringPrintf(
          R"({"mode":"native","lists":[%s],"exceptions":[],"health":{},)"
          R"("engineGeneration":0})",
          lists.c_str())));
  Designate(service.get());

  service->CheckForUpdates();
  EXPECT_EQ(base::Seconds(500),
            task_environment_.NextMainThreadPendingTaskDelay());
}

TEST_F(MahoContentBlockerUpdateServiceTest, ModeFalseCancelsAndPreventsApply) {
  auto service = MakeService();
  const std::string url = "https://public.example/f.txt";
  service->SetStateProviderForTesting(base::BindRepeating(
      [](std::string json) { return json; },
      base::StringPrintf(
          R"({"mode":"native","lists":[{"id":"m","name":"m","url":"%s",)"
          R"("enabled":true,"ruleCount":0,"failureCount":0}],)"
          R"("exceptions":[],"health":{},"engineGeneration":0})",
          url.c_str())));
  Designate(service.get());
  AddResponse(url, "! rules", 200, PublicPeer());

  service->CheckForUpdates();  // due-now list -> request starts.
  EXPECT_EQ(1u, service->GetActiveRequestCountForTesting());

  service->OnModeChanged(false);  // cancel before the response is pumped.
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
  EXPECT_TRUE(applied_.empty());
}

TEST_F(MahoContentBlockerUpdateServiceTest, ModeTrueResumesFetching) {
  auto service = MakeService();
  const std::string url = "https://public.example/f.txt";
  service->SetStateProviderForTesting(base::BindRepeating(
      [](std::string json) { return json; },
      base::StringPrintf(
          R"({"mode":"native","lists":[{"id":"m","name":"m","url":"%s",)"
          R"("enabled":true,"ruleCount":0,"failureCount":0}],)"
          R"("exceptions":[],"health":{},"engineGeneration":0})",
          url.c_str())));
  Designate(service.get());

  service->OnModeChanged(false);
  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());

  service->OnModeChanged(true);  // resume -> immediate due check.
  EXPECT_EQ(1u, service->GetActiveRequestCountForTesting());
}

TEST_F(MahoContentBlockerUpdateServiceTest, NonDesignatedServiceIsInert) {
  auto service = MakeService();
  service->SetStateProviderForTesting(base::BindRepeating(
      [](std::string json) { return json; },
      std::string(R"({"mode":"native","lists":[{"id":"x","name":"x",)"
                  R"("url":"https://x.example/f","enabled":true,"ruleCount":0,)"
                  R"("failureCount":0}],"exceptions":[],"health":{},)"
                  R"("engineGeneration":0})")));
  // Deliberately NOT designated.
  service->CheckForUpdates();
  service->UpdateList("x");
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
  EXPECT_EQ(0, test_url_loader_factory_.NumPending());
  EXPECT_TRUE(applied_.empty());
}

TEST_F(MahoContentBlockerUpdateServiceTest, DoubleShutdownAndLateCallbackSafe) {
  auto service = MakeService();
  const std::string url = "https://public.example/f.txt";
  AddResponse(url, "! rules", 200, PublicPeer());
  service->FetchListForTesting("late", url);
  EXPECT_EQ(1u, service->GetActiveRequestCountForTesting());

  service->Shutdown();
  service->Shutdown();  // idempotent
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
  EXPECT_TRUE(applied_.empty());
  // Destructor (end of scope) runs DoShutdown again without crashing.
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       StartFetchBlocksLocalNetworkRequest) {
  auto service = MakeService();
  // No response registered: the request stays pending so its options are
  // inspectable on the factory.
  service->FetchListForTesting("opt", "https://public.example/f.txt");
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1, test_url_loader_factory_.NumPending());
  network::TestURLLoaderFactory::PendingRequest* pending =
      test_url_loader_factory_.GetPendingRequest(0);
  ASSERT_TRUE(pending);
  EXPECT_TRUE(pending->options &
              network::mojom::kURLLoadOptionBlockLocalRequest);
  // Proxy bypass ensures the local-network option and peer-IP guard evaluate
  // the real origin instead of a proxy endpoint.
  EXPECT_TRUE(pending->request.load_flags & net::LOAD_BYPASS_PROXY);
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       NonDesignatedInstanceIgnoresGlobalNotify) {
  auto secondary = MakeService();
  secondary->FetchListForTesting("sec", "https://sec.example/f.txt");
  task_environment_.RunUntilIdle();
  ASSERT_EQ(1u, secondary->GetActiveRequestCountForTesting());

  auto designated = MakeService();
  Designate(designated.get());

  // The global notify reaches only the designated instance; the secondary
  // (non-designated) service keeps its in-flight work untouched.
  MahoContentBlockerUpdateService::NotifyDesignatedModeChanged(false);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(1u, secondary->GetActiveRequestCountForTesting());
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       StaticModeFalseCancelsDesignatedService) {
  auto service = MakeService();
  Designate(service.get());
  service->FetchListForTesting("a", "https://a.example/f.txt");
  ASSERT_EQ(1u, service->GetActiveRequestCountForTesting());

  MahoContentBlockerUpdateService::NotifyDesignatedModeChanged(false);
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       StaticTriggerUpdateFalseWithoutDesignatedUpdater) {
  MahoContentBlockerUpdateService::SetDesignatedUpdater(nullptr);
  EXPECT_FALSE(
      MahoContentBlockerUpdateService::TriggerDesignatedUpdate(std::string()));
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       StaticManualUpdateRejectsNonNativeAndWorksAfterResume) {
  auto service = MakeService();
  const std::string url = "https://public.example/f.txt";
  service->SetStateProviderForTesting(base::BindRepeating(
      [](std::string json) { return json; },
      base::StringPrintf(
          R"({"mode":"native","lists":[{"id":"m","name":"m","url":"%s",)"
          R"("enabled":true,"ruleCount":0,"failureCount":0}],)"
          R"("exceptions":[],"health":{},"engineGeneration":0})",
          url.c_str())));
  AddResponse(url, "! rules", 200, PublicPeer());
  Designate(service.get());

  // Non-native: manual update rejected, no network work.
  MahoContentBlockerUpdateService::NotifyDesignatedModeChanged(false);
  EXPECT_FALSE(MahoContentBlockerUpdateService::TriggerDesignatedUpdate("m"));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, test_url_loader_factory_.NumPending());
  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());

  // Native resume: manual update is accepted.
  MahoContentBlockerUpdateService::NotifyDesignatedModeChanged(true);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(MahoContentBlockerUpdateService::TriggerDesignatedUpdate("m"));
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       ManualUpdateAllForcesEnabledListsIgnoringCadence) {
  auto service = MakeService();
  const int64_t now = NowSeconds();
  // Two healthy lists that just succeeded: both are due in ~24h, not now.
  service->SetStateProviderForTesting(base::BindRepeating(
      [](std::string json) { return json; },
      base::StringPrintf(
          R"({"mode":"native","lists":[)"
          R"({"id":"a","name":"a","url":"https://a.example/f","enabled":true,)"
          R"("ruleCount":0,"failureCount":0,"lastSuccessTimestamp":%lld},)"
          R"({"id":"b","name":"b","url":"https://b.example/f","enabled":true,)"
          R"("ruleCount":0,"failureCount":0,"lastSuccessTimestamp":%lld})"
          R"(],"exceptions":[],"health":{},"engineGeneration":0})",
          static_cast<long long>(now), static_cast<long long>(now))));
  Designate(service.get());

  EXPECT_TRUE(
      MahoContentBlockerUpdateService::TriggerDesignatedUpdate(std::string()));
  task_environment_.RunUntilIdle();

  // Manual "update all" forces every enabled list despite the 24h cadence.
  EXPECT_EQ(2u, service->GetActiveRequestCountForTesting());
}

TEST_F(MahoContentBlockerUpdateServiceTest, AutomaticCheckSkipsNonDueLists) {
  auto service = MakeService();
  const int64_t now = NowSeconds();
  service->SetStateProviderForTesting(base::BindRepeating(
      [](std::string json) { return json; },
      base::StringPrintf(
          R"({"mode":"native","lists":[)"
          R"({"id":"a","name":"a","url":"https://a.example/f","enabled":true,)"
          R"("ruleCount":0,"failureCount":0,"lastSuccessTimestamp":%lld},)"
          R"({"id":"b","name":"b","url":"https://b.example/f","enabled":true,)"
          R"("ruleCount":0,"failureCount":0,"lastSuccessTimestamp":%lld})"
          R"(],"exceptions":[],"health":{},"engineGeneration":0})",
          static_cast<long long>(now), static_cast<long long>(now))));
  Designate(service.get());

  service->CheckForUpdates();
  task_environment_.RunUntilIdle();

  // Automatic path stays due-aware: neither recently-successful list fetches.
  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
  EXPECT_EQ(0, test_url_loader_factory_.NumPending());
}

TEST_F(MahoContentBlockerUpdateServiceTest,
       AuthoritativeNonNativeCancelsActiveAndQueued) {
  auto service = MakeService();
  Designate(service.get());
  // 4 active + 1 queued, started directly (bypasses due/designation gating).
  for (int i = 0; i < 5; ++i) {
    service->FetchListForTesting(
        base::StringPrintf("l%d", i),
        base::StringPrintf("https://l%d.example/f.txt", i));
  }
  task_environment_.RunUntilIdle();
  ASSERT_EQ(4u, service->GetActiveRequestCountForTesting());
  ASSERT_EQ(1u, service->GetPendingQueueSizeForTesting());

  // Authoritative state flips to non-native with no explicit notification; the
  // next CheckForUpdates must cancel everything and apply nothing.
  service->SetStateProviderForTesting(base::BindRepeating(
      [](std::string json) { return json; },
      std::string(R"({"mode":"extension","lists":[],"exceptions":[],)"
                  R"("health":{},"engineGeneration":0})")));
  service->CheckForUpdates();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, service->GetActiveRequestCountForTesting());
  EXPECT_EQ(0u, service->GetPendingQueueSizeForTesting());
  EXPECT_TRUE(applied_.empty());
}

}  // namespace
}  // namespace maho
