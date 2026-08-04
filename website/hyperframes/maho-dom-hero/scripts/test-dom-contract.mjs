import { copyFileSync, linkSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { assert } from "./contract-lib.mjs";

const projectRoot = resolve(fileURLToPath(new URL("..", import.meta.url)));
const verifier = resolve(projectRoot, "scripts", "verify-dom-contract.mjs");
const canonical = resolve(projectRoot, "compositions", "spaces.html");
const root = mkdtempSync(resolve(tmpdir(), "maho-dom-contract-"));

function expectFailure(name, source) {
  const fixture = resolve(root, `${name}.html`);
  writeFileSync(fixture, source);
  const result = spawnSync(process.execPath, [verifier, "--fixture", fixture], { encoding: "utf8" });
  assert(result.status !== 0, `${name} negative control unexpectedly passed`);
}

try {
  const valid = readFileSync(canonical, "utf8");
  expectFailure("parent-path", valid.replace("node_modules/gsap", "../node_modules/gsap"));
  expectFailure("root-class", valid.replace('id="root"', 'id="root" class="root-shell"').replace("#root {", ".root-shell {"));
  expectFailure("remote-url", valid.replace("node_modules/gsap", "https://example.invalid/gsap"));
  expectFailure("video", valid.replace("</body>", "<video src=\"assets/forbidden.mp4\"></video></body>"));
  expectFailure("capture-image", valid.replace("</body>", "<img src=\"assets/screenshot.png\" /></body>"));
  expectFailure("active-timeline", valid.replace("paused: true", "paused: false"));

  const copiedRoot = resolve(root, "index.html");
  copyFileSync(canonical, copiedRoot);
  const copied = spawnSync(process.execPath, [verifier, "--entry-root", copiedRoot], {
    cwd: root,
    encoding: "utf8",
    env: { ...process.env, MAHO_DOM_ENTRY_ID: "spaces" },
  });
  assert(copied.status !== 0, "copied entry root negative control unexpectedly passed");

  const linkedRoot = resolve(root, "linked-index.html");
  linkSync(canonical, linkedRoot);
  const linked = spawnSync(process.execPath, [verifier, "--entry-root", linkedRoot], {
    cwd: root,
    encoding: "utf8",
    env: { ...process.env, MAHO_DOM_ENTRY_ID: "spaces" },
  });
  assert(linked.status !== 0, "hard-link control must require disposable local asset symlinks");
  console.log(JSON.stringify({ result: "PASS", negativeControls: 8 }));
} catch (error) {
  console.error(`DOM contract negative controls failed: ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
} finally {
  rmSync(root, { force: true, recursive: true });
}
