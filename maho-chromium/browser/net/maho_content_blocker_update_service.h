// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_NET_MAHO_CONTENT_BLOCKER_UPDATE_SERVICE_H_
#define MAHO_CHROMIUM_BROWSER_NET_MAHO_CONTENT_BLOCKER_UPDATE_SERVICE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"

class GURL;

namespace net {
struct RedirectInfo;
class IPEndPoint;
}  // namespace net

namespace network::mojom {
class URLResponseHead;
}  // namespace network::mojom

namespace maho {

// Fetches native content-blocking filter lists and forwards every terminal
// outcome (safe 200 / safe 304 / failure) to the Rust core as JSON so per-list
// health and scheduling are authoritative. Runtime-safety invariants:
//   * bounded 16 MiB streamed body (DownloadAsStream, never buffers more);
//   * exact `https` + publicly-routable connected peer, re-checked
//   post-connect;
//   * at most kMaxInFlightDownloads concurrent requests, deduplicated by list
//   id;
//   * a single process-wide designated updater drives the shared core;
//   * shutdown/mode-off cancels all work and can never apply late bytes.
class MahoContentBlockerUpdateService : public KeyedService {
 public:
  static constexpr size_t kMaxInFlightDownloads = 4;
  static constexpr size_t kMaxBodySize = 16 * 1024 * 1024;  // 16 MiB

  // Returns the content-blocker state JSON (ContentBlockerStateDto, camelCase).
  using StateProvider = base::RepeatingCallback<std::string()>;
  // Applies a FilterListUpdateResponse JSON at the core boundary and returns
  // the typed ContentBlockerMutationResult JSON. A candidate is compiled only
  // when that result explicitly requests it.
  using ApplyCallback =
      base::RepeatingCallback<std::string(const std::string& json)>;
  // Triggers the coalescing holder compile/install after a rebuild-true apply.
  using CompileCallback = base::RepeatingClosure;

  explicit MahoContentBlockerUpdateService(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~MahoContentBlockerUpdateService() override;

  MahoContentBlockerUpdateService(const MahoContentBlockerUpdateService&) =
      delete;
  MahoContentBlockerUpdateService& operator=(
      const MahoContentBlockerUpdateService&) = delete;

  // KeyedService:
  void Shutdown() override;

  // Designated-updater gating: only one process-wide service may drive the
  // shared Rust core. Non-designated instances are inert for CheckForUpdates /
  // UpdateList. |service| may be null to clear.
  static void SetDesignatedUpdater(MahoContentBlockerUpdateService* service);
  bool IsDesignatedUpdater() const;

  // Static entrypoints that operate ONLY on the current process-wide designated
  // updater (no public raw-pointer getter is exposed). Safe when there is no
  // designated updater or it is shut down; callers pass through profile-level
  // events without needing to know which instance is designated.
  static void NotifyDesignatedModeChanged(bool is_native);
  // Triggers a manual update on the designated updater. Empty |list_id| means
  // all lists. Returns true iff a live, native-mode designated updater accepted
  // the work (false when none designated or the current mode is non-native).
  static bool TriggerDesignatedUpdate(const std::string& list_id);

  // Evaluates state, fetches due enabled lists (native mode only), and always
  // reschedules a future check so scheduling survives non-native modes.
  void CheckForUpdates();
  // Forces a fetch of a single list by id.
  void UpdateList(const std::string& list_id);

  // Content-blocking mode transition hook.
  //   false: pause - stop timer, cancel active + queued work, block applies.
  //   true:  resume - immediate due check and reschedule.
  void OnModeChanged(bool is_native);

  bool is_updating() const { return !active_requests_.empty(); }

  // Test seams (production defaults use GetCore + bridge APIs).
  void SetStateProviderForTesting(StateProvider provider);
  void SetApplyHooksForTesting(ApplyCallback apply, CompileCallback compile);
  void FetchListForTesting(const std::string& list_id, const std::string& url);
  size_t GetActiveRequestCountForTesting() const {
    return active_requests_.size();
  }
  size_t GetPendingQueueSizeForTesting() const { return pending_queue_.size(); }

 private:
  class StreamConsumer;

  struct PendingFetch {
    PendingFetch();
    PendingFetch(const PendingFetch&);
    PendingFetch& operator=(const PendingFetch&);
    PendingFetch(PendingFetch&&) noexcept;
    PendingFetch& operator=(PendingFetch&&) noexcept;
    ~PendingFetch();

    std::string list_id;
    std::string url;
    std::string etag;
    std::string last_modified;
  };

  struct ActiveRequest {
    ActiveRequest();
    ~ActiveRequest();
    std::string list_id;
    std::string body;
    bool overflowed = false;
    bool finalized = false;
    // Declared last so it is destroyed first: destroying the loader cancels the
    // request and guarantees no further callbacks into |consumer|.
    std::unique_ptr<StreamConsumer> consumer;
    std::unique_ptr<network::SimpleURLLoader> loader;
  };

  void FetchList(const std::string& list_id,
                 const std::string& url,
                 const std::string& etag,
                 const std::string& last_modified);
  void MaybeStartNextFetch();
  void StartFetch(const PendingFetch& fetch);

  void OnRedirect(const std::string& list_id,
                  ActiveRequest* request,
                  const GURL& url_before_redirect,
                  const net::RedirectInfo& redirect_info,
                  const network::mojom::URLResponseHead& response_head,
                  std::vector<std::string>* removed_headers);

  // Called by the per-request StreamConsumer on this sequence.
  void OnStreamData(ActiveRequest* request,
                    std::string_view chunk,
                    base::OnceClosure resume);
  void OnStreamComplete(ActiveRequest* request, bool stream_success);

  // Classifies the terminal outcome, forwards JSON, then removes the request
  // (deferred, since it runs on the consumer's stack) and advances the queue.
  void FinalizeRequest(ActiveRequest* request, bool stream_success);

  // Emits a per-list failure for a request that is aborted before the loader
  // reports completion (overflow, unsafe redirect), without querying the
  // not-yet-finished loader. |destroy_sync| destroys the loader immediately
  // (safe from a redirect callback); otherwise destruction is deferred (safe
  // from a stream-data callback).
  void EmitAbortFailure(ActiveRequest* request, bool destroy_sync);

  // Builds a FilterListUpdateResponse JSON and hands it to the apply boundary.
  void ForwardOutcome(const std::string& list_id,
                      int status_code,
                      std::optional<std::string> body,
                      const std::string& etag,
                      const std::string& last_modified);

  // Removes |request| from the active set. Sync erase destroys the loader
  // immediately (only safe outside the consumer's own callbacks, e.g. redirect
  // rejection); deferred erase hands the object to DeleteSoon so a consumer
  // callback can complete without self-delete UAF.
  void RemoveRequestSync(ActiveRequest* request);
  void RemoveRequestDeferred(ActiveRequest* request);

  void ScheduleFromState();
  void DoShutdown();

  // Instance implementation of TriggerDesignatedUpdate on the designated
  // updater; rejects (returns false) unless live, designated, and native.
  bool TriggerUpdate(const std::string& list_id);

  // Cancels all active/queued work and marks the service non-native. Shared by
  // explicit OnModeChanged(false) and authoritative non-native detection inside
  // CheckForUpdates; callers reschedule (benign) afterward.
  void CancelAllWorkForModeOff();

  // Manual "update all": force-fetches every enabled list from authoritative
  // state, ignoring cadence/retry due time. Native-gated; dedup and the max-4
  // ceiling still apply. Distinct from the due-aware automatic CheckForUpdates.
  void ForceUpdateAllEnabled();

  std::string GetStateJson();

  static bool IsAllowedUpdateUrl(const GURL& url);
  static bool IsPubliclyRoutablePeer(const net::IPEndPoint& endpoint);

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  StateProvider state_provider_;
  ApplyCallback apply_callback_;
  CompileCallback compile_callback_;

  std::vector<std::unique_ptr<ActiveRequest>> active_requests_;
  base::circular_deque<PendingFetch> pending_queue_;
  base::flat_set<std::string> active_or_queued_ids_;

  base::OneShotTimer update_timer_;
  bool is_shutdown_ = false;
  bool is_native_mode_ = true;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoContentBlockerUpdateService> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_CHROMIUM_BROWSER_NET_MAHO_CONTENT_BLOCKER_UPDATE_SERVICE_H_
