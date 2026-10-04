import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {afterEach, describe, expect, it, vi} from 'vitest';
import {getMahoAiPageConnection} from '../../page_connection.js';
import {MahoAiStore} from '../../store.js';
import {useVoiceInput, type VoiceInputController} from '../hooks/use-voice-input.js';

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason: unknown) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return {promise, resolve, reject};
}

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {configurable: true, value: true});
afterEach(() => { vi.restoreAllMocks(); vi.unstubAllGlobals(); });

describe('voice capture cancellation', () => {
  it.each(['media', 'worklet', 'failure', 'unmount'] as const)(
      'releases microphone resources after %s cancellation or failure', async phase => {
    const connection = getMahoAiPageConnection();
    vi.spyOn(connection.handler, 'startVoiceSession').mockResolvedValue({accepted: true});
    vi.spyOn(connection.handler, 'stopVoiceSession').mockResolvedValue(undefined);
    const store = new MahoAiStore(connection.handler, connection.router);
    const reached = deferred<void>();
    const media = deferred<MediaStream>();
    const module = deferred<void>();
    const stopTrack = vi.fn();
    const stream = {getTracks: () => [{stop: stopTrack}]} as unknown as MediaStream;
    const connect = vi.fn();
    const node = () => ({connect, disconnect: vi.fn(), gain: {value: 1},
      port: {close: vi.fn()}, fftSize: 1024, getFloatTimeDomainData: vi.fn()});
    const close = vi.fn(async () => {});
    vi.stubGlobal('navigator', {mediaDevices: {getUserMedia: vi.fn(() => {
      if (phase === 'media' || phase === 'unmount') reached.resolve();
      return media.promise;
    })}});
    vi.stubGlobal('AudioContext', class {
      close = close;
      audioWorklet = {addModule: () => { reached.resolve(); return module.promise; }};
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
    let controller!: VoiceInputController;
    function Harness() { controller = useVoiceInput({store, onFinalTranscript: vi.fn()}); return null; }
    const container = document.createElement('div');
    const root = createRoot(container);
    act(() => root.render(<Harness />));
    const starting = controller.start();
    if (phase === 'worklet' || phase === 'failure') media.resolve(stream);
    await reached.promise;
    if (phase === 'unmount') act(() => root.unmount());
    else if (phase !== 'failure') await controller.stop();
    if (phase === 'media' || phase === 'unmount') media.resolve(stream);
    if (phase === 'failure') module.reject(new Error('Worklet failed'));
    else module.resolve();
    await starting;
    expect.soft(stopTrack).toHaveBeenCalledOnce();
    expect.soft(connect).not.toHaveBeenCalled();
    expect.soft(store.getSnapshot().voice.status).not.toBe('listening');
    if (phase === 'worklet' || phase === 'failure') expect.soft(close).toHaveBeenCalledOnce();
    if (phase !== 'unmount') act(() => root.unmount());
    store.dispose();
  });
});
