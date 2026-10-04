// Phase 0 validation for the subresource ad-block coverage fix
// (.omo/plans/content-blocking-subresource-adblock-coverage-2026-07-24.md).
//
// Purpose: prove the native filter engine returns a BLOCK decision for the exact
// request-type strings the browser-side throttle emits from
// MapRequestDestination() in maho_ad_block_throttle.cc — script, image,
// stylesheet, font, subdocument, object, media, document, and the "other"
// fallback used for fetch/XHR/beacon. If the engine blocks for all of these,
// then the observed "subresources are not blocked" behavior is purely a missing
// ENFORCEMENT hook (renderer subresources never reach the browser-side throttle),
// not an engine or request-type-mapping defect.

use maho_core::content_blocker::compile_engine_snapshot;
use maho_core::maho_core::MahoCore;
use maho_types::content_blocking::ContentBlockingMode;

// Every distinct request-type string MapRequestDestination() can emit today.
const RUNTIME_REQUEST_TYPES: &[&str] = &[
    "script",
    "image",
    "stylesheet",
    "font",
    "document",
    "subdocument",
    "object",
    "media",
    "other",
];

fn native_core_with_generic_block_rule() -> MahoCore {
    let mut core = MahoCore::new();
    assert!(core.add_filter_list(
        "probe".into(),
        "Phase0 Probe".into(),
        "https://filters.example/probe.txt".into(),
    ));
    // A generic network rule (no type restriction) must block every resource type.
    core.update_filter_list_content("probe", "||ads.example^\n".into());
    assert!(
        core.install_content_blocker_compiled_engine(compile_engine_snapshot(
            core.create_content_blocker_compile_snapshot(),
        ))
    );
    core.set_content_blocking_mode(ContentBlockingMode::Native);
    core
}

#[test]
fn generic_rule_blocks_every_runtime_request_type_in_native_mode() {
    let core = native_core_with_generic_block_rule();
    for request_type in RUNTIME_REQUEST_TYPES {
        assert!(
            core.should_block_request("https://ads.example/x", "https://site.test", request_type),
            "engine must block a generic ||ads.example^ rule for request type {request_type:?}; \
             if this fails the request-type mapping (not just the enforcement hook) needs fixing"
        );
    }
}

#[test]
fn disabled_mode_blocks_nothing_even_with_rule_loaded() {
    let mut core = native_core_with_generic_block_rule();
    core.set_content_blocking_mode(ContentBlockingMode::Disabled);
    for request_type in RUNTIME_REQUEST_TYPES {
        assert!(
            !core.should_block_request("https://ads.example/x", "https://site.test", request_type),
            "disabled mode must not block request type {request_type:?}"
        );
    }
}

#[test]
fn non_matching_control_request_is_never_blocked_in_native_mode() {
    let core = native_core_with_generic_block_rule();
    for request_type in RUNTIME_REQUEST_TYPES {
        assert!(
            !core.should_block_request(
                "https://not-an-ad.example/x",
                "https://site.test",
                request_type
            ),
            "control URL must load for request type {request_type:?}"
        );
    }
}
