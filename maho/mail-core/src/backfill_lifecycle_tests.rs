use super::*;
use std::future::Future;
use std::pin::Pin;
use std::task::Poll;

async fn poll_pending<F: Future>(future: Pin<&mut F>) {
    let mut future = future;
    std::future::poll_fn(|cx| {
        assert!(future.as_mut().poll(cx).is_pending(), "worker exited instead of idling");
        Poll::Ready(())
    }).await;
}

#[tokio::test]
async fn empty_bootstrap_remains_alive_for_arrivals() {
    let stop = Arc::new(AtomicBool::new(false));
    let token = CancellationToken::new();
    let wake = tokio::sync::Notify::new();
    let (started, mut starts) = tokio::sync::mpsc::unbounded_channel();
    let mut worker = Box::pin(run_backfill_loop("account", &stop, &token, &wake, || {
        let (reply, outcome) = tokio::sync::oneshot::channel();
        started.send(reply).unwrap();
        async { outcome.await.unwrap() }
    }));
    poll_pending(worker.as_mut()).await;
    starts.try_recv().unwrap().send(Ok(0)).unwrap();
    poll_pending(worker.as_mut()).await;
    assert!(starts.try_recv().is_err());
    wake.notify_one();
    poll_pending(worker.as_mut()).await;
    starts.try_recv().unwrap().send(Ok(0)).unwrap();
    poll_pending(worker.as_mut()).await;
    wake.notify_one();
    poll_pending(worker.as_mut()).await;
    starts.try_recv().unwrap().send(Ok(0)).unwrap();
    poll_pending(worker.as_mut()).await;
    token.cancel();
    tokio::time::timeout(Duration::from_secs(1), worker).await.unwrap();
}

#[tokio::test]
async fn in_flight_notification_is_retained_and_repeated_wakes_coalesce() {
    let stop = Arc::new(AtomicBool::new(false));
    let token = CancellationToken::new();
    let wake = tokio::sync::Notify::new();
    let (started, mut starts) = tokio::sync::mpsc::unbounded_channel();
    let mut worker = Box::pin(run_backfill_loop("account", &stop, &token, &wake, || {
        let (reply, outcome) = tokio::sync::oneshot::channel();
        started.send(reply).unwrap();
        async { outcome.await.unwrap() }
    }));
    poll_pending(worker.as_mut()).await;
    let first = starts.try_recv().unwrap();
    for _ in 0..10 { wake.notify_one(); }
    first.send(Ok(0)).unwrap();
    poll_pending(worker.as_mut()).await;
    starts.try_recv().unwrap().send(Ok(0)).unwrap();
    poll_pending(worker.as_mut()).await;
    assert!(starts.try_recv().is_err());
    stop.store(true, Ordering::SeqCst);
    wake.notify_one();
    tokio::time::timeout(Duration::from_secs(1), worker).await.unwrap();
    assert!(starts.try_recv().is_err());
}

#[tokio::test]
async fn stopped_generation_notification_cannot_wake_replacement() {
    let registry = SyncRegistry::default();
    let old = Arc::new(tokio::sync::Notify::new());
    let old_stop = Arc::new(AtomicBool::new(false));
    registry.insert_backfill("account".into(), WorkerHandle {
        stop: Arc::clone(&old_stop), tasks: vec![], wake: Some(Arc::clone(&old)),
    });
    let captured = registry.backfill_wake("account").unwrap();
    registry.stop_account("account");
    assert!(registry.backfill_wake("account").is_none());
    let current = Arc::new(tokio::sync::Notify::new());
    registry.insert_backfill("account".into(), WorkerHandle {
        stop: Arc::new(AtomicBool::new(false)), tasks: vec![], wake: Some(Arc::clone(&current)),
    });
    captured.notify_one();
    let mut new_notification = Box::pin(current.notified());
    poll_pending(new_notification.as_mut()).await;
    assert!(old_stop.load(Ordering::SeqCst));
    registry.backfill_wake("account").unwrap().notify_one();
    tokio::time::timeout(Duration::from_secs(1), new_notification).await.unwrap();
}
