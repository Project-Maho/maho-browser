use std::path::Path;

use ed25519_dalek::{Signature, Signer, SigningKey, VerifyingKey};
use rand::rngs::OsRng;
use tokio::fs;

#[derive(Debug)]
pub enum SignerError {
    Io(std::io::Error),
    InvalidKeyLength(usize),
    PlaceholderKey,
}

impl std::fmt::Display for SignerError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SignerError::Io(e) => write!(f, "io error: {e}"),
            SignerError::InvalidKeyLength(n) => {
                write!(f, "invalid key length: expected 32 bytes, got {n}")
            }
            SignerError::PlaceholderKey => {
                write!(f, "private key is the all-zero placeholder")
            }
        }
    }
}

impl std::error::Error for SignerError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            SignerError::Io(e) => Some(e),
            _ => None,
        }
    }
}

impl From<std::io::Error> for SignerError {
    fn from(e: std::io::Error) -> Self {
        SignerError::Io(e)
    }
}

pub struct UpdateSigner {
    signing_key: SigningKey,
}

impl UpdateSigner {
    pub async fn from_path(path: &Path) -> Result<Self, SignerError> {
        let bytes = fs::read(path).await?;
        Self::from_bytes(&bytes)
    }

    pub fn from_bytes(bytes: &[u8]) -> Result<Self, SignerError> {
        if bytes.len() != 32 {
            return Err(SignerError::InvalidKeyLength(bytes.len()));
        }
        if bytes.iter().all(|b| *b == 0) {
            return Err(SignerError::PlaceholderKey);
        }
        let mut seed = [0u8; 32];
        seed.copy_from_slice(bytes);
        Ok(Self {
            signing_key: SigningKey::from_bytes(&seed),
        })
    }

    pub fn generate() -> (Self, [u8; 32]) {
        let mut csprng = OsRng;
        let signing_key = SigningKey::generate(&mut csprng);
        let seed = signing_key.to_bytes();
        (Self { signing_key }, seed)
    }

    pub fn public_key(&self) -> VerifyingKey {
        self.signing_key.verifying_key()
    }

    pub fn public_key_bytes(&self) -> [u8; 32] {
        self.public_key().to_bytes()
    }

    pub fn sign(&self, payload: &[u8]) -> Signature {
        self.signing_key.sign(payload)
    }

    pub fn sign_hex(&self, payload: &[u8]) -> String {
        hex::encode(self.sign(payload).to_bytes())
    }

    pub fn sign_base64(&self, payload: &[u8]) -> String {
        use base64::Engine;
        base64::engine::general_purpose::STANDARD.encode(self.sign(payload).to_bytes())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::Verifier;

    #[test]
    fn rejects_zero_key() {
        let zeros = [0u8; 32];
        assert!(matches!(
            UpdateSigner::from_bytes(&zeros),
            Err(SignerError::PlaceholderKey)
        ));
    }

    #[test]
    fn rejects_wrong_length() {
        assert!(matches!(
            UpdateSigner::from_bytes(&[1u8; 16]),
            Err(SignerError::InvalidKeyLength(16))
        ));
    }

    #[test]
    fn sign_and_verify_roundtrip() {
        let (signer, _) = UpdateSigner::generate();
        let payload = br#"{"version":"1.2.3"}"#;
        let sig = signer.sign(payload);
        assert!(signer.public_key().verify(payload, &sig).is_ok());
    }

    #[test]
    fn tampered_payload_fails() {
        let (signer, _) = UpdateSigner::generate();
        let payload = br#"{"version":"1.2.3"}"#;
        let sig = signer.sign(payload);
        let tampered = br#"{"version":"9.9.9"}"#;
        assert!(signer.public_key().verify(tampered, &sig).is_err());
    }

    #[test]
    fn from_seed_is_deterministic() {
        let (a, seed) = UpdateSigner::generate();
        let b = UpdateSigner::from_bytes(&seed).unwrap();
        assert_eq!(a.public_key_bytes(), b.public_key_bytes());
    }
}
