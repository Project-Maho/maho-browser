import { spawn } from "child_process";
import * as path from "path";

const tests = [
  "static_scope_contract.ts",
  "static_complex_profile_guard_contract.ts",
  "static_partial_profile_row_guard_contract.ts",
  "profile_target_guard.test.ts",
  "static_search_engine_settings_contract.ts",
  "e2e_auth_billing_flow.ts",
  "e2e_pane_profiles.ts",
  "e2e_pane_passwords.ts",
  "e2e_pane_password_provider_dropdown.ts",
  "e2e_pane_password_import.ts",
  "e2e_pane_passwords_ia_split.ts",
  "e2e_pane_shortcuts.ts",
  "e2e_shortcuts_recording.ts",
  "e2e_shortcuts_import_export.ts",
  "e2e_pane_content_blocker.ts",
  "e2e_pane_ai.ts",
  "e2e_pane_ai_hydration.ts",
  "e2e_pane_account_sync.ts",
  "e2e_pane_autofill.ts",
  "e2e_pane_atc.ts",
  "e2e_pane_mail_accounts.ts",
  "e2e_pane_mail_signatures.ts",
  "e2e_pane_mail_rules.ts",
  "e2e_pane_mail_calendar.ts",
  "e2e_pane_mail_behavior.ts",
  "e2e_pane_mail_security.ts",
  "e2e_pane_chromium_settings.ts",
  "e2e_pane_schema_live.ts",
  "e2e_error_paths.ts",
  "e2e_slider_persist.ts",
  "e2e_billing_error.ts"
];

async function runTest(script: string): Promise<number> {
  console.log(`\n\x1b[35m>>> RUNNING TEST: ${script} <<<\x1b[0m`);
  return new Promise((resolve) => {
    const scriptPath = path.join(__dirname, script);
    const child = spawn("bun", [scriptPath], { stdio: "inherit" });
    let settled = false;
    const finish = (code: number) => {
      if (settled) {
        return;
      }
      settled = true;
      resolve(code);
    };

    child.once("error", (error) => {
      console.error(`\x1b[31mFailed to start ${script}: ${error.message}\x1b[0m`);
      finish(1);
    });
    child.once("close", (code, signal) => {
      if (signal !== null) {
        console.error(`\x1b[31mTest ${script} terminated by signal ${signal}\x1b[0m`);
        finish(1);
        return;
      }
      if (code === null) {
        console.error(`\x1b[31mTest ${script} closed without an exit code\x1b[0m`);
        finish(1);
        return;
      }
      finish(code);
    });
  });
}

async function main() {
  let failedTests = 0;
  for (const t of tests) {
    const code = await runTest(t);
    if (code !== 0) {
      console.error(`\x1b[31mTest ${t} failed with exit code ${code}\x1b[0m`);
      failedTests++;
    }
  }

  console.log("\n\x1b[36m=============================\x1b[0m");
  if (failedTests === 0) {
    console.log("\x1b[32mAll E2E settings tests passed!\x1b[0m");
    process.exit(0);
  } else {
    console.error(`\x1b[31m${failedTests} settings test(s) failed.\x1b[0m`);
    process.exit(1);
  }
}

main();
