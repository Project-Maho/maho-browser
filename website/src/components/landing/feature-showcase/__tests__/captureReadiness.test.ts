import { describe, expect, test } from "bun:test";
import {
  type CaptureImage,
  type CaptureReadinessOptions,
  waitForCaptureReadiness,
} from "../captureReadiness";

type Deferred = {
  readonly promise: Promise<void>;
  readonly resolve: () => void;
  readonly reject: (error: Error) => void;
};

function createDeferred(): Deferred {
  let resolve = () => undefined;
  let reject = (_error: Error) => undefined;
  const promise = new Promise<void>((done, fail) => {
    resolve = done;
    reject = fail;
  });
  return { promise, resolve, reject };
}

async function flushMicrotasks(): Promise<void> {
  for (let turn = 0; turn < 3; turn += 1) await Promise.resolve();
}

class FakeAbortSignal {
  aborted = false;
  addCount = 0;
  removeCount = 0;
  private readonly listeners = new Set<EventListener>();

  addEventListener(_type: "abort", listener: EventListener): void {
    this.addCount += 1; this.listeners.add(listener);
  }

  removeEventListener(_type: "abort", listener: EventListener): void {
    this.removeCount += 1; this.listeners.delete(listener);
  }

  abort(): void {
    if (this.aborted) return;
    this.aborted = true;
    for (const listener of [...this.listeners]) listener(new Event("abort"));
  }

  listenerCount(): number {
    return this.listeners.size;
  }
}

class FakeCaptureImage implements CaptureImage {
  readonly listeners = new Map<"load" | "error", Set<EventListener>>();
  complete = false;
  naturalWidth = 100;
  addCount = 0;
  removeCount = 0;

  addEventListener(type: "load" | "error", listener: EventListener): void {
    this.addCount += 1;
    const listeners = this.listeners.get(type) ?? new Set<EventListener>();
    listeners.add(listener);
    this.listeners.set(type, listeners);
  }

  removeEventListener(type: "load" | "error", listener: EventListener): void {
    this.removeCount += 1; this.listeners.get(type)?.delete(listener);
  }

  emit(type: "load" | "error"): void {
    for (const listener of [...(this.listeners.get(type) ?? [])]) listener(new Event(type));
  }

  listenerCount(): number {
    return (this.listeners.get("load")?.size ?? 0) + (this.listeners.get("error")?.size ?? 0);
  }
}

function createFrameScheduler() {
  let activeCallback: FrameRequestCallback | null = null;
  let lastCallback: FrameRequestCallback | null = null;
  let cancelCount = 0;
  return {
    requestFrame: (callback: FrameRequestCallback): number => {
      activeCallback = callback;
      lastCallback = callback;
      return 1;
    },
    cancelFrame: (): void => {
      cancelCount += 1;
      activeCallback = null;
    },
    flush: (): void => {
      const callback = activeCallback;
      activeCallback = null;
      callback?.(0);
    },
    flushLate: (): void => lastCallback?.(0),
    get cancelCount(): number {
      return cancelCount;
    },
    get hasPendingFrame(): boolean {
      return activeCallback !== null;
    },
  };
}

type ScenarioOptions = {
  readonly fontsReady?: Promise<unknown>;
  readonly images?: readonly CaptureImage[];
  readonly timeoutMs?: CaptureReadinessOptions["timeoutMs"];
};

function startScenario(options: ScenarioOptions = {}) {
  const frames = createFrameScheduler();
  const signal = new FakeAbortSignal();
  const readiness = waitForCaptureReadiness({
    signal,
    fontsReady: options.fontsReady ?? Promise.resolve(),
    getImages: () => options.images ?? [],
    requestFrame: frames.requestFrame,
    cancelFrame: frames.cancelFrame,
    timeoutMs: options.timeoutMs,
  });
  return { frames, signal, readiness };
}

describe("capture readiness", () => {
  test("reports frame timeout, cancels the frame, and ignores late completion", async () => {
    const scenario = startScenario({ timeoutMs: { frame: 1 } });
    let settlements = 0;
    const readiness = scenario.readiness.then((result) => {
      settlements += 1;
      return result;
    });
    expect(await readiness).toEqual({ ready: false, stage: "frame", error: "Frame timeout" });
    expect(scenario.frames.cancelCount).toBe(1);
    expect(scenario.frames.hasPendingFrame).toBe(false);
    expect(scenario.signal.listenerCount()).toBe(0);
    scenario.frames.flushLate();
    await flushMicrotasks();
    expect(settlements).toBe(1);
  });

  test("reports frame abort, cancels the pending frame once, and ignores its callback", async () => {
    const scenario = startScenario();
    scenario.signal.abort();
    expect(await scenario.readiness).toEqual({ ready: false, stage: "frame", error: "Aborted" });
    expect([scenario.frames.cancelCount, scenario.signal.listenerCount()]).toEqual([1, 0]);
    scenario.frames.flushLate();
    expect(scenario.frames.cancelCount).toBe(1);
  });

  test("reports font rejection at the fonts stage", async () => {
    const fonts = createDeferred();
    const scenario = startScenario({ fontsReady: fonts.promise });
    scenario.frames.flush();
    await flushMicrotasks();
    fonts.reject(new Error("FontFaceSet rejected"));
    expect(await scenario.readiness).toEqual({ ready: false, stage: "fonts", error: "FontFaceSet rejected" });
    expect(scenario.signal.listenerCount()).toBe(0);
  });

  test("reports font timeout and ignores late font completion", async () => {
    const fonts = createDeferred();
    const scenario = startScenario({ fontsReady: fonts.promise, timeoutMs: { fonts: 1 } });
    let settlements = 0;
    const readiness = scenario.readiness.then((result) => {
      settlements += 1;
      return result;
    });
    scenario.frames.flush();
    expect(await readiness).toEqual({ ready: false, stage: "fonts", error: "Fonts timeout" });
    fonts.resolve();
    await flushMicrotasks();
    expect(settlements).toBe(1);
    expect(scenario.signal.addCount).toBe(scenario.signal.removeCount);
  });

  test("reports abort during fonts and removes the current abort listener", async () => {
    const fonts = createDeferred();
    const scenario = startScenario({ fontsReady: fonts.promise });
    scenario.frames.flush();
    await flushMicrotasks();
    scenario.signal.abort();
    expect(await scenario.readiness).toEqual({ ready: false, stage: "fonts", error: "Aborted" });
    expect(scenario.signal.listenerCount()).toBe(0);
    expect(scenario.signal.addCount).toBe(scenario.signal.removeCount);
    fonts.resolve();
    await flushMicrotasks();
    expect(scenario.signal.removeCount).toBe(2);
  });

  test("reports image timeout and removes image and abort listeners exactly once", async () => {
    const image = new FakeCaptureImage();
    const scenario = startScenario({ images: [image], timeoutMs: { images: 1 } });
    let settlements = 0;
    const readiness = scenario.readiness.then((result) => {
      settlements += 1;
      return result;
    });
    scenario.frames.flush();
    expect(await readiness).toEqual({ ready: false, stage: "images", error: "Image timeout" });
    expect([image.listenerCount(), image.addCount, image.removeCount]).toEqual([0, 2, 2]);
    expect(scenario.signal.listenerCount()).toBe(0);
    image.emit("load");
    scenario.signal.abort();
    await flushMicrotasks();
    expect([settlements, image.removeCount]).toEqual([1, 2]);
  });

  test("reports abort during image waiting and cleans every listener", async () => {
    const image = new FakeCaptureImage();
    const scenario = startScenario({ images: [image] });
    scenario.frames.flush();
    await flushMicrotasks();
    expect(image.listenerCount()).toBe(2);
    scenario.signal.abort();
    expect(await scenario.readiness).toEqual({ ready: false, stage: "images", error: "Aborted" });
    expect([image.listenerCount(), image.removeCount, scenario.signal.listenerCount()]).toEqual([0, 2, 0]);
    image.emit("load");
    expect(image.removeCount).toBe(2);
  });

  test("fails immediately for an already-complete broken image", async () => {
    const image = new FakeCaptureImage();
    image.complete = true;
    image.naturalWidth = 0;
    const scenario = startScenario({ images: [image] });
    scenario.frames.flush();
    expect(await scenario.readiness).toEqual({
      ready: false,
      stage: "images",
      error: "Image completed with naturalWidth 0 (broken image)",
    });
    expect(image.addCount).toBe(0);
  });

  test("waits for the frame, fonts, and every current image before succeeding", async () => {
    const fonts = createDeferred();
    const firstImage = new FakeCaptureImage();
    const secondImage = new FakeCaptureImage();
    const scenario = startScenario({ fontsReady: fonts.promise, images: [firstImage, secondImage] });
    let isReady = false;
    void scenario.readiness.then((result) => {
      isReady = result.ready;
    });
    await flushMicrotasks();
    scenario.frames.flush();
    await flushMicrotasks();
    fonts.resolve();
    await flushMicrotasks();
    firstImage.emit("load");
    await flushMicrotasks();
    expect(isReady).toBe(false);
    secondImage.emit("load");
    expect(await scenario.readiness).toEqual({ ready: true });
    expect([firstImage.listenerCount(), secondImage.listenerCount()]).toEqual([0, 0]);
  });

  test("image failure cleans sibling listeners before returning", async () => {
    const failedImage = new FakeCaptureImage();
    const pendingImage = new FakeCaptureImage();
    const scenario = startScenario({ images: [failedImage, pendingImage] });
    scenario.frames.flush();
    await flushMicrotasks();
    failedImage.emit("error");
    expect(await scenario.readiness).toEqual({ ready: false, stage: "images", error: "Image load error event" });
    expect([pendingImage.listenerCount(), pendingImage.removeCount]).toEqual([0, 2]);
    pendingImage.emit("load");
    scenario.signal.abort();
    expect(pendingImage.removeCount).toBe(2);
  });
});
