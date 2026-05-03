use std::collections::HashMap;
use std::time::Instant;

use axum::extract::ws::Message;
use dashmap::DashMap;
use tokio::sync::mpsc;

pub struct RoomManager {
    rooms: DashMap<String, Room>,
    max_connections_per_room: u32,
}

struct Room {
    peers: HashMap<String, mpsc::UnboundedSender<Message>>,
    last_activity: Instant,
}

impl RoomManager {
    pub fn new(max_connections_per_room: u32) -> Self {
        Self {
            rooms: DashMap::new(),
            max_connections_per_room,
        }
    }

    pub fn join(
        &self,
        room_id: &str,
        peer_id: &str,
        sender: mpsc::UnboundedSender<Message>,
    ) -> Result<(), RoomError> {
        let mut room = self.rooms.entry(room_id.to_string()).or_insert_with(|| Room {
            peers: HashMap::new(),
            last_activity: Instant::now(),
        });

        if room.peers.len() as u32 >= self.max_connections_per_room {
            return Err(RoomError::RoomFull);
        }

        room.peers.insert(peer_id.to_string(), sender);
        room.last_activity = Instant::now();
        Ok(())
    }

    pub fn leave(&self, room_id: &str, peer_id: &str) {
        if let Some(mut room) = self.rooms.get_mut(room_id) {
            room.peers.remove(peer_id);
            if room.peers.is_empty() {
                let key = room.key().clone();
                drop(room);
                self.rooms.remove_if(&key, |_, r| r.peers.is_empty());
            }
        }
    }

    pub fn broadcast(&self, room_id: &str, msg: &Message, exclude_peer_id: &str) {
        if let Some(room) = self.rooms.get(room_id) {
            for (pid, sender) in &room.peers {
                if pid != exclude_peer_id {
                    let _ = sender.send(msg.clone());
                }
            }
        }
    }

    #[cfg(test)]
    pub fn peer_count(&self, room_id: &str) -> usize {
        self.rooms
            .get(room_id)
            .map(|r| r.peers.len())
            .unwrap_or(0)
    }
}

#[derive(Debug, PartialEq)]
pub enum RoomError {
    RoomFull,
}

impl std::fmt::Display for RoomError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RoomError::RoomFull => write!(f, "room is full"),
        }
    }
}

impl std::error::Error for RoomError {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_join_and_leave() {
        let mgr = RoomManager::new(5);
        let (tx, _rx) = mpsc::unbounded_channel();

        mgr.join("room1", "peer1", tx.clone()).unwrap();
        assert_eq!(mgr.peer_count("room1"), 1);

        let (tx2, _rx2) = mpsc::unbounded_channel();
        mgr.join("room1", "peer2", tx2).unwrap();
        assert_eq!(mgr.peer_count("room1"), 2);

        mgr.leave("room1", "peer1");
        assert_eq!(mgr.peer_count("room1"), 1);

        mgr.leave("room1", "peer2");
        assert_eq!(mgr.peer_count("room1"), 0);
    }

    #[test]
    fn test_max_connections_enforcement() {
        let mgr = RoomManager::new(2);
        let (tx1, _) = mpsc::unbounded_channel();
        let (tx2, _) = mpsc::unbounded_channel();
        let (tx3, _) = mpsc::unbounded_channel();

        mgr.join("room1", "p1", tx1).unwrap();
        mgr.join("room1", "p2", tx2).unwrap();
        let result = mgr.join("room1", "p3", tx3);
        assert_eq!(result.unwrap_err(), RoomError::RoomFull);
    }

    #[tokio::test]
    async fn test_broadcast() {
        let mgr = RoomManager::new(5);
        let (tx1, mut rx1) = mpsc::unbounded_channel();
        let (tx2, mut rx2) = mpsc::unbounded_channel();

        mgr.join("room1", "p1", tx1).unwrap();
        mgr.join("room1", "p2", tx2).unwrap();

        let msg = Message::Text("hello".into());
        mgr.broadcast("room1", &msg, "p1");

        assert!(rx1.try_recv().is_err());
        let received = rx2.try_recv().unwrap();
        assert_eq!(received, Message::Text("hello".into()));
    }

    #[test]
    fn test_room_isolation() {
        let mgr = RoomManager::new(5);
        let (tx1, _) = mpsc::unbounded_channel();
        let (tx2, _) = mpsc::unbounded_channel();

        mgr.join("room_a", "p1", tx1).unwrap();
        mgr.join("room_b", "p2", tx2).unwrap();

        assert_eq!(mgr.peer_count("room_a"), 1);
        assert_eq!(mgr.peer_count("room_b"), 1);
        assert_eq!(mgr.peer_count("room_c"), 0);
    }

    #[test]
    fn test_leave_last_peer_removes_room() {
        let mgr = RoomManager::new(5);
        let (tx, _rx) = mpsc::unbounded_channel();

        mgr.join("room1", "peer1", tx).unwrap();
        assert_eq!(mgr.peer_count("room1"), 1);

        mgr.leave("room1", "peer1");
        assert_eq!(mgr.peer_count("room1"), 0);

        let (tx2, _rx2) = mpsc::unbounded_channel();
        mgr.join("room1", "peer_new", tx2).unwrap();
        assert_eq!(mgr.peer_count("room1"), 1);
    }

    #[tokio::test]
    async fn test_broadcast_single_peer() {
        let mgr = RoomManager::new(5);
        let (tx1, mut rx1) = mpsc::unbounded_channel();

        mgr.join("room1", "p1", tx1).unwrap();

        let msg = Message::Text("hello".into());
        mgr.broadcast("room1", &msg, "p1");

        assert!(rx1.try_recv().is_err());
    }

    #[test]
    fn test_multiple_rooms_independent() {
        let mgr = RoomManager::new(5);
        let (tx1, _) = mpsc::unbounded_channel();
        let (tx2, _) = mpsc::unbounded_channel();

        mgr.join("room_a", "p1", tx1).unwrap();
        mgr.join("room_b", "p2", tx2).unwrap();

        mgr.leave("room_a", "p1");
        assert_eq!(mgr.peer_count("room_a"), 0);
        assert_eq!(mgr.peer_count("room_b"), 1);
    }
}
