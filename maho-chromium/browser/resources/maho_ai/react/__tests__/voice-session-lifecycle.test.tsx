import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {afterEach, describe, expect, it, vi} from 'vitest';
import {getMahoAiPageConnection} from '../../page_connection.js';
import {MahoAiStore} from '../../store.js';
import {VoiceInputModal} from '../features/compact/voice-input-modal.js';
import {
  useVoiceInput,
  VOICE_TRANSCRIBE_TIMEOUT_MS,
  type VoiceInputController,
} from '../hooks/use-voice-input.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {configurable: true, value: true});
afterEach(() => { vi.restoreAllMocks(); vi.unstubAllGlobals(); vi.useRealTimers(); });

interface StubEvent<T> {
  emit(value: T): void;
  removeListener(id: number): boolean;
}

interface StubVoiceRouter {
  onVoicePartial: StubEvent<string>;
  onVoiceFinal: StubEvent<string>;
  onVoiceError: StubEvent<string>;
}

async function setup() {
  const connection = getMahoAiPageConnection();
  const router = connection.router as unknown as StubVoiceRouter;
  // The standalone router's removeListener is a no-op; make it real so the
  // test observes the hook detaching its listeners.
  vi.spyOn(connection.router, 'removeListener').mockImplementation((id: number) => {
    router.onVoicePartial.removeListener(id);
    router.onVoiceFinal.removeListener(id);
    router.onVoiceError.removeListener(id);
    return true;
  });
  vi.spyOn(connection.handler, 'startVoiceSession').mockResolvedValue({accepted: true});
  const stopVoiceSession =
      vi.spyOn(connection.handler, 'stopVoiceSession').mockResolvedValue(undefined);
  vi.spyOn(connection.handler, 'pushAudioChunk').mockResolvedValue(undefined);
  const store = new MahoAiStore(connection.handler, connection.router);
  const stopTrack = vi.fn();
  const stream = {getTracks: () => [{stop: stopTrack}]} as unknown as MediaStream;
  const node = () => ({connect: vi.fn(), disconnect: vi.fn(), gain: {value: 1},
    port: {close: vi.fn()}, fftSize: 1024, getFloatTimeDomainData: vi.fn()});
  vi.stubGlobal('navigator', {mediaDevices: {getUserMedia: vi.fn(async () => stream)}});
  vi.stubGlobal('AudioContext', class {
    close = vi.fn(async () => {});
    audioWorklet = {addModule: vi.fn(async () => {})};
    createMediaStreamSource = node;
    createAnalyser = node;
    createGain = node;
    destination = {};
  });
  vi.stubGlobal('AudioWorkletNode', class { constructor() { return node(); } });
  vi.stubGlobal('requestAnimationFrame', vi.fn(() => 1));
  vi.stubGlobal('cancelAnimationFrame', vi.fn());
  vi.stubGlobal('URL', class extends URL {
    static createObjectURL = vi.fn(() => 'blob:voice-test');
    static revokeObjectURL = vi.fn();
  });
  const onFinal = vi.fn();
  let controller!: VoiceInputController;
  function Harness() { controller = useVoiceInput({store, onFinalTranscript: onFinal}); return null; }
  const root = createRoot(document.createElement('div'));
  act(() => root.render(<Harness />));
  await act(async () => { await controller.start(); });
  expect(store.getSnapshot().voice.status).toBe('listening');
  stopVoiceSession.mockClear();
  return {
    router, store, onFinal, stopTrack, stopVoiceSession,
    get controller() { return controller; },
    teardown() { act(() => root.unmount()); store.dispose(); },
  };
}

describe('voice session lifecycle', () => {
  it('delivers the final transcript that arrives after finish()', async () => {
    const t = await setup();
    await act(async () => { await t.controller.finish(); });
    expect(t.stopVoiceSession).toHaveBeenCalledOnce();
    expect(t.stopTrack).toHaveBeenCalledOnce();
    expect(t.controller.transcribing).toBe(true);
    act(() => t.router.onVoiceFinal.emit('book a table'));
    expect(t.onFinal).toHaveBeenCalledOnce();
    expect(t.onFinal.mock.calls[0]![0]).toBe('book a table');
    expect(t.controller.transcribing).toBe(false);
    act(() => t.router.onVoiceFinal.emit('book a table'));
    expect(t.onFinal).toHaveBeenCalledOnce();
    t.teardown();
  });

  it('drops a stale final from an older session while listening', async () => {
    const t = await setup();
    act(() => t.router.onVoiceFinal.emit('previous utterance'));
    act(() => t.router.onVoiceError.emit('Transcription failed'));
    expect(t.onFinal).not.toHaveBeenCalled();
    expect(t.store.getSnapshot().voice.status).toBe('listening');
    t.teardown();
  });

  it('discards the result when the user cancels while transcribing', async () => {
    const t = await setup();
    await act(async () => { await t.controller.finish(); });
    await act(async () => { await t.controller.stop(); });
    act(() => t.router.onVoiceFinal.emit('late result'));
    expect(t.onFinal).not.toHaveBeenCalled();
    t.teardown();
  });

  it('surfaces a transcription error for the finished session', async () => {
    const t = await setup();
    await act(async () => { await t.controller.finish(); });
    act(() => t.router.onVoiceError.emit("Didn't catch that"));
    expect(t.store.getSnapshot().voice.error).toBe("Didn't catch that");
    expect(t.store.getSnapshot().voice.status).toBe('error');
    t.teardown();
  });

  it('gives up after a bounded wait for the terminal event', async () => {
    const t = await setup();
    vi.useFakeTimers();
    await act(async () => { await t.controller.finish(); });
    act(() => { vi.advanceTimersByTime(VOICE_TRANSCRIBE_TIMEOUT_MS); });
    expect(t.store.getSnapshot().voice.status).toBe('error');
    act(() => t.router.onVoiceFinal.emit('too late'));
    expect(t.onFinal).not.toHaveBeenCalled();
    t.teardown();
  });

  it('reports preparing while backend setup runs, then opens the microphone', async () => {
    const t = await setup();
    const connection = getMahoAiPageConnection();
    let resolveStart!: (value: {accepted: boolean}) => void;
    vi.mocked(connection.handler.startVoiceSession).mockImplementationOnce(
        () => new Promise(resolve => { resolveStart = resolve; }));
    let starting!: Promise<void>;
    await act(async () => { starting = t.controller.start(); await Promise.resolve(); });
    await act(async () => { await Promise.resolve(); });
    expect(t.controller.preparing).toBe(true);
    await act(async () => { resolveStart({accepted: true}); await starting; });
    expect(t.controller.preparing).toBe(false);
    expect(t.store.getSnapshot().voice.status).toBe('listening');
    t.teardown();
  });

  it('reports a busy backend, not "already listening", on a bare rejection', async () => {
    const t = await setup();
    const connection = getMahoAiPageConnection();
    vi.mocked(connection.handler.startVoiceSession).mockResolvedValueOnce({accepted: false});
    await act(async () => { await t.controller.start(); });
    expect(t.store.getSnapshot().voice.error).toBe('Voice input is busy. Try again.');
    expect(t.controller.preparing).toBe(false);
    t.teardown();
  });

  it('keeps a setup error as the rejection reason', async () => {
    const t = await setup();
    const connection = getMahoAiPageConnection();
    vi.mocked(connection.handler.startVoiceSession).mockImplementationOnce(async () => {
      t.router.onVoiceError.emit('Voice model download failed');
      return {accepted: false};
    });
    await act(async () => { await t.controller.start(); });
    expect(t.store.getSnapshot().voice.error).toBe('Voice model download failed');
    t.teardown();
  });

  it('shows "Preparing voice input" in the modal while preparing', async () => {
    const t = await setup();
    await act(async () => { await t.controller.stop(); });
    act(() => t.store.openVoice());
    const host = document.createElement('div');
    document.body.appendChild(host);
    const root = createRoot(host);
    act(() => root.render(
        <VoiceInputModal onClose={vi.fn()} onFinish={vi.fn()} onRetry={vi.fn()}
          preparing state={t.store.getSnapshot()} transcribing={false} />));
    expect(document.body.textContent).toContain('Preparing voice input…');
    expect(document.body.textContent).not.toContain('Opening microphone…');
    act(() => root.render(
        <VoiceInputModal onClose={vi.fn()} onFinish={vi.fn()} onRetry={vi.fn()}
          preparing={false} state={t.store.getSnapshot()} transcribing={false} />));
    expect(document.body.textContent).toContain('Opening microphone…');
    act(() => root.unmount());
    host.remove();
    t.teardown();
  });
});
