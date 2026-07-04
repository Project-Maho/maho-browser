//! Spike crate to validate rmcp server+client feature coexistence in the workspace.
//!
//! This crate exists ONLY to prove that `rmcp = { features = ["server", "transport-io"] }`
//! can coexist with `maho-agent`'s `rmcp = { features = ["client"] }` after Cargo
//! unifies features across the workspace.

/// Placeholder — real implementation comes in Wave 2.
pub fn placeholder() {}

#[cfg(test)]
mod tests {
    #[test]
    fn rmcp_server_transport_compiles() {
        // Exercise server-side types to prove the feature gate compiled.
        // In rmcp 1.7, ServiceExt requires a ServiceRole generic.
        use rmcp::service::RoleServer;
        use rmcp::ServiceExt;

        // Prove that the server role + ServiceExt trait resolves
        fn _assert_service_ext<T: ServiceExt<RoleServer>>() {}

        // Prove transport-io feature provides the stdio function
        // In rmcp 1.7, `rmcp::transport::stdio` is a function, not a module
        let _: fn() -> _ = rmcp::transport::stdio;
    }
}
