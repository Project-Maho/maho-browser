use std::pin::Pin;
use std::task::{Context, Poll};

use futures_util::Stream;
use tokio::sync::mpsc;

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

#[cfg(test)]
mod tests {
    use super::*;
    use futures_util::StreamExt;

    #[tokio::test]
    async fn test_stream_receives_items() {
        let (tx, rx) = mpsc::unbounded_channel();
        let mut stream = channel_to_stream(rx);

        tx.send("hello").expect("send should succeed");
        tx.send("world").expect("send should succeed");
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
}
