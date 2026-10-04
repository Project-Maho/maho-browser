import { chromium, expect } from '../../../website/node_modules/@playwright/test/index.mjs';
import { createServer } from 'node:http';
import { readFile, rm } from 'node:fs/promises';
import { resolve, extname } from 'node:path';
import assert from 'node:assert/strict';

const root = resolve('dist');
const server = createServer(async (req, res) => {
  const path = resolve(root, '.' + (req.url === '/' ? '/index.html' : req.url.split('?')[0]));
  if (!path.startsWith(root + '/')) { res.writeHead(403).end(); return; }
  try {
    res.setHeader('Content-Type', ({ '.js': 'text/javascript', '.html': 'text/html', '.css': 'text/css', '.png': 'image/png' })[extname(path)] ?? 'application/octet-stream');
    res.end(await readFile(path));
  } catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const context = await chromium.launchPersistentContext(resolve('.review-browser-profile'), { headless: true, viewport: { width: 430, height: 850 } });
const page = await context.newPage();
const errors = [];
page.on('pageerror', error => errors.push(error.message));
await page.addInitScript(() => {
  window.review = { drafts: {}, pendingSend: null, answers: [], queue: [], sent: [] };
  const key = scope => scope.kind === 'new_task' ? 'new_task' : `conversation:${scope.conversationId}`;
  window.__mahoChromePageHandler = raw => {
    const { id, method, params } = JSON.parse(raw);
    const reply = result => window.__mahoBridgeResponse(JSON.stringify({ jsonrpc: '2.0', id, result }));
    let result = true;
    switch (method) {
      case 'composerDraftGet': result = window.review.drafts[key(params.scope)] ?? null; break;
      case 'composerDraftSet': window.review.drafts[key(params.scope)] = { version: 1, text: params.text, updatedAt: '2026-09-14T00:00:00Z' }; break;
      case 'composerDraftDelete': delete window.review.drafts[key(params.scope)]; break;
      case 'agentCreateSession': result = 'agent-1'; break;
      case 'agentSendMessage': window.review.sent.push(params.message); window.review.pendingSend = reply; window.dispatchEvent(new Event('review-send')); return;
      case 'agentPollEvent': result = window.review.queue.shift() ?? null; break;
      case 'agentResolveInteraction': window.review.answers.push(params); break;
      case 'getAiSettings': result = { provider: 'maho-managed', baseUrl: '', model: '', hasApiKey: false, hasByokOpenai: false, hasByokAnthropic: false }; break;
      case 'chatSessionResume': result = `handle-${params.handle}`; break;
      case 'chatSessionStart': result = 'new-chat'; break;
      case 'chatGetHistory': case 'chatPollEvents': case 'conversationList': case 'conversationProjectList': result = []; break;
    }
    queueMicrotask(() => reply(result));
  };
});
try {
  const base = `http://127.0.0.1:${server.address().port}`;
  await page.goto(`${base}/#agent?goal=50%`);
  await expect(page.getByRole('textbox', { name: 'Message' })).toBeVisible();
  assert.deepEqual(await page.evaluate(() => window.review.sent), ['50%']);
  await page.getByRole('textbox', { name: 'Message' }).fill('replacement draft');
  await page.evaluate(() => window.review.pendingSend(true));
  // Hash navigation from the editable composer exercises SPA cleanup without blur.
  await page.evaluate(() => { window.location.hash = 'conversations'; });
  await expect(page.getByTestId('conversation-list-screen')).toBeVisible();
  await page.evaluate(() => { window.location.hash = 'agent'; });
  await expect(page.getByRole('textbox')).toHaveValue('replacement draft');
  console.log('PASS malformed hash, delayed send acknowledgement, SPA draft restoration');

  await page.evaluate(() => { window.location.hash = 'chat?sessionId=A'; });
  await expect(page.getByRole('textbox', { name: 'Message' })).toBeEnabled();
  await page.getByRole('textbox').fill('private A');
  await page.evaluate(() => { window.location.hash = 'chat?sessionId=B'; });
  await expect(page.getByRole('textbox')).toHaveValue('');
  await expect(page.getByRole('textbox')).toBeEnabled();
  await page.evaluate(() => { window.location.hash = 'chat?sessionId=A'; });
  await expect(page.getByRole('textbox')).toHaveValue('private A');
  console.log('PASS chat A -> B -> A preserves and isolates scoped drafts');

  await page.evaluate(() => { window.location.hash = 'agent'; });
  await expect(page.getByRole('textbox')).toHaveValue('replacement draft');
  await page.getByRole('button', { name: 'Send message' }).click();
  await page.evaluate(() => {
    window.review.pendingSend(true);
    window.review.queue.push(JSON.stringify({ type: 'interaction_request', data: { id: 'q-browser', kind: 'question', question: 'Destination?', options: [] } }));
    document.dispatchEvent(new Event('visibilitychange'));
  });
  await page.getByRole('textbox', { name: 'Answer' }).fill('Seoul');
  await page.getByRole('button', { name: 'Submit answer' }).click();
  await expect(page.getByRole('textbox', { name: 'Answer' })).toBeDisabled();
  const answers = await page.evaluate(() => window.review.answers);
  assert.equal(answers[0].requestId, 'q-browser');
  assert.deepEqual(JSON.parse(answers[0].answerJson), { answer_kind: 'text', '0': 'Seoul' });
  await page.screenshot({ path: 'review-browser.png', fullPage: true });
  assert.deepEqual(errors, []);
  console.log('PASS text answer RPC and disabled resolved control; no browser page errors');
} finally {
  await context.close();
  await new Promise(resolve => server.close(resolve));
  await rm('.review-browser-profile', { recursive: true, force: true });
}
