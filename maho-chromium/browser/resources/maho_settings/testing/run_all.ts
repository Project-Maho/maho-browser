import { spawn } from "child_process";
import * as path from "path";

const tests = [
  "e2e_auth_billing_flow.ts",
  "e2e_pane_profiles.ts",
  "e2e_pane_passwords.ts",
  "e2e_pane_shortcuts.ts",
  "e2e_shortcuts_recording.ts",
  "e2e_shortcuts_import_export.ts",
  "e2e_pane_extensions.ts",
  "e2e_pane_content_blocker.ts",
  "e2e_pane_ai.ts",
  "e2e_pane_account_sync.ts",
  "e2e_pane_autofill.ts",
  "e2e_pane_atc.ts",
  "e2e_pane_chromium_settings.ts",
  "e2e_pane_schema_live.ts",
  "e2e_error_paths.ts"
];

async function runTest(script: string): Promise<number> {
  console.log(`\n\x1b[35m>>> RUNNING TEST: ${script} <<<\x1b[0m`);
  return new Promise((resolve) => {
    const scriptPath = path.join(__dirname, script);
    const child = spawn("bun", [scriptPath], { stdio: "inherit" });
    child.on("close", (code) => {
      resolve(code || 0);
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
