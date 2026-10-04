pub mod ax_tree;
pub mod backends;
pub mod capture;
pub mod contacts;
pub mod desktop;
pub mod disk;
pub mod error;
pub mod find_text;
pub mod grounding;
pub mod imessage;
pub mod linux_probe;
pub mod ocr;
pub mod probe;
pub mod send;
#[cfg(target_os = "windows")]
pub mod win_ax;
#[cfg(target_os = "windows")]
pub mod win_capture;
#[cfg(target_os = "windows")]
pub mod win_ocr;
pub mod win_probe;

pub use contacts::Contact;
pub use disk::status as disk_status;
pub use error::{
    PlatformError, DEEP_LINK_AUTOMATION, DEEP_LINK_CONTACTS, DEEP_LINK_FDA, NON_MACOS_REMEDIATION,
    PLATFORM_PERMISSION_REQUIRED, PLATFORM_TYPED_APPROVAL_REQUIRED, PLATFORM_UNSUPPORTED,
};
pub use grounding::{
    AppTarget, CaptureInfo, FindMatch, FindSource, Region, ScreenElement, TextBlock,
};
pub use imessage::{Chat, FdaStatus, Message, SearchResult};
pub use send::{
    compose_send_applescript, escape_applescript, OsRunner, ProcessRunner, SendRequest,
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_code_mapping() {
        let err_unimplemented = PlatformError::Unimplemented {
            capability: "contacts",
        };
        assert_eq!(err_unimplemented.code(), "platform_unimplemented");

        let err_perm = PlatformError::PermissionRequired {
            scope: "contacts".to_string(),
            deep_link: DEEP_LINK_CONTACTS.to_string(),
        };
        assert_eq!(err_perm.code(), PLATFORM_PERMISSION_REQUIRED);

        let err_unsupported = PlatformError::Unsupported {
            platform: "linux".to_string(),
            capability: "imessage".to_string(),
            remediation: NON_MACOS_REMEDIATION.to_string(),
        };
        assert_eq!(err_unsupported.code(), PLATFORM_UNSUPPORTED);

        let err_approval = PlatformError::ApprovalRequired {
            action: "send_message".to_string(),
        };
        assert_eq!(err_approval.code(), PLATFORM_TYPED_APPROVAL_REQUIRED);

        let err_io = PlatformError::Io("io error".to_string());
        assert_eq!(err_io.code(), "platform_io");

        let err_sqlite = PlatformError::Sqlite("sqlite error".to_string());
        assert_eq!(err_sqlite.code(), "platform_sqlite");
    }
}
