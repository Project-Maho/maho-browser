import {useCallback, useEffect, useRef} from 'react';

import type {MahoAiStore} from '../../store.js';
import {getMahoAiPageConnection} from '../../page_connection.js';

const WORKLET_NAME = 'maho-voice-capture';
const RMS_GAIN = 5.5;

const VOICE_WORKLET_SOURCE = `
const TARGET_SAMPLE_RATE = 16000;
const MESSAGE_FRAME_TARGET = 1600;

class MahoVoiceCaptureProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.inputBuffer = [];
    this.outputBuffer = [];
    this.cursor = 0;
    this.ratio = sampleRate / TARGET_SAMPLE_RATE;
  }

  process(inputs) {
    const input = inputs[0];
    const channel = input && input[0];
    if (!channel) {
      return true;
    }

    for (let index = 0; index < channel.length; index += 1) {
      this.inputBuffer.push(channel[index]);
    }

    while (this.cursor + this.ratio <= this.inputBuffer.length) {
      const start = Math.floor(this.cursor);
      const end = Math.max(start + 1, Math.floor(this.cursor + this.ratio));
      let sum = 0;
      let count = 0;
      for (let index = start; index < end && index < this.inputBuffer.length; index += 1) {
        sum += this.inputBuffer[index];
        count += 1;
      }
      this.outputBuffer.push(count > 0 ? sum / count : 0);
      this.cursor += this.ratio;
    }

    const consumed = Math.floor(this.cursor);
    if (consumed > 0) {
      this.inputBuffer = this.inputBuffer.slice(consumed);
      this.cursor -= consumed;
    }

    if (this.outputBuffer.length >= MESSAGE_FRAME_TARGET) {
      const chunk = new Float32Array(this.outputBuffer);
      this.port.postMessage(chunk, [chunk.buffer]);
      this.outputBuffer = [];
    }

    return true;
  }
}

registerProcessor('${WORKLET_NAME}', MahoVoiceCaptureProcessor);
`;

interface VoiceMojoListeners {
  partial: number;
  final: number;
  error: number;
}

interface CleanupOptions {
  resetStatus: boolean;
  stopBackend: boolean;
}

export interface VoiceFinalControls {
  stop: () => Promise<void>;
}

export interface VoiceInputController {
  start: () => Promise<void>;
  stop: () => Promise<void>;
}

export interface UseVoiceInputOptions {
  store: MahoAiStore;
  onFinalTranscript: (transcript: string, controls: VoiceFinalControls) => void;
}

const voiceMojo = getMahoAiPageConnection();

function hasVoiceCaptureSupport(): boolean {
  return typeof navigator !== 'undefined' &&
      typeof navigator.mediaDevices?.getUserMedia === 'function' &&
      typeof AudioContext !== 'undefined' &&
      typeof AudioWorkletNode !== 'undefined';
}

function getVoiceCaptureErrorMessage(error: unknown): string {
  if (error instanceof DOMException) {
    switch (error.name) {
      case 'NotAllowedError':
      case 'SecurityError':
        return 'Microphone permission was denied.';
      case 'NotFoundError':
        return 'No microphone was found.';
      case 'NotReadableError':
        return 'Microphone is already in use.';
    }
  }

  if (error instanceof Error) {
    return error.message;
  }

  return String(error);
}

function createWorkletUrl(): string {
  return URL.createObjectURL(new Blob([VOICE_WORKLET_SOURCE], {type: 'text/javascript'}));
}

export function useVoiceInput({store, onFinalTranscript}: UseVoiceInputOptions): VoiceInputController {
  const storeRef = useRef(store);
  const onFinalTranscriptRef = useRef(onFinalTranscript);
  const audioContextRef = useRef<AudioContext|null>(null);
  const analyserRef = useRef<AnalyserNode|null>(null);
  const sourceRef = useRef<MediaStreamAudioSourceNode|null>(null);
  const workletRef = useRef<AudioWorkletNode|null>(null);
  const muteRef = useRef<GainNode|null>(null);
  const streamRef = useRef<MediaStream|null>(null);
  const animationFrameRef = useRef<number|null>(null);
  const workletUrlRef = useRef<string|null>(null);
  const listenerIdsRef = useRef<VoiceMojoListeners|null>(null);
  const captureGenerationRef = useRef(0);

  useEffect(() => {
    storeRef.current = store;
  }, [store]);

  useEffect(() => {
    onFinalTranscriptRef.current = onFinalTranscript;
  }, [onFinalTranscript]);

  const unbindVoiceListeners = useCallback(() => {
    const listenerIds = listenerIdsRef.current;
    if (!listenerIds) {
      return;
    }

    voiceMojo.router.removeListener(listenerIds.partial);
    voiceMojo.router.removeListener(listenerIds.final);
    voiceMojo.router.removeListener(listenerIds.error);
    listenerIdsRef.current = null;
  }, []);

  const cleanupCapture = useCallback(async ({resetStatus, stopBackend}: CleanupOptions): Promise<void> => {
    const generation = ++captureGenerationRef.current;
    if (animationFrameRef.current !== null) {
      cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
    }

    unbindVoiceListeners();

    workletRef.current?.port.close();
    workletRef.current?.disconnect();
    muteRef.current?.disconnect();
    sourceRef.current?.disconnect();
    analyserRef.current?.disconnect();

    streamRef.current?.getTracks().forEach(track => {
      track.stop();
    });

    const audioContext = audioContextRef.current;
    if (workletUrlRef.current) {
      URL.revokeObjectURL(workletUrlRef.current);
    }

    audioContextRef.current = null;
    analyserRef.current = null;
    sourceRef.current = null;
    workletRef.current = null;
    muteRef.current = null;
    streamRef.current = null;
    workletUrlRef.current = null;
    storeRef.current.setVoiceLevel(0);

    // Detach resources before awaiting so a newer capture cannot be cleared.
    const closing = audioContext?.close().catch((error: unknown) => {
      console.warn('[maho-ai] Failed to close voice audio context:', String(error));
    });
    if (stopBackend) {
      try {
        await voiceMojo.handler.stopVoiceSession();
      } catch (error: unknown) {
        if (error instanceof Error) {
          console.warn('[maho-ai] Failed to stop voice session:', error.message);
        } else {
          console.warn('[maho-ai] Failed to stop voice session:', String(error));
        }
      }
    }

    await closing;
    if (resetStatus && generation === captureGenerationRef.current) {
      storeRef.current.setVoiceStatus('idle');
    }
  }, [unbindVoiceListeners]);

  const stop = useCallback(async (): Promise<void> => {
    await cleanupCapture({resetStatus: true, stopBackend: true});
  }, [cleanupCapture]);

  const bindVoiceListeners = useCallback(() => {
    unbindVoiceListeners();
    listenerIdsRef.current = {
      partial: voiceMojo.router.onVoicePartial.addListener((transcript: string) => {
        storeRef.current.setVoiceTranscript(transcript);
      }),
      final: voiceMojo.router.onVoiceFinal.addListener((transcript: string) => {
        storeRef.current.setVoiceTranscript(transcript);
        onFinalTranscriptRef.current(transcript, {stop});
      }),
      error: voiceMojo.router.onVoiceError.addListener((message: string) => {
        storeRef.current.setVoiceError(message);
        void cleanupCapture({resetStatus: false, stopBackend: true});
      }),
    };
  }, [cleanupCapture, stop, unbindVoiceListeners]);

  const updateLevel = useCallback(() => {
    const analyser = analyserRef.current;
    if (!analyser) {
      return;
    }

    const samples = new Float32Array(analyser.fftSize);
    analyser.getFloatTimeDomainData(samples);
    let sum = 0;
    for (const sample of samples) {
      sum += sample * sample;
    }
    const rms = Math.sqrt(sum / samples.length);
    storeRef.current.setVoiceLevel(Math.min(1, rms * RMS_GAIN));
    animationFrameRef.current = requestAnimationFrame(updateLevel);
  }, []);

  const start = useCallback(async (): Promise<void> => {
    if (!hasVoiceCaptureSupport()) {
      storeRef.current.setVoiceError('Voice input is not supported in this WebUI host.');
      storeRef.current.setVoiceStatus('unsupported');
      return;
    }

    const cleanup = cleanupCapture({resetStatus: false, stopBackend: true});
    const generation = captureGenerationRef.current;
    await cleanup;
    if (generation !== captureGenerationRef.current) return;
    storeRef.current.setVoiceError(null);
    storeRef.current.setVoiceTranscript('');
    storeRef.current.setVoiceLevel(0);
    bindVoiceListeners();

    try {
      const {accepted} = await voiceMojo.handler.startVoiceSession();
      if (generation !== captureGenerationRef.current) return;
      if (!accepted) {
        storeRef.current.setVoiceError('Voice input is already listening.');
        await cleanupCapture({resetStatus: false, stopBackend: true});
        return;
      }

      const stream = await navigator.mediaDevices.getUserMedia({audio: true});
      if (generation !== captureGenerationRef.current) {
        stream.getTracks().forEach(track => track.stop());
        return;
      }
      streamRef.current = stream;
      const audioContext = new AudioContext();
      audioContextRef.current = audioContext;
      const workletUrl = createWorkletUrl();
      workletUrlRef.current = workletUrl;
      await audioContext.audioWorklet.addModule(workletUrl);
      if (generation !== captureGenerationRef.current) return;

      const source = audioContext.createMediaStreamSource(stream);
      sourceRef.current = source;
      const analyser = audioContext.createAnalyser();
      analyserRef.current = analyser;
      analyser.fftSize = 1024;
      const worklet = new AudioWorkletNode(audioContext, WORKLET_NAME, {
        numberOfInputs: 1,
        numberOfOutputs: 1,
        outputChannelCount: [1],
      });
      workletRef.current = worklet;
      const mute = audioContext.createGain();
      muteRef.current = mute;
      mute.gain.value = 0;

      worklet.port.onmessage = event => {
        if (!(event.data instanceof Float32Array)) {
          return;
        }
        void voiceMojo.handler.pushAudioChunk(Array.from(event.data));
      };

      source.connect(analyser);
      source.connect(worklet);
      worklet.connect(mute);
      mute.connect(audioContext.destination);

      storeRef.current.setVoiceStatus('listening');
      animationFrameRef.current = requestAnimationFrame(updateLevel);
    } catch (error: unknown) {
      if (generation !== captureGenerationRef.current) return;
      storeRef.current.setVoiceError(getVoiceCaptureErrorMessage(error));
      await cleanupCapture({resetStatus: false, stopBackend: true});
    }
  }, [bindVoiceListeners, cleanupCapture, updateLevel]);

  useEffect(() => {
    return () => {
      void cleanupCapture({resetStatus: true, stopBackend: true});
    };
  }, [cleanupCapture]);

  return {start, stop};
}
