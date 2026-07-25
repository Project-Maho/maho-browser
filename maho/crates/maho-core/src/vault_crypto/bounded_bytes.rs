use std::fmt;

use serde::de::{SeqAccess, Visitor};

use super::MAX_WRAPPED_KEY_CIPHERTEXT_LEN;

const MAX_WRAPPED_NONCE_LEN: usize = 64;
const MAX_WRAPPED_TAG_LEN: usize = 64;

struct BoundedBytesVisitor<const MAX: usize>;

impl<'de, const MAX: usize> Visitor<'de> for BoundedBytesVisitor<MAX> {
    type Value = Vec<u8>;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "at most {MAX} bytes")
    }

    fn visit_seq<A>(self, mut sequence: A) -> Result<Self::Value, A::Error>
    where
        A: SeqAccess<'de>,
    {
        let mut bytes = Vec::with_capacity(sequence.size_hint().unwrap_or(0).min(MAX));
        while let Some(byte) = sequence.next_element()? {
            if bytes.len() == MAX {
                return Err(serde::de::Error::invalid_length(MAX + 1, &self));
            }
            bytes.push(byte);
        }
        Ok(bytes)
    }
}

fn deserialize_bounded_bytes<'de, D, const MAX: usize>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    deserializer.deserialize_seq(BoundedBytesVisitor::<MAX>)
}

pub(super) fn nonce<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    deserialize_bounded_bytes::<D, MAX_WRAPPED_NONCE_LEN>(deserializer)
}

pub(super) fn ciphertext<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    deserialize_bounded_bytes::<D, MAX_WRAPPED_KEY_CIPHERTEXT_LEN>(deserializer)
}

pub(super) fn tag<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    deserialize_bounded_bytes::<D, MAX_WRAPPED_TAG_LEN>(deserializer)
}
