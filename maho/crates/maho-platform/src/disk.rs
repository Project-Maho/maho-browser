use crate::error::PlatformError;
#[cfg(not(target_os = "macos"))]
use crate::error::NON_MACOS_REMEDIATION;
use crate::imessage::FdaStatus;

pub fn status() -> Result<FdaStatus, PlatformError> {
    #[cfg(target_os = "macos")]
    {
        // The disk-scope capability on macOS IS the Full Disk Access probe:
        // the only TCC-protected resource the agent layer touches is
        // ~/Library/Messages/chat.db, so its readability defines the scope.
        crate::imessage::fda_status(None)
    }
    #[cfg(not(target_os = "macos"))]
    {
        Err(PlatformError::Unsupported {
            platform: std::env::consts::OS.to_string(),
            capability: "disk".to_string(),
            remediation: NON_MACOS_REMEDIATION.to_string(),
        })
    }
}
