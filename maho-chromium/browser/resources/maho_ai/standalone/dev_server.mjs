import http from 'node:http';
import https from 'node:https';
import fs from 'node:fs/promises';
import nodeFs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {context} from '../../../../../website/node_modules/esbuild/lib/main.js';
import {nodeModulesGuardPlugin} from '../../maho_common/react/bundle_guard.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const mahoAiDir = path.resolve(__dirname, '..');
const websiteNodeModules = path.resolve(__dirname, '../../../../../website/node_modules');
const outputFile = path.join(__dirname, 'app_bundle.js');
const previewOutputDir = path.join(os.tmpdir(), 'maho-ai-standalone-preview');
const guidancePreviewOutputFile = path.join(previewOutputDir, 'mcp_agent_guidance_preview.js');
const generatedStylesheet = path.resolve(
    __dirname,
    '../../../../../chromium/src/out/Default/gen/maho/browser/resources/maho_ai/app.generated.css');
const hostname = process.env.MAHO_AI_STANDALONE_HOST || '127.0.0.1';
const port = Number(process.env.MAHO_AI_STANDALONE_PORT || '4071');
const opencodeBaseUrl = new URL(process.env.OPENCODE_BASE_URL || 'http://127.0.0.1:4096/');
const shadowRoot = path.join(os.homedir(), 'node_modules');

// Mirrors the alias map in ../bundle_react.mjs. esbuild walks node_modules
// upward from the importer and only consults nodePaths after that walk fails,
// so an outer workspace node_modules shadows website/node_modules and a SECOND
// copy of React lands in the bundle. Two React copies means two dispatcher
// instances, and every hook throws "Invalid hook call" /
// "Cannot read properties of null (reading 'useRef')" -> the panel renders as a
// blank #app. Pin every shadowable specifier to the canonical copy.
function dedupeAlias() {
  if (!nodeFs.existsSync(shadowRoot)) {
    return {};
  }
  return Object.fromEntries(
      nodeFs.readdirSync(shadowRoot)
          .flatMap((entry) => entry.startsWith('@')
              ? nodeFs.readdirSync(path.join(shadowRoot, entry))
                    .map((sub) => `${entry}/${sub}`)
              : [entry])
          .filter((pkg) => !pkg.startsWith('.') &&
              nodeFs.existsSync(path.join(websiteNodeModules, pkg)))
          .map((pkg) => [pkg, path.join(websiteNodeModules, pkg)]));
}

function aliasPlugin() {
  return {
    name: 'maho-ai-standalone-alias',
    setup(build) {
      build.onResolve({filter: /maho_ai\.mojom-webui\.js$/}, () => ({
        path: path.join(__dirname, 'maho_ai.mojom-webui.js'),
      }));
      build.onResolve({filter: /\/views\/icons\.[jt]s$/}, () => ({
        path: path.join(__dirname, 'icons_stub.ts'),
      }));
      build.onResolve({filter: /^chrome:\/\/resources\/js\/load_time_data\.js$/}, () => ({
        path: path.join(__dirname, 'load_time_data.js'),
      }));
      build.onResolve({filter: /^chrome:\/\/resources\/js\/parse_html_subset\.js$/}, () => ({
        path: path.join(__dirname, 'parse_html_subset.js'),
      }));
    },
  };
}

function contentTypeFor(filePath) {
  const extension = path.extname(filePath);
  switch (extension) {
    case '.css':
      return 'text/css; charset=utf-8';
    case '.html':
      return 'text/html; charset=utf-8';
    case '.js':
    case '.mjs':
      return 'text/javascript; charset=utf-8';
    case '.json':
      return 'application/json; charset=utf-8';
    default:
      return 'text/plain; charset=utf-8';
  }
}

async function serveStatic(requestPath, response) {
  const pathname = new URL(requestPath, `http://${hostname}:${port}`).pathname;
  if (pathname === '/maho_ai.css') {
    await serveFile(generatedStylesheet, response);
    return;
  }
  if (pathname === '/standalone/mcp_agent_guidance_preview.js') {
    await serveFile(guidancePreviewOutputFile, response);
    return;
  }
  if (pathname === '/standalone/mcp_agent_guidance_preview.js.map') {
    await serveFile(`${guidancePreviewOutputFile}.map`, response);
    return;
  }

  const normalized = pathname === '/' ? '/standalone/index.html' : pathname;
  const filePath = path.resolve(mahoAiDir, `.${normalized}`);
  if (!filePath.startsWith(mahoAiDir)) {
    response.writeHead(403);
    response.end('Forbidden');
    return;
  }

  try {
    await serveFile(filePath, response);
  } catch {
    response.writeHead(404);
    response.end('Not found');
  }
}

async function serveFile(filePath, response) {
  const contents = await fs.readFile(filePath);
  response.writeHead(200, {'Content-Type': contentTypeFor(filePath)});
  response.end(contents);
}

function proxyRequest(request, response) {
  const upstreamPath = request.url.replace(/^\/api\/?/, '/');
  const upstreamUrl = new URL(upstreamPath, opencodeBaseUrl);
  const transport = upstreamUrl.protocol === 'https:' ? https : http;
  const proxy = transport.request(upstreamUrl, {
    method: request.method,
    headers: {
      ...request.headers,
      host: upstreamUrl.host,
    },
  }, upstreamResponse => {
    response.writeHead(upstreamResponse.statusCode || 502, upstreamResponse.headers);
    upstreamResponse.pipe(response);
  });

  proxy.on('error', error => {
    response.writeHead(502, {'Content-Type': 'text/plain; charset=utf-8'});
    response.end(`Proxy error: ${error.message}`);
  });

  request.pipe(proxy);
}

function createBuildContext(entryPoint, outfile) {
  return context({
    absWorkingDir: mahoAiDir,
    alias: dedupeAlias(),
    bundle: true,
    entryPoints: [entryPoint],
    format: 'esm',
    legalComments: 'none',
    loader: {
      '.ts': 'ts',
      '.tsx': 'tsx',
    },
    nodePaths: [websiteNodeModules],
    outfile,
    platform: 'browser',
    plugins: [aliasPlugin(), nodeModulesGuardPlugin(websiteNodeModules)],
    sourcemap: true,
    target: ['chrome120'],
    tsconfig: path.join(mahoAiDir, 'react/tsconfig.json'),
  });
}

await fs.mkdir(previewOutputDir, {recursive: true});

const buildContexts = await Promise.all([
  createBuildContext(path.join(__dirname, 'app.ts'), outputFile),
  createBuildContext(
      path.join(__dirname, 'mcp_agent_guidance_preview.tsx'),
      guidancePreviewOutputFile),
]);

await Promise.all(buildContexts.map(buildContext => buildContext.rebuild()));
await Promise.all(buildContexts.map(buildContext => buildContext.watch()));

const server = http.createServer((request, response) => {
  if (!request.url) {
    response.writeHead(400);
    response.end('Missing URL');
    return;
  }

  if (request.url.startsWith('/api/')) {
    proxyRequest(request, response);
    return;
  }

  void serveStatic(request.url, response);
});

server.listen(port, hostname, () => {
  process.stdout.write(
      `Maho AI standalone dev server running at http://${hostname}:${port}\n` +
      `Proxying OpenCode requests to ${opencodeBaseUrl.href}\n`);
});
