import {compileScript, readResource} from './review_script';
import {expect, it, vi} from 'vitest';

it('releases Space save busy state and reports a rejected update', async () => {
  // Execute the actual private UI callback with its captured state and native boundary.
  const source = readResource('maho_space_config/react/app.tsx');
  const callback = source.slice(source.indexOf('  const handleSave ='), source.indexOf('\n  return (', source.indexOf('  const handleSave =')));
  let busy = false;
  let error = '';
  const handler = {updateName: vi.fn().mockRejectedValue(new Error('disconnected')), closeDialog: vi.fn()};
  const fn = new Function('saving', 'setSaving', 'setSaveError', 'profileValue', 'DEFAULT_PROFILE_VALUE',
    'spaceName', 'initialInfo', 'spaceIcon', 'profiles', 'resolveProfileValue', 'handler',
    compileScript(callback) + '\nreturn handleSave();');
  await fn(false, (v: boolean) => {busy = v;}, (v: string) => {error = v;}, '', '',
    'new', {name: 'old', icon: 'x'}, 'x', [], () => '', handler);
  expect(handler.updateName).toHaveBeenCalledWith('new');
  expect(busy).toBe(false);
  expect(error).toBeTruthy();
  expect(handler.closeDialog).not.toHaveBeenCalled();
});
