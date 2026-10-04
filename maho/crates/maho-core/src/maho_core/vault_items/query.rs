use maho_types::vault::{VaultItemListRequest, VaultItemPublicDto, VaultItemSearchRequest};

use crate::maho_core::MahoCore;
use crate::vault_manager::{OriginMatchPolicy, VaultCrudError};

impl MahoCore {
    pub fn vault_list_items(
        &self,
        request: &VaultItemListRequest,
    ) -> Result<Vec<VaultItemPublicDto>, VaultCrudError> {
        self.vault_runtime
            .list_items(self.vault_repository().map_err(storage_error)?, request)
    }

    pub fn vault_search_items(
        &self,
        request: &VaultItemSearchRequest,
    ) -> Result<Vec<VaultItemPublicDto>, VaultCrudError> {
        self.vault_runtime
            .search_items(self.vault_repository().map_err(storage_error)?, request)
    }

    pub fn vault_match_items(
        &self,
        request: &VaultItemSearchRequest,
        policy: OriginMatchPolicy,
    ) -> Result<Vec<VaultItemPublicDto>, VaultCrudError> {
        self.vault_runtime.match_items(
            self.vault_repository().map_err(storage_error)?,
            request,
            policy,
        )
    }
}

fn storage_error(error: maho_storage::StorageError) -> VaultCrudError {
    VaultCrudError::Storage(error.to_string())
}
