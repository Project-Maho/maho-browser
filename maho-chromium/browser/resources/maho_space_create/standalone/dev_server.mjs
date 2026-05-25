import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {context} from '../../../../../website/node_modules/esbuild/lib/main.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const standaloneDir = __dirname;
const featureDir = path.resolve(__dirname, '..');
const websiteNodeModules = path.resolve(__dirname, '../../../../../website/node_modules');
const outputFile = path.join(__dirname, 'app_bundle.js');
const hostname = process.env.MAHO_THEME_STANDALONE_HOST || '127.0.0.1';
const port = Number(process.env.MAHO_THEME_STANDALONE_PORT || '4072');

function aliasPlugin() {
  return {
    name: 'maho-theme-standalone-alias',
    setup(build) {
      build.onResolve({filter: /maho_space_create\.mojom-webui\.js$/}, () => ({
        path: path.join(standaloneDir, 'maho_space_create.mojom-webui.js'),
      }));
    },
  };
}

function contentTypeFor(filePath) {
  const extension = path.extname(filePath);
  switch (extension) {
    case '.css': return 'text/css; charset=utf-8';
    case '.html': return 'text/html; charset=utf-8';
    case '.js':
    case '.mjs': return 'text/javascript; charset=utf-8';
    case '.json': return 'application/json; charset=utf-8';
    case '.map': return 'application/json; charset=utf-8';
    default: return 'text/plain; charset=utf-8';
  }
}

async function serveStatic(requestPath, response) {
  const normalized = requestPath === '/' ? '/standalone/index.html' : requestPath;
  const filePath = path.resolve(featureDir, `.${normalized}`);
  if (!filePath.startsWith(featureDir)) {
    response.writeHead(403);
    response.end('Forbidden');
    return;
  }
  try {
    const contents = await fs.readFile(filePath);
    response.writeHead(200, {'Content-Type': contentTypeFor(filePath)});
    response.end(contents);
  } catch {
    response.writeHead(404);
    response.end('Not found');
  }
}

const buildContext = await context({
  absWorkingDir: featureDir,
  bundle: true,
  entryPoints: [path.join(featureDir, 'react/app.tsx')],
  format: 'esm',
  jsx: 'automatic',
  legalComments: 'none',
  loader: {
    '.ts': 'ts',
    '.tsx': 'tsx',
  },
  nodePaths: [websiteNodeModules],
  outfile: outputFile,
  platform: 'browser',
  plugins: [aliasPlugin()],
  sourcemap: true,
  target: ['chrome120'],
});

await buildContext.rebuild();
await buildContext.watch();

const server = http.createServer((request, response) => {
  if (!request.url) {
    response.writeHead(400);
    response.end('Missing URL');
    return;
  }
  void serveStatic(request.url, response);
});

server.listen(port, hostname, () => {
  process.stdout.write(
      `Maho Theme Editor standalone running at http://${hostname}:${port}\n`);
});
