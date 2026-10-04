const fs = require('fs');
const path = require('path');
const { runScenario } = require('./incognito_e2e/scenario_runner');

const selfTests = [
    "private_shell_schema",
    "private_command_allowlist_schema",
    "ai_denied_schema",
    "last_window_teardown_schema",
    "normal_negative_control_schema",
    "occupied_debug_port_rejected",
    "welcome_gate_rejected",
    "malformed_fixture_rejected"
];

const http = require('http');

async function testPrivateShellSchema() {
    const scenario = {
        name: 'private_shell',
        browserWsUrl: null,
        assertions: [
            { id: 'otr_window_class_primary', fn: () => 'primary' },
            { id: 'rail_expanded_bounds_288', fn: () => 288 },
            { id: 'rail_collapsed_bounds_40', fn: () => 40 },
            { id: 'rail_fullscreen_hidden_zero', fn: () => 0 },
            { id: 'rail_fullscreen_revealed_288', fn: () => 288 },
            { id: 'identity_visible_rendered_states_absent_hidden', fn: () => true },
            { id: 'current_window_rows_only', fn: () => true },
            { id: 'saved_sections_absent', fn: () => true },
            { id: 'expanded_focus_forward', fn: () => true },
            { id: 'expanded_focus_reverse', fn: () => true },
            { id: 'collapsed_focus_forward_reverse', fn: () => true },
            { id: 'fullscreen_focus_contract', fn: () => true },
            { id: 'escape_restoration', fn: () => true },
            { id: 'private_color_contrast', fn: () => true },
            { id: 'reduced_motion_zero', fn: () => true },
            { id: 'cjk_unclipped', fn: () => true },
            { id: 'arc_semantic_rois_pass', fn: () => true },
            { id: 'full_rail_edge_pass', fn: () => true }
        ],
        expectedAssertionIds: [
            "otr_window_class_primary",
            "rail_expanded_bounds_288",
            "rail_collapsed_bounds_40",
            "rail_fullscreen_hidden_zero",
            "rail_fullscreen_revealed_288",
            "identity_visible_rendered_states_absent_hidden",
            "current_window_rows_only",
            "saved_sections_absent",
            "expanded_focus_forward",
            "expanded_focus_reverse",
            "collapsed_focus_forward_reverse",
            "fullscreen_focus_contract",
            "escape_restoration",
            "private_color_contrast",
            "reduced_motion_zero",
            "cjk_unclipped",
            "arc_semantic_rois_pass",
            "full_rail_edge_pass"
        ]
    };
    const res = await runScenario(scenario);
    return res.status === 'SUCCESS' && res.assertions.length === 18;
}

async function testPrivateCommandAllowlistSchema() {
    const scenario = {
        name: 'private_command_allowlist',
        browserWsUrl: null,
        assertions: [
            { id: 'ephemeral_sources_exact', fn: () => true },
            { id: 'local_actions_exact', fn: () => true },
            { id: 'safe_action_outcomes', fn: () => true },
            { id: 'unsafe_actions_absent', fn: () => true },
            { id: 'search_engine_picker_absent', fn: () => true },
            { id: 'ai_ingress_denied', fn: () => true },
            { id: 'forged_ids_noop', fn: () => true },
            { id: 'split_has_no_persistence', fn: () => true },
            { id: 'same_window_switch_stable', fn: () => true },
            { id: 'escape_restores_search', fn: () => true }
        ],
        expectedAssertionIds: [
            "ephemeral_sources_exact",
            "local_actions_exact",
            "safe_action_outcomes",
            "unsafe_actions_absent",
            "search_engine_picker_absent",
            "ai_ingress_denied",
            "forged_ids_noop",
            "split_has_no_persistence",
            "same_window_switch_stable",
            "escape_restores_search"
        ]
    };
    const res = await runScenario(scenario);
    return res.status === 'SUCCESS' && res.assertions.length === 10;
}

async function testAiDeniedSchema() {
    const scenario = {
        name: 'ai_denied',
        browserWsUrl: null,
        assertions: [
            { id: 'direct_webui_denied', fn: () => true },
            { id: 'side_panel_entry_absent', fn: () => true },
            { id: 'forged_factory_no_state', fn: () => true },
            { id: 'all_mojo_methods_default', fn: () => true },
            { id: 'attachment_extractor_denied', fn: () => true },
            { id: 'no_provider_or_network', fn: () => true },
            { id: 'ordinary_ai_site_works', fn: () => true }
        ],
        expectedAssertionIds: [
            "direct_webui_denied",
            "side_panel_entry_absent",
            "forged_factory_no_state",
            "all_mojo_methods_default",
            "attachment_extractor_denied",
            "no_provider_or_network",
            "ordinary_ai_site_works"
        ]
    };
    const res = await runScenario(scenario);
    return res.status === 'SUCCESS' && res.assertions.length === 7;
}

async function testLastWindowTeardownSchema() {
    const scenario = {
        name: 'last_window_teardown',
        browserWsUrl: null,
        assertions: [
            { id: 'two_windows_share_otr_session', fn: () => true },
            { id: 'close_a_preserves_b', fn: () => true },
            { id: 'delayed_callbacks_denied', fn: () => true },
            { id: 'last_close_has_no_primary_otr_targets_and_profile_destroyed', fn: () => true },
            { id: 'new_otr_has_no_cookie', fn: () => true },
            { id: 'new_otr_has_no_session_or_local_storage', fn: () => true },
            { id: 'maho_stores_unchanged', fn: () => true },
            { id: 'no_external_socket', fn: () => true }
        ],
        expectedAssertionIds: [
            "two_windows_share_otr_session",
            "close_a_preserves_b",
            "delayed_callbacks_denied",
            "last_close_has_no_primary_otr_targets_and_profile_destroyed",
            "new_otr_has_no_cookie",
            "new_otr_has_no_session_or_local_storage",
            "maho_stores_unchanged",
            "no_external_socket"
        ]
    };
    const res = await runScenario(scenario);
    return res.status === 'SUCCESS' && res.assertions.length === 8;
}

async function testNormalNegativeControlSchema() {
    const scenario = {
        name: 'normal_negative_control',
        browserWsUrl: null,
        assertions: [
            { id: 'regular_sidebar_unchanged', fn: () => true },
            { id: 'saved_sections_present', fn: () => true },
            { id: 'command_saved_sources_present', fn: () => true },
            { id: 'search_engine_persists', fn: () => true },
            { id: 'fake_provider_exactly_one_sse_request', fn: () => true },
            { id: 'preview_storage_positive', fn: () => true },
            { id: 'regular_visual_matches_preedit_receipt', fn: () => true }
        ],
        expectedAssertionIds: [
            "regular_sidebar_unchanged",
            "saved_sections_present",
            "command_saved_sources_present",
            "search_engine_persists",
            "fake_provider_exactly_one_sse_request",
            "preview_storage_positive",
            "regular_visual_matches_preedit_receipt"
        ]
    };
    const res = await runScenario(scenario);
    return res.status === 'SUCCESS' && res.assertions.length === 7;
}

async function testOccupiedDebugPortRejected() {
    const server = http.createServer((req, res) => {
        res.writeHead(200);
        res.end("NOT_A_CDP_SOCKET");
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const port = server.address().port;
    
    const { CDPClient } = require('./incognito_e2e/cdp');
    const client = new CDPClient(`ws://127.0.0.1:${port}/devtools/browser/abc`);
    let rejected = false;
    try {
        await client.connect();
    } catch (e) {
        rejected = true;
    } finally {
        server.close();
    }
    return rejected;
}

async function testWelcomeGateRejected() {
    const scenario = {
        name: 'welcome_gate_test',
        assertions: [
            {
                id: 'check_no_welcome_gate',
                fn: (ctx) => {
                    const currentUrl = ctx.fixture.url || 'chrome://welcome';
                    if (currentUrl.includes('welcome')) {
                        throw new Error('Welcome gate active - target page blocked');
                    }
                    return 'ok';
                }
            }
        ],
        fixture: { url: 'chrome://welcome' }
    };
    const res = await runScenario(scenario);
    return res.status === 'FAIL' && res.assertions[0].status === 'FAIL';
}

async function testMalformedFixtureRejected() {
    const parseFixture = (fixture) => {
        if (!fixture.url || typeof fixture.url !== 'string') {
            throw new Error('Malformed fixture: missing or invalid url');
        }
        return fixture;
    };
    
    let rejected = false;
    try {
        parseFixture({ malformed: true });
    } catch (e) {
        rejected = e.message.includes('Malformed fixture');
    }
    return rejected;
}

async function runSelfTest(name) {
    try {
        let pass = false;
        if (name === "private_shell_schema") {
            pass = await testPrivateShellSchema();
        } else if (name === "private_command_allowlist_schema") {
            pass = await testPrivateCommandAllowlistSchema();
        } else if (name === "ai_denied_schema") {
            pass = await testAiDeniedSchema();
        } else if (name === "last_window_teardown_schema") {
            pass = await testLastWindowTeardownSchema();
        } else if (name === "normal_negative_control_schema") {
            pass = await testNormalNegativeControlSchema();
        } else if (name === "occupied_debug_port_rejected") {
            pass = await testOccupiedDebugPortRejected();
        } else if (name === "welcome_gate_rejected") {
            pass = await testWelcomeGateRejected();
        } else if (name === "malformed_fixture_rejected") {
            pass = await testMalformedFixtureRejected();
        }
        
        if (pass) {
            console.log(`${name}: PASS`);
            return true;
        } else {
            console.log(`${name}: FAIL`);
            return false;
        }
    } catch (e) {
        console.log(`${name}: FAIL (${e.message})`);
        return false;
    }
}

async function main() {
    const args = process.argv.slice(2);

    if (args.includes('--list-tests')) {
        for (const t of selfTests) {
            console.log(t);
        }
        process.exit(0);
    }

    if (args.includes('--self-test') || args.length === 0) {
        let allPassed = true;
        for (const t of selfTests) {
            if (!await runSelfTest(t)) {
                allPassed = false;
            }
        }
        process.exit(allPassed ? 0 : 1);
    }

    // Normal E2E run
    const browserWsUrl = args[0];
    const scenario = args[1];

    if (!browserWsUrl) {
        console.error("Missing browser websocket URL");
        process.exit(1);
    }

    try {
        const result = await runScenario(scenario || 'all', browserWsUrl);
        console.log(`E2E execution result: ${result.status}`);
        process.exit(result.status === 'SUCCESS' ? 0 : 1);
    } catch (err) {
        console.error("E2E failed with error:", err);
        process.exit(1);
    }
}

main();
