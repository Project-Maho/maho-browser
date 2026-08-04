function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function errorMessage(error) {
  return error instanceof Error ? error.message : String(error);
}

function expectThrow(operation, expectedMessage) {
  let received = "";
  try {
    operation();
  } catch (error) {
    received = errorMessage(error);
  }
  assert(received.includes(expectedMessage), `expected "${expectedMessage}", received "${received}"`);
}

function createEnvironment(compositionId) {
  const calls = [];
  let paused = true;
  const timeline = {
    paused: () => paused,
    play: () => { paused = false; },
    pause: () => { paused = true; },
    to: (...args) => calls.push(["to", ...args]),
    fromTo: (...args) => calls.push(["fromTo", ...args]),
    set: (...args) => calls.push(["set", ...args]),
    addLabel: (...args) => calls.push(["addLabel", ...args]),
  };
  const root = {
    dataset: { compositionId, width: "1920", height: "1080", fps: "30" },
  };
  const wrapper = { hasAttribute: () => false };
  const environment = {
    gsap: {
      timeline: (options) => {
        paused = options.paused;
        calls.push(["timeline", options]);
        return timeline;
      },
    },
    document: {
      querySelector: (selector) => (selector.includes("camera-wrapper") ? wrapper : root),
    },
    window: {},
  };
  return { calls, environment, timeline };
}

async function runControl(name, operation) {
  try {
    await operation();
    return { name, result: "PASS" };
  } catch (error) {
    return { name, result: "FAIL", error: errorMessage(error) };
  }
}

export async function runRuntimeControls(runtime) {
  const controls = [
    runControl("single-paused-wrapper-timeline", () => {
      const { calls, environment, timeline } = createEnvironment("spaces");
      runtime.registerCameraTimeline(
        "spaces",
        (camera) => camera.to({ x: 12, y: -8, scale: 1.1, opacity: 1, duration: 1 }, 0),
        environment,
      );
      assert(environment.window.__timelines.spaces === timeline, "timeline was not registered by composition ID");
      assert(timeline.paused() === true, "registered timeline is not paused");
      assert(calls.filter(([kind]) => kind === "timeline").length === 1, "expected exactly one timeline creation");
      assert(calls.some(([kind, target]) => kind === "to" && target === '[data-maho-camera-wrapper="spaces"]'), "camera did not target the wrapper");
    }),
    runControl("registration-returns-no-raw-timeline", () => {
      const { environment } = createEnvironment("spaces");
      const result = runtime.registerCameraTimeline("spaces", () => {}, environment);
      assert(result === undefined, "registerCameraTimeline exposed a return value");
    }),
    runControl("registration-result-has-no-play-pause", () => {
      const { environment } = createEnvironment("spaces");
      const result = runtime.registerCameraTimeline("spaces", () => {}, environment);
      assert(result?.play === undefined && result?.pause === undefined, "registration exposed play/pause controls");
    }),
    runControl("facade-has-no-play-pause", () => {
      const { environment } = createEnvironment("spaces");
      let captured;
      runtime.registerCameraTimeline("spaces", (camera) => { captured = camera; }, environment);
      assert(captured.play === undefined && captured.pause === undefined, "camera facade exposed play/pause controls");
    }),
    runControl("post-builder-tween-rejected", () => {
      const { environment } = createEnvironment("spaces");
      let captured;
      runtime.registerCameraTimeline("spaces", (camera) => { captured = camera; }, environment);
      expectThrow(() => captured.to({ x: 1, duration: 1 }, 0), "construction window is closed");
    }),
    runControl("post-builder-label-rejected", () => {
      const { environment } = createEnvironment("spaces");
      let captured;
      runtime.registerCameraTimeline("spaces", (camera) => { captured = camera; }, environment);
      expectThrow(() => captured.addLabel("late", 1), "construction window is closed");
    }),
    runControl("microtask-mutation-rejected", async () => {
      const { environment } = createEnvironment("spaces");
      let received = "";
      runtime.registerCameraTimeline("spaces", (camera) => {
        queueMicrotask(() => {
          try { camera.to({ y: 1, duration: 1 }, 0); } catch (error) { received = errorMessage(error); }
        });
      }, environment);
      await Promise.resolve();
      assert(received.includes("construction window is closed"), `microtask mutation was not rejected: ${received}`);
    }),
    runControl("timer-callback-mutation-rejected", () => {
      const { environment } = createEnvironment("spaces");
      let callback;
      runtime.registerCameraTimeline("spaces", (camera) => { callback = () => camera.to({ scale: 1.1, duration: 1 }, 0); }, environment);
      expectThrow(callback, "construction window is closed");
    }),
    runControl("event-callback-mutation-rejected", () => {
      const { environment } = createEnvironment("spaces");
      let listener;
      runtime.registerCameraTimeline("spaces", (camera) => { listener = () => camera.set({ opacity: 0.5 }, 0); }, environment);
      expectThrow(listener, "construction window is closed");
    }),
    runControl("promise-mutation-rejected", async () => {
      const { environment } = createEnvironment("spaces");
      let received = "";
      runtime.registerCameraTimeline("spaces", (camera) => {
        Promise.resolve().then(() => {
          try { camera.to({ opacity: 0.5, duration: 1 }, 0); } catch (error) { received = errorMessage(error); }
        });
      }, environment);
      await Promise.resolve();
      assert(received.includes("construction window is closed"), `Promise mutation was not rejected: ${received}`);
    }),
    runControl("duration-only-to-rejected", () => {
      const { environment } = createEnvironment("spaces");
      expectThrow(
        () => runtime.registerCameraTimeline("spaces", (camera) => camera.to({ duration: 3, ease: "none" }, 0), environment),
        "at least one camera property",
      );
    }),
    runControl("empty-from-vars-rejected", () => {
      const { environment } = createEnvironment("spaces");
      expectThrow(
        () => runtime.registerCameraTimeline("spaces", (camera) => camera.fromTo({}, { x: 4, duration: 1 }, 0), environment),
        "at least one camera property",
      );
    }),
    runControl("duration-only-to-vars-rejected", () => {
      const { environment } = createEnvironment("spaces");
      expectThrow(
        () => runtime.registerCameraTimeline("spaces", (camera) => camera.fromTo({ x: 0 }, { duration: 1, ease: "none" }, 0), environment),
        "at least one camera property",
      );
    }),
    runControl("async-builder-rejected", () => {
      const { environment } = createEnvironment("spaces");
      expectThrow(() => runtime.registerCameraTimeline("spaces", async () => {}, environment), "finish synchronously");
    }),
    runControl("promise-builder-rejected", () => {
      const { environment } = createEnvironment("spaces");
      expectThrow(() => runtime.registerCameraTimeline("spaces", () => Promise.resolve(), environment), "finish synchronously");
    }),
    runControl("async-post-await-mutation-rejected", async () => {
      const { environment } = createEnvironment("spaces");
      let received = "";
      expectThrow(
        () => runtime.registerCameraTimeline("spaces", async (camera) => {
          await Promise.resolve();
          try { camera.to({ x: 1, duration: 1 }, 0); } catch (error) { received = errorMessage(error); }
        }, environment),
        "finish synchronously",
      );
      await Promise.resolve();
      assert(received.includes("construction window is closed"), `async mutation was not rejected: ${received}`);
    }),
    runControl("forbidden-geometry-property-rejected", () => {
      const { environment } = createEnvironment("spaces");
      expectThrow(
        () => runtime.registerCameraTimeline("spaces", (camera) => camera.to({ width: 100, duration: 1 }, 0), environment),
        "forbidden camera property",
      );
    }),
    runControl("duplicate-registration-rejected", () => {
      const { environment } = createEnvironment("spaces");
      runtime.registerCameraTimeline("spaces", () => {}, environment);
      expectThrow(() => runtime.registerCameraTimeline("spaces", () => {}, environment), "already registered");
    }),
  ];
  const results = await Promise.all(controls);
  const failures = results.filter((result) => result.result === "FAIL");
  assert(failures.length === 0, `runtime controls failed: ${JSON.stringify(failures)}`);
  return results;
}
