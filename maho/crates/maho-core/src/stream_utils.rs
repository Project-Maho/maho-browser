use std::pin::Pin;
use std::task::{Context, Poll};

use futures_util::Stream;
use tokio::sync::mpsc;

/// Decode the complete UTF-8 prefix, retaining a code point split by transport
/// framing. Invalid (rather than merely incomplete) UTF-8 remains an error.
pub(crate) fn push_utf8_chunk(
    pending: &mut Vec<u8>,
    chunk: &[u8],
) -> Result<String, std::str::Utf8Error> {
    pending.extend_from_slice(chunk);
    let valid_len = match std::str::from_utf8(pending) {
        Ok(_) => pending.len(),
        Err(error) if error.error_len().is_none() => error.valid_up_to(),
        Err(error) => return Err(error),
    };
    let text = std::str::from_utf8(&pending[..valid_len])?.to_string();
    pending.drain(..valid_len);
    Ok(text)
}

pub struct UnboundedReceiverStream<T> {
    rx: mpsc::UnboundedReceiver<T>,
}

impl<T> UnboundedReceiverStream<T> {
    pub fn new(rx: mpsc::UnboundedReceiver<T>) -> Self {
        Self { rx }
    }
}

impl<T> Stream for UnboundedReceiverStream<T> {
    type Item = T;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.rx.poll_recv(cx)
    }
}

pub fn channel_to_stream<T: Send + 'static>(
    rx: mpsc::UnboundedReceiver<T>,
) -> Pin<Box<dyn Stream<Item = T> + Send>> {
    Box::pin(UnboundedReceiverStream::new(rx))
}

/// Stream adapter for bounded `mpsc::Receiver<T>` (audit finding H8).
pub struct ReceiverStream<T> {
    rx: mpsc::Receiver<T>,
}

impl<T> ReceiverStream<T> {
    pub fn new(rx: mpsc::Receiver<T>) -> Self {
        Self { rx }
    }
}

impl<T> Stream for ReceiverStream<T> {
    type Item = T;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.rx.poll_recv(cx)
    }
}

pub fn bounded_channel_to_stream<T: Send + 'static>(
    rx: mpsc::Receiver<T>,
) -> Pin<Box<dyn Stream<Item = T> + Send>> {
    Box::pin(ReceiverStream::new(rx))
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures_util::StreamExt;

    #[tokio::test]
    async fn test_stream_receives_items() {
        let (tx, rx) = mpsc::channel(128);
        let mut stream = bounded_channel_to_stream(rx);

        tx.try_send("hello").expect("send should succeed");
        tx.try_send("world").expect("send should succeed");
        drop(tx);

        let first = stream.next().await;
        assert_eq!(first, Some("hello"));

        let second = stream.next().await;
        assert_eq!(second, Some("world"));

        let done = stream.next().await;
        assert_eq!(done, None);
    }

    #[tokio::test]
    async fn test_stream_closes_on_sender_drop() {
        let (tx, rx) = mpsc::unbounded_channel::<i32>();
        let mut stream = channel_to_stream(rx);
        drop(tx);

        let result = stream.next().await;
        assert_eq!(result, None);
    }

    #[tokio::test]
    async fn bounded_channel_applies_backpressure() {
        let (tx, mut rx) = tokio::sync::mpsc::channel::<u32>(4);
        for i in 0..4 {
            tx.try_send(i).unwrap();
        }
        let err = tx.try_send(99).unwrap_err();
        assert!(matches!(
            err,
            tokio::sync::mpsc::error::TrySendError::Full(_)
        ));
        drop(tx);
        let mut count = 0u32;
        while rx.recv().await.is_some() {
            count += 1;
        }
        assert_eq!(count, 4);
    }
}
