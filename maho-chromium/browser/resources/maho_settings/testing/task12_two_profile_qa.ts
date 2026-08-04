import {createHash} from 'node:crypto';
import {readFileSync, statSync, mkdirSync, writeFileSync} from 'node:fs';
import {getSettingsWs, CDP, clickElementByText} from './cdp_harness.ts';

const OUT = process.env.TASK12_OUT ?? '/tmp/task12-qa';
const APP = process.env.TASK12_APP ?? 'chromium/src/out/Default/Maho.app/Contents/MacOS/Maho';
const runId = process.argv.find(a => a.startsWith('--run-id='))?.split('=')[1] ?? 'adhoc';

const ws = await getSettingsWs();
const cdp = new CDP(ws);
await cdp.send('Runtime.enable');
await cdp.send('Page.enable');

async function screenshot(name: string): Promise<void> {
  const res = await cdp.send('Page.captureScreenshot', {format: 'png'}) as {result?: {data?: string}};
  const data = res?.result?.data;
  if (data) { mkdirSync(OUT, {recursive: true}); writeFileSync(`${OUT}/${runId}-${name}.png`, Buffer.from(data, 'base64')); }
}

async function waitCond(predicateExpr: string, deadlineMs = 12000): Promise<boolean> {
  return (await cdp.eval<boolean>(`(async () => {
    const store = window.settingsStore;
    const pred = () => { try { return !!(${predicateExpr}); } catch (e) { return false; } };
    if (pred()) return true;
    return await new Promise((resolve) => {
      let settled = false; let unsub = null; let obs = null;
      const finish = (ok) => { if (!settled) { settled = true; try { unsub && unsub(); } catch (e) {} try { obs && obs.disconnect(); } catch (e) {} clearTimeout(to); resolve(ok); } };
      const check = () => { if (pred()) finish(true); };
      try { unsub = store.subscribe(check); } catch (e) {}
      try { obs = new MutationObserver(check); obs.observe(document.documentElement, {attributes: true, childList: true, characterData: true, subtree: true}); } catch (e) {}
      const to = setTimeout(() => finish(pred()), ${deadlineMs});
    });
  })()`, true)) === true;
}

async function mutateUntilVerified(actionExpr: string, verifyExpr: string, deadlineMs = 12000): Promise<boolean> {
  return (await cdp.eval<boolean>(`(async () => {
    const store = window.settingsStore;
    const verify = (s) => (${verifyExpr});
    return await new Promise((resolve) => {
      let settled = false; let inFlight = false; let unsub = null;
      const finish = (ok) => { if (!settled) { settled = true; try { unsub && unsub(); } catch (e) {} clearTimeout(to); resolve(ok); } };
      const attempt = async () => {
        if (settled || inFlight) return;
        inFlight = true;
        try { const r = await (${actionExpr}); if (r && verify(store.getSnapshot())) { finish(true); return; } } catch (e) {}
        inFlight = false;
      };
      unsub = store.subscribe(() => { attempt(); });
      const to = setTimeout(() => finish(!!verify(store.getSnapshot())), ${deadlineMs});
      attempt();
    });
  })()`, true)) === true;
}

const profilesExpr = `(async () => { const r = await window.settingsStore.getHandler().getProfiles(); return r.profiles.map(p => ({id:p.id,name:p.name,avatarColor:p.avatarColor,isActive:p.isActive,spaceIds:(p.spaceIds||[]).slice()})); })()`;

if (!await waitCond(`typeof window.settingsStore?.getHandler === 'function'`, 20000)) throw new Error('settingsStore not ready');
await cdp.eval(`window.settingsStore.selectPane('profiles')`);
if (!await waitCond(`window.settingsStore.getSnapshot().currentPaneKey === 'profiles'`, 8000)) throw new Error('profiles pane');

const profilesBefore = await cdp.eval<Array<{id: string; name: string; avatarColor?: string; isActive: boolean; spaceIds: string[]}>>(profilesExpr, true);
if (!Array.isArray(profilesBefore)) throw new Error('getProfiles failed');
const activeProfile = profilesBefore.find(p => p.isActive);
if (!activeProfile) throw new Error('no active profile');

const bId = await cdp.eval<string>(`(async () => { const r = await window.settingsStore.getHandler().createProfile('Task 12 QA B'); return r.profile?.id; })()`, true);
if (typeof bId !== 'string') throw new Error('createProfile B failed');

await cdp.eval(`window.settingsStore.selectProfileFromCatalog(${JSON.stringify(bId)})`);
if (!await waitCond(`document.querySelector('[data-profile-editor="true"]') !== null && window.settingsStore.getSnapshot().selectedProfileContext && window.settingsStore.getSnapshot().selectedProfileContext.profileId === ${JSON.stringify(bId)}`, 15000)) throw new Error('editor did not mount for B');

const globalSnapshotBefore = await cdp.eval<string>(`(async () => { const s = await window.settingsStore.getHandler().getSettings?.(); return JSON.stringify(s ?? window.settingsStore.getSnapshot().settings ?? null); })()`, true);
const before = await cdp.eval<Record<string, unknown>>(`(() => { const st = window.settingsStore.getSnapshot(); return {selectedId: st.selectedProfileContext?.profileId, b_name: st.selectedProfileMetadata?.name, b_avatar: st.selectedProfileMetadata?.avatarColor}; })()`, true);
await screenshot('before');

const results: Record<string, boolean> = {};
results['name+avatar'] = await mutateUntilVerified(
  `store.updateSelectedProfileMetadata({name:'Task 12 Work QA',avatarColor:'#4F46E5'})`,
  `s.selectedProfileMetadata && s.selectedProfileMetadata.name === 'Task 12 Work QA' && s.selectedProfileMetadata.avatarColor === '#4F46E5'`);

await waitCond(`window.settingsStore.getSnapshot().selectedProfileMetadata && window.settingsStore.getSnapshot().selectedProfileMetadata.name === 'Task 12 Work QA'`, 5000);
await screenshot('after');
const after = await cdp.eval<Record<string, unknown>>(`(() => { const st = window.settingsStore.getSnapshot(); return {selectedId: st.selectedProfileContext?.profileId, b_name: st.selectedProfileMetadata?.name, b_avatar: st.selectedProfileMetadata?.avatarColor}; })()`, true);


let unsupportedPaneGated = false;
let unsupportedPaneDom = '';
await cdp.eval(`window.settingsStore.selectPane('passwords')`);
unsupportedPaneGated = await waitCond(
  `window.settingsStore.getSnapshot().currentPaneKey === 'passwords' && window.settingsStore.getSnapshot().selectedProfileContext && window.settingsStore.getSnapshot().selectedProfileContext.isHostProfile === false && document.querySelector('[data-selected-profile-limitation="true"][data-pane="passwords"]') !== null`, 8000);
unsupportedPaneDom = await cdp.eval<string>(`(() => { const el = document.querySelector('[data-selected-profile-limitation="true"][data-pane="passwords"]'); return el ? el.innerText.slice(0,300) : 'NO_LIMITATION_ELEMENT'; })()`, true) as string;
await screenshot('unsupported-pane');
await cdp.eval(`window.settingsStore.selectPane('profiles')`);

const dId = await cdp.eval<string>(`(async () => { const r = await window.settingsStore.getHandler().createProfile('Task 12 Cancel D'); return r.profile?.id; })()`, true);
let deleteCancelKeepsProfile = false;
if (typeof dId === 'string') {
  await cdp.eval(`window.settingsStore.selectProfileFromCatalog(${JSON.stringify(dId)})`);
  await waitCond(`window.settingsStore.getSnapshot().selectedProfileContext && window.settingsStore.getSnapshot().selectedProfileContext.profileId === ${JSON.stringify(dId)}`, 8000);
  await clickElementByText(cdp, 'button', 'Delete profile');
  const confirmShown = await waitCond(`Array.from(document.querySelectorAll('button')).some(b => (b.textContent||'').trim() === 'Confirm delete')`, 5000);
  if (confirmShown) { await clickElementByText(cdp, 'button', 'Cancel'); }
  await waitCond(`Array.from(document.querySelectorAll('button')).every(b => (b.textContent||'').trim() !== 'Confirm delete')`, 5000);
  const dStillExists = await cdp.eval<boolean>(`(async () => { const r = await window.settingsStore.getHandler().getProfiles(); return r.profiles.some(p => p.id === ${JSON.stringify(dId)}); })()`, true);
  deleteCancelKeepsProfile = dStillExists === true;
}

const staleTargetFailsClosed = await cdp.eval<boolean>(`(async () => {
  const h = window.settingsStore.getHandler();
  await window.settingsStore.selectProfileFromCatalog(${JSON.stringify(bId)});
  try { await h.deleteProfile(${JSON.stringify(bId)}); } catch (e) {}
  const gone = await new Promise((resolve) => {
    const store = window.settingsStore;
    const check = async () => { try { const r = await h.getProfiles(); return !r.profiles.some(p => p.id === ${JSON.stringify(bId)}); } catch (e) { return false; } };
    check().then((ok) => {
      if (ok) { resolve(true); return; }
      const unsub = store.subscribe(() => { check().then((k) => { if (k) { try { unsub(); } catch (e) {} resolve(true); } }); });
      setTimeout(() => { try { unsub(); } catch (e) {} check().then(resolve); }, 8000);
    });
  });
  const ok = await window.settingsStore.updateSelectedProfileMetadata({name:'ShouldNotApply',avatarColor:'#123456',downloadPath:null,archiveTimeoutHours:null}).catch(() => false);
  return gone === true && ok === false;
})()`, true);

const profilesAfter = await cdp.eval<Array<{id: string; name: string; avatarColor?: string; isActive: boolean; spaceIds: string[]}>>(profilesExpr, true);
const globalSnapshotAfter = await cdp.eval<string>(`(async () => { const s = await window.settingsStore.getHandler().getSettings?.(); return JSON.stringify(s ?? window.settingsStore.getSnapshot().settings ?? null); })()`, true);
const badIdRejected = await cdp.eval<boolean>(`(async () => { try { const ok = await window.settingsStore.getHandler().updateProfileMetadata?.('___nonexistent___', {name:'x'}); return ok === false || ok == null; } catch (e) { return true; } })()`, true);

const activeAfter = Array.isArray(profilesAfter) ? profilesAfter.find(p => p.isActive) : undefined;
const aBefore = profilesBefore.find(p => p.id === activeProfile.id);
const aAfter = Array.isArray(profilesAfter) ? profilesAfter.find(p => p.id === activeProfile.id) : undefined;
const b = (before ?? {}) as Record<string, unknown>;
const a = (after ?? {}) as Record<string, unknown>;
const checks = {
  b_selected: a.selectedId === bId,
  b_name_changed: b.b_name !== 'Task 12 Work QA' && a.b_name === 'Task 12 Work QA',
  b_avatar_changed: a.b_avatar === '#4F46E5',
  active_unchanged: !!activeAfter && activeAfter.id === activeProfile.id,
  A_identity_unchanged: !!aBefore && !!aAfter && aBefore.name === aAfter.name && aBefore.avatarColor === aAfter.avatarColor,
  A_spaces_unchanged: !!aBefore && !!aAfter && JSON.stringify(aBefore.spaceIds) === JSON.stringify(aAfter.spaceIds),
  globals_unchanged: globalSnapshotBefore === globalSnapshotAfter,
  bad_id_fails_closed: badIdRejected === true,
  unsupported_pane_gated: unsupportedPaneGated === true,
  delete_cancel_keeps_profile: deleteCancelKeepsProfile === true,
  stale_target_fails_closed: staleTargetFailsClosed === true,
  mutations_accepted: results['name+avatar'] === true,
};
const pass = Object.values(checks).every(Boolean);

let appIdentity: Record<string, unknown> = {path: APP};
try { const bytes = readFileSync(APP); const st = statSync(APP); appIdentity = {path: APP, sha256: createHash('sha256').update(bytes).digest('hex'), size: st.size, mtime_ms: st.mtimeMs}; } catch (e) { appIdentity = {path: APP, error: String(e)}; }

mkdirSync(OUT, {recursive: true});
writeFileSync(`${OUT}/${runId}-receipt.json`, JSON.stringify({schema: 'task12-two-profile-qa', runId, timestamp: new Date().toISOString(), command: process.argv.join(' '), app: appIdentity, pass, target_b: bId, cancel_target_d: dId, active: activeProfile.id, before: b, after: a, mutation_results: results, unsupported_pane_dom: unsupportedPaneDom, checks}, null, 2));
console.log('TASK12_QA_RESULT ' + JSON.stringify({pass, checks}));

ws.close();
process.exit(pass ? 0 : 1);
