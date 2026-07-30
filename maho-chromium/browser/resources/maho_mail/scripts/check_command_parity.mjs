// Command-parity guard (plan T15): every command the WebUI calls via
// `callBackend('X', ...)` MUST have a matching arm in the mail helper's
// CallBackend string dispatch. A mismatch means a "dead" command that throws
// at runtime ("Unknown command or not implemented"). Run in CI.
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const dir = path.dirname(fileURLToPath(import.meta.url));
const apiFile = path.join(dir, "../react/api/index.ts");
const helperFile = path.join(dir, "../../../mail_helper/maho_mail_helper_main.cc");

const api = readFileSync(apiFile, "utf8");
const helper = readFileSync(helperFile, "utf8");

const frontendCmds = new Set(
  [...api.matchAll(/callBackend(?:<[^>]*>)?\(\s*'([A-Za-z0-9_]+)'/g)].map((m) => m[1]),
);
const backendCmds = new Set(
  [...helper.matchAll(/command == "([A-Za-z0-9_]+)"/g)].map((m) => m[1]),
);

const dead = [...frontendCmds].filter((c) => !backendCmds.has(c)).sort();

if (dead.length > 0) {
  console.error(
    `command-parity FAIL: ${dead.length} frontend callBackend command(s) have no backend dispatch arm:\n  ${dead.join("\n  ")}`,
  );
  process.exit(1);
}
console.log(
  `command-parity OK: all ${frontendCmds.size} frontend callBackend commands are dispatched by the helper (${backendCmds.size} arms).`,
);
