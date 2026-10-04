// Compile the actual benchmark worker implementation under the test harness.
include!("../benches/sqlite_concurrency_bench.rs");

mod tests {
    use super::*;

    #[test]
    fn disconnected_reader_cannot_report_a_successful_round() {
        let (task_tx, task_rx) = std::sync::mpsc::channel();
        let (done_tx, done_rx) = std::sync::mpsc::channel();
        drop(task_rx);
        drop(done_tx);
        let pool = ReaderPool {
            workers: vec![ReaderWorker {
                task_tx: Some(task_tx),
                done_rx,
                handle: None,
            }],
        };
        assert!(
            std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| pool.execute_round()))
                .is_err()
        );
    }

    #[test]
    fn worker_panic_is_reported_when_joined() {
        let (_, done_rx) = std::sync::mpsc::channel();
        let worker = ReaderWorker {
            task_tx: None,
            done_rx,
            handle: Some(std::thread::spawn(|| panic!("reader query failed"))),
        };
        assert!(std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| drop(worker))).is_err());
    }
}
