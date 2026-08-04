import { spawn } from "node:child_process";
import http from "node:http";
import net from "node:net";

export const CAPTURE_HOST = "127.0.0.1";

function formatOutput(output) {
  return output.trim() || "(no output captured)";
}

function collectOutput(child) {
  let output = "";
  child.stdout?.on("data", (data) => {
    output += data.toString();
  });
  child.stderr?.on("data", (data) => {
    output += data.toString();
  });
  return () => output;
}

function watchChildExit(child) {
  if (child.exitCode !== null || child.signalCode !== null) {
    return Promise.resolve({ code: child.exitCode, signal: child.signalCode });
  }

  return new Promise((resolve, reject) => {
    const onExit = (code, signal) => {
      child.off("error", onError);
      resolve({ code, signal });
    };
    const onError = (error) => {
      child.off("exit", onExit);
      reject(error);
    };
    child.once("exit", onExit);
    child.once("error", onError);
  });
}

async function waitFor(value, timeoutMs) {
  let timeout;
  const timeoutResult = new Promise((resolve) => {
    timeout = setTimeout(() => resolve(null), timeoutMs);
  });
  return Promise.race([value, timeoutResult]).finally(() => clearTimeout(timeout));
}

function signalProcessGroup(child, signal) {
  if (child.pid === undefined || child.exitCode !== null || child.signalCode !== null) {
    return;
  }

  if (process.platform !== "win32") {
    try {
      process.kill(-child.pid, signal);
      return;
    } catch (error) {
      if (error && typeof error === "object" && error.code === "ESRCH") {
        return;
      }
      throw error;
    }
  }

  if (!child.kill(signal) && child.exitCode === null && child.signalCode === null) {
    throw new Error(`Unable to send ${signal} to preview process ${child.pid}`);
  }
}

export async function getFreePort() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen({ host: CAPTURE_HOST, port: 0 }, resolve);
  });
  const address = server.address();
  if (address === null || typeof address === "string") {
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    throw new Error("Unable to determine an IPv4 preview port");
  }
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return address.port;
}

export async function runCommand({ command, args, cwd }) {
  const child = spawn(command, args, { cwd, stdio: ["ignore", "pipe", "pipe"] });
  const output = collectOutput(child);
  const exit = await watchChildExit(child);
  if (exit.code !== 0) {
    throw new Error(
      `${command} ${args.join(" ")} exited with exit code ${exit.code ?? "null"} (${exit.signal ?? "no signal"}):\n${formatOutput(output())}`,
    );
  }
  return output();
}

export function startPreview({ cwd, port }) {
  const child = spawn(
    "bun",
    ["run", "preview", "--", "--host", CAPTURE_HOST, "--port", String(port)],
    { cwd, detached: process.platform !== "win32", stdio: ["ignore", "pipe", "pipe"] },
  );
  const output = collectOutput(child);
  return { child, exit: watchChildExit(child), output };
}

function probeCaptureRoute(port) {
  return new Promise((resolve) => {
    const request = http.get(
      { hostname: CAPTURE_HOST, port, path: "/capture/maho-feature/", timeout: 1_000 },
      (response) => {
        response.resume();
        resolve(response.statusCode === 200);
      },
    );
    request.once("error", () => resolve(false));
    request.once("timeout", () => {
      request.destroy();
      resolve(false);
    });
  });
}

export async function waitForCaptureRoute({ preview, port, timeoutMs = 10_000 }) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await probeCaptureRoute(port)) {
      return;
    }
    const outcome = await waitFor(preview.exit, 100);
    if (outcome !== null) {
      throw new Error(`Preview exited before capture route was ready:\n${formatOutput(preview.output())}`);
    }
  }
  throw new Error(`Preview route did not become ready:\n${formatOutput(preview.output())}`);
}

export async function stopProcessGroup({ child, timeoutMs = 5_000 }) {
  if (child.pid === undefined) {
    return { code: child.exitCode, signal: child.signalCode };
  }
  const exit = watchChildExit(child);
  signalProcessGroup(child, "SIGTERM");
  const gracefulExit = await waitFor(exit, timeoutMs);
  if (gracefulExit !== null) {
    return gracefulExit;
  }
  signalProcessGroup(child, "SIGKILL");
  const forcedExit = await waitFor(exit, timeoutMs);
  if (forcedExit === null) {
    throw new Error(`Preview process ${child.pid} did not exit after SIGKILL`);
  }
  return forcedExit;
}
