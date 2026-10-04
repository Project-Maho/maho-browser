//! Todo 11 durable Vault CRUD surface on `MahoCore`, split by concern to keep
//! each unit within the file-size budget. All operations require an unlocked
//! Vault, treat storage as the source of truth, and never retain plaintext.

mod add;
mod audit;
mod mutate;
mod parity;
mod policy;
mod query;
mod shared;
mod sync;
mod use_secret;

pub(crate) use sync::{vault_item_storage_row_from_dto, vault_item_sync_dto_from_row};

#[cfg(test)]
mod tests;
