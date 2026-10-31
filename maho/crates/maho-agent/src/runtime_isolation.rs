use crate::AgentError;
use std::future::Future;
use std::sync::OnceLock;
use std::time::Duration;
use tokio::runtime::Runtime;

static GLOBAL_RUNTIME: OnceLock<Result<Runtime, String>> = OnceLock::new();

pub struct SessionRuntime {
    handle: tokio::runtime::Handle,
}

impl SessionRuntime {
    pub fn new() -> Result<Self, AgentError> {
        let runtime_res = GLOBAL_RUNTIME.get_or_init(|| {
            tokio::runtime::Builder::new_multi_thread()
                .thread_name("maho-agent-worker")
                .enable_all()
                .build()
                .map_err(|e| format!("Failed to build global runtime: {e}"))
        });
        match runtime_res {
            Ok(runtime) => Ok(Self {
                handle: runtime.handle().clone(),
            }),
            Err(err) => Err(AgentError::ExecutionError(err.clone())),
        }
    }

    pub fn block_on<F, T>(&self, future: F) -> Result<T, AgentError>
    where
        F: Future<Output = T> + Send + 'static,
        T: Send + 'static,
    {
        // Handle::block_on panics when the calling thread is already inside a
        // tokio runtime. A multi-thread worker may hand its slot off with
        // block_in_place; a current-thread runtime cannot block at all, so
        // that case is an error instead of a process-killing panic.
        let in_multi_thread_runtime = match tokio::runtime::Handle::try_current() {
            Err(_) => false,
            Ok(current)
                if current.runtime_flavor() == tokio::runtime::RuntimeFlavor::MultiThread =>
            {
                true
            }
            Ok(_) => {
                return Err(AgentError::ExecutionError(
                    "SessionRuntime::block_on called from a current-thread tokio runtime"
                        .to_string(),
                ))
            }
        };

        // We run the future on the runtime; a panicking task surfaces as a
        // JoinError and is mapped to an error below.
        let join_handle = self.handle.spawn(future);
        let wait = async {
            match join_handle.await {
                Ok(res) => Ok(res),
                Err(join_err) => {
                    if join_err.is_panic() {
                        Err(AgentError::ExecutionError("Panic in task".to_string()))
                    } else {
                        Err(AgentError::ExecutionError(format!(
                            "Join error: {join_err}"
                        )))
                    }
                }
            }
        };
        if in_multi_thread_runtime {
            tokio::task::block_in_place(|| self.handle.block_on(wait))
        } else {
            self.handle.block_on(wait)
        }
    }

    /// Runs a future with a timeout
    pub fn block_on_with_timeout<F, T>(&self, future: F, timeout: Duration) -> Result<T, AgentError>
    where
        F: Future<Output = T> + Send + 'static,
        T: Send + 'static,
    {
        self.block_on(async move {
            match tokio::time::timeout(timeout, future).await {
                Ok(res) => Ok(res),
                Err(_) => Err(AgentError::ExecutionError(
                    "Operation timed out".to_string(),
                )),
            }
        })?
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_runtime_isolation_normal() {
        let runtime = SessionRuntime::new().unwrap();
        let res = runtime.block_on(async { 42 }).unwrap();
        assert_eq!(res, 42);
    }

    #[test]
    fn test_runtime_isolation_panic() {
        let runtime = SessionRuntime::new().unwrap();
        let res = runtime.block_on(async {
            panic!("Oops, tool panicked!");
        });
        assert!(res.is_err());
        let err = res.unwrap_err();
        assert!(err.to_string().contains("Panic"));
    }

    #[test]
    fn test_runtime_isolation_timeout() {
        let runtime = SessionRuntime::new().unwrap();
        let res = runtime.block_on_with_timeout(
            async {
                tokio::time::sleep(Duration::from_millis(200)).await;
                42
            },
            Duration::from_millis(50),
        );
        assert!(res.is_err());
        let err = res.unwrap_err();
        assert!(err.to_string().contains("timed out"));
    }

    #[test]
    fn block_on_inside_current_thread_runtime_errors_instead_of_panicking() {
        let session = SessionRuntime::new().unwrap();
        let outer = tokio::runtime::Builder::new_current_thread()
            .build()
            .unwrap();
        let res = outer.block_on(async { session.block_on(async { 1 }) });
        let err = res.expect_err("current-thread caller must get an error");
        assert!(err.to_string().contains("current-thread"), "{err}");
    }

    #[test]
    fn block_on_inside_multi_thread_runtime_completes() {
        let session = SessionRuntime::new().unwrap();
        let outer = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(1)
            .build()
            .unwrap();
        let res = outer.block_on(async {
            tokio::spawn(async move { session.block_on(async { 7 }) })
                .await
                .expect("worker task must not panic")
        });
        assert_eq!(res.unwrap(), 7);
    }

    #[test]
    fn test_path_traversal_sanitization() {
        fn sanitize_path(
            base: &std::path::Path,
            input: &str,
        ) -> Result<std::path::PathBuf, String> {
            let path = base.join(input);
            if input.contains("..") {
                let normalized =
                    path.components()
                        .fold(std::path::PathBuf::new(), |mut acc, comp| {
                            match comp {
                                std::path::Component::ParentDir => {
                                    acc.pop();
                                }
                                std::path::Component::Normal(c) => {
                                    acc.push(c);
                                }
                                _ => {}
                            }
                            acc
                        });
                if !normalized.starts_with(base) {
                    return Err("Path traversal attempt out of workspace".to_string());
                }
            }
            Ok(path)
        }

        let base = std::env::current_dir().unwrap();
        assert!(sanitize_path(&base, "../../../etc/passwd").is_err());
        assert!(sanitize_path(&base, "src/lib.rs").is_ok());
    }
}
