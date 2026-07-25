//! Todo 11 durable Vault CRUD surface on `MahoCore`, split by concern to keep
//! each unit within the file-size budget. All operations require an unlocked
//! Vault, treat storage as the source of truth, and never retain plaintext.

mod add;
mod audit;
mod mutate;
mod policy;
mod query;
mod shared;
mod use_secret;

#[cfg(test)]
mod tests;
