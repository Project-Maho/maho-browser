export interface CaptureImage {
  readonly complete: boolean;
  readonly naturalWidth?: number;
  addEventListener(type: "load" | "error", listener: EventListener, options?: boolean | AddEventListenerOptions): void;
  removeEventListener(type: "load" | "error", listener: EventListener, options?: boolean | EventListenerOptions): void;
}

export type ReadinessStage = "frame" | "fonts" | "images";

export type ReadinessResult =
  | { readonly ready: true }
  | { readonly ready: false; readonly stage: ReadinessStage; readonly error: string };

export type CaptureReadinessOptions = {
  readonly signal: AbortSignal;
  readonly fontsReady: Promise<unknown>;
  readonly getImages: () => readonly CaptureImage[];
  readonly requestFrame: (callback: FrameRequestCallback) => number;
  readonly cancelFrame: (handle: number) => void;
  readonly timeoutMs?: {
    readonly frame?: number;
    readonly fonts?: number;
    readonly images?: number;
  };
};

type TimerHandle = ReturnType<typeof setTimeout>;

type ImageListenerRecord = {
  readonly image: CaptureImage;
  readonly handleLoad: () => void;
  readonly handleError: () => void;
};

const DEFAULT_TIMEOUTS = {
  frame: 1000,
  fonts: 3000,
  images: 5000,
} as const;

function failure(stage: ReadinessStage, error: string): ReadinessResult {
  return { ready: false, stage, error };
}

function clearTimer(handle: TimerHandle | undefined): void {
  if (handle !== undefined) clearTimeout(handle);
}

function waitForNextFrame(options: CaptureReadinessOptions): Promise<ReadinessResult> {
  if (options.signal.aborted) return Promise.resolve(failure("frame", "Aborted"));

  return new Promise<ReadinessResult>((resolve) => {
    let frameHandle: number | undefined;
    let timeoutHandle: TimerHandle | undefined;
    let settled = false;

    const finish = (result: ReadinessResult): void => {
      if (settled) return;
      settled = true;
      clearTimer(timeoutHandle);
      options.signal.removeEventListener("abort", handleAbort);
      if (!result.ready && frameHandle !== undefined) options.cancelFrame(frameHandle);
      resolve(result);
    };
    const handleAbort = (): void => finish(failure("frame", "Aborted"));

    options.signal.addEventListener("abort", handleAbort, { once: true });
    if (options.signal.aborted) {
      handleAbort();
      return;
    }

    timeoutHandle = setTimeout(
      () => finish(failure("frame", "Frame timeout")),
      options.timeoutMs?.frame ?? DEFAULT_TIMEOUTS.frame,
    );

    frameHandle = options.requestFrame(() => finish({ ready: true }));

    if (options.signal.aborted) handleAbort();
  });
}

function waitForFonts(
  promise: Promise<unknown>,
  signal: AbortSignal,
  timeoutMs: number = DEFAULT_TIMEOUTS.fonts,
): Promise<ReadinessResult> {
  if (signal.aborted) return Promise.resolve(failure("fonts", "Aborted"));

  return new Promise<ReadinessResult>((resolve) => {
    let timeoutHandle: TimerHandle | undefined;
    let settled = false;

    const finish = (result: ReadinessResult): void => {
      if (settled) return;
      settled = true;
      clearTimer(timeoutHandle);
      signal.removeEventListener("abort", handleAbort);
      resolve(result);
    };
    const handleAbort = (): void => finish(failure("fonts", "Aborted"));

    signal.addEventListener("abort", handleAbort, { once: true });
    if (signal.aborted) {
      handleAbort();
      return;
    }

    timeoutHandle = setTimeout(
      () => finish(failure("fonts", "Fonts timeout")),
      timeoutMs,
    );
    void promise.then(
      () => finish({ ready: true }),
      (error: unknown) =>
        finish(failure("fonts", error instanceof Error ? error.message : "Fonts rejected")),
    );

    if (signal.aborted) handleAbort();
  });
}

function waitForImages(
  images: readonly CaptureImage[],
  signal: AbortSignal,
  timeoutMs: number = DEFAULT_TIMEOUTS.images,
): Promise<ReadinessResult> {
  if (signal.aborted) return Promise.resolve(failure("images", "Aborted"));

  const brokenImage = images.find((image) => image.complete && image.naturalWidth === 0);
  if (brokenImage !== undefined) {
    return Promise.resolve(failure("images", "Image completed with naturalWidth 0 (broken image)"));
  }

  const pendingImages = images.filter((image) => !image.complete);
  if (pendingImages.length === 0) return Promise.resolve({ ready: true });

  return new Promise<ReadinessResult>((resolve) => {
    const pending = new Set(pendingImages);
    const listeners: ImageListenerRecord[] = [];
    let timeoutHandle: TimerHandle | undefined;
    let settled = false;

    const cleanup = (): void => {
      clearTimer(timeoutHandle);
      for (const record of listeners) {
        record.image.removeEventListener("load", record.handleLoad);
        record.image.removeEventListener("error", record.handleError);
      }
      signal.removeEventListener("abort", handleAbort);
    };
    const finish = (result: ReadinessResult): void => {
      if (settled) return;
      settled = true;
      cleanup();
      resolve(result);
    };
    const handleAbort = (): void => finish(failure("images", "Aborted"));

    signal.addEventListener("abort", handleAbort, { once: true });
    if (signal.aborted) {
      handleAbort();
      return;
    }

    for (const image of pendingImages) {
      const handleLoad = (): void => {
        if (settled || !pending.delete(image)) return;
        if (image.naturalWidth === 0) {
          finish(failure("images", "Image loaded with naturalWidth 0"));
          return;
        }
        if (pending.size === 0) finish({ ready: true });
      };
      const handleError = (): void => finish(failure("images", "Image load error event"));
      listeners.push({ image, handleLoad, handleError });
      image.addEventListener("load", handleLoad, { once: true });
      image.addEventListener("error", handleError, { once: true });
    }

    timeoutHandle = setTimeout(() => finish(failure("images", "Image timeout")), timeoutMs);
    for (const record of listeners) {
      if (record.image.complete) record.handleLoad();
    }
    if (signal.aborted) handleAbort();
  });
}

export async function waitForCaptureReadiness(
  options: CaptureReadinessOptions,
): Promise<ReadinessResult> {
  const frameResult = await waitForNextFrame(options);
  if (!frameResult.ready) return frameResult;

  const fontResult = await waitForFonts(
    options.fontsReady,
    options.signal,
    options.timeoutMs?.fonts,
  );
  if (!fontResult.ready) return fontResult;

  return waitForImages(options.getImages(), options.signal, options.timeoutMs?.images);
}
