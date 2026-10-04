pub const PLATFORM_PERMISSION_REQUIRED: &str = "platform_permission_required";
pub const PLATFORM_UNSUPPORTED: &str = "platform_unsupported";
pub const PLATFORM_TYPED_APPROVAL_REQUIRED: &str = "platform_typed_approval_required";

pub const DEEP_LINK_FDA: &str =
    "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_AllFiles";
pub const DEEP_LINK_CONTACTS: &str =
    "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Contacts";
pub const DEEP_LINK_ACCESSIBILITY: &str =
    "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Accessibility";
pub const DEEP_LINK_AUTOMATION: &str =
    "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Automation";

pub const NON_MACOS_REMEDIATION: &str = "native backend not available; contacts: Microsoft Graph People API or Google Contacts sync; messaging: platform SMS integration";

#[derive(Debug, thiserror::Error)]
pub enum PlatformError {
    #[error("capability '{capability}' is unimplemented")]
    Unimplemented { capability: &'static str },

    #[error("permission required for scope '{scope}'")]
    PermissionRequired { scope: String, deep_link: String },

    #[error("platform '{platform}' unsupported for capability '{capability}': {remediation}")]
    Unsupported {
        platform: String,
        capability: String,
        remediation: String,
    },

    #[error("typed approval required for action '{action}'")]
    ApprovalRequired { action: String },

    #[error("io error: {0}")]
    Io(String),

    #[error("sqlite error: {0}")]
    Sqlite(String),
}

impl PlatformError {
    pub fn code(&self) -> &'static str {
        match self {
            PlatformError::Unimplemented { .. } => "platform_unimplemented",
            PlatformError::PermissionRequired { .. } => PLATFORM_PERMISSION_REQUIRED,
            PlatformError::Unsupported { .. } => PLATFORM_UNSUPPORTED,
            PlatformError::ApprovalRequired { .. } => PLATFORM_TYPED_APPROVAL_REQUIRED,
            PlatformError::Io(_) => "platform_io",
            PlatformError::Sqlite(_) => "platform_sqlite",
        }
    }
}
