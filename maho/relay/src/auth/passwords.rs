use argon2::password_hash::rand_core::{CryptoRng, Error as RandError, RngCore};
use argon2::password_hash::{PasswordHash, SaltString};
use argon2::{Argon2, PasswordHasher, PasswordVerifier};
use hmac::{Hmac, Mac};
use sha2::Sha256;

#[derive(Debug)]
pub enum PasswordError {
    HmacError,
    HashError(argon2::password_hash::Error),
    Argon2Error(argon2::Error),
}

impl std::fmt::Display for PasswordError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PasswordError::HmacError => write!(f, "HMAC derivation failed"),
            PasswordError::HashError(e) => write!(f, "Argon2 hash error: {e}"),
            PasswordError::Argon2Error(e) => write!(f, "Argon2 error: {e}"),
        }
    }
}

impl std::error::Error for PasswordError {}

impl From<argon2::password_hash::Error> for PasswordError {
    fn from(err: argon2::password_hash::Error) -> Self {
        PasswordError::HashError(err)
    }
}

impl From<argon2::Error> for PasswordError {
    fn from(err: argon2::Error) -> Self {
        PasswordError::Argon2Error(err)
    }
}

struct GetRandomRng;

impl RngCore for GetRandomRng {
    fn next_u32(&mut self) -> u32 {
        let mut bytes = [0u8; 4];
        // These trait methods cannot return an error, so a CSPRNG failure must
        // panic (caught by console_error_panic_hook -> 500) rather than proceed
        // with predictable zero bytes for a password salt.
        if let Err(e) = getrandom::getrandom(&mut bytes) {
            panic!("csprng_failure in next_u32: {e}");
        }
        u32::from_le_bytes(bytes)
    }
    fn next_u64(&mut self) -> u64 {
        let mut bytes = [0u8; 8];
        if let Err(e) = getrandom::getrandom(&mut bytes) {
            panic!("csprng_failure in next_u64: {e}");
        }
        u64::from_le_bytes(bytes)
    }
    fn fill_bytes(&mut self, dest: &mut [u8]) {
        if let Err(e) = getrandom::getrandom(dest) {
            panic!("csprng_failure in fill_bytes: {e}");
        }
    }
    fn try_fill_bytes(&mut self, dest: &mut [u8]) -> std::result::Result<(), RandError> {
        getrandom::getrandom(dest).map_err(|e| RandError::from(e.code()))
    }
}

impl CryptoRng for GetRandomRng {}

fn get_peppered_password(password: &str, pepper: &[u8]) -> Result<Vec<u8>, PasswordError> {
    let mut mac = Hmac::<Sha256>::new_from_slice(pepper).map_err(|_| PasswordError::HmacError)?;
    mac.update(password.as_bytes());
    Ok(mac.finalize().into_bytes().to_vec())
}

fn get_argon2_instance() -> Result<Argon2<'static>, PasswordError> {
    let params = argon2::Params::new(
        19456, // m_cost (memory KiB)
        2,     // t_cost (iterations)
        1,     // p_cost (parallelism)
        None,  // output_len
    )?;
    Ok(Argon2::new(
        argon2::Algorithm::Argon2id,
        argon2::Version::V0x13,
        params,
    ))
}

pub fn hash_password(password: &str, pepper: &[u8]) -> Result<String, PasswordError> {
    let peppered = get_peppered_password(password, pepper)?;
    let salt = SaltString::generate(&mut GetRandomRng);
    let argon2 = get_argon2_instance()?;
    let hash = argon2.hash_password(&peppered, &salt)?.to_string();
    Ok(hash)
}

pub fn verify_password(
    password: &str,
    password_hash: &str,
    pepper: &[u8],
) -> Result<bool, PasswordError> {
    let peppered = get_peppered_password(password, pepper)?;
    let parsed_hash = PasswordHash::new(password_hash)?;
    let argon2 = get_argon2_instance()?;
    Ok(argon2.verify_password(&peppered, &parsed_hash).is_ok())
}
