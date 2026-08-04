(() => {
  "use strict";

  const contract = {
    schemaVersion: 1,
    compositionIds: ["spaces", "split-view", "command-ai"],
    canvas: { width: 1920, height: 1080, fps: 30 },
    source: { width: 2818, height: 2060 },
    crop: { x: 68, y: 52, width: 2682, height: 1924 },
    browser: {
      width: 1440,
      height: 1033,
      left: 240,
      top: 23.5,
      right: 1680,
      bottom: 1056.5,
      centerX: 960,
      centerY: 540,
    },
    renderedVideo: {
      width: 1513.020134228188,
      height: 1106.018711018711,
      left: -36.51006711409396,
      top: -27.91891891891892,
    },
    surface: {
      radius: 17,
      cornerShape: "superellipse(1)",
      underlay: "#0a0a0a",
      edge: "1px solid rgba(250, 250, 250, 0.12)",
      shadow: "0 0 0 1px rgba(250, 250, 250, 0.08) inset, 0 24px 48px rgba(10, 10, 10, 0.28)",
    },
    assets: {
      gsap: "../node_modules/gsap/dist/gsap.min.js",
      background: "../assets/originals/water-lilies.jpg",
      videos: {
        spaces: "../assets/originals/spaces.mov",
        "split-view": "../assets/originals/split-view.mov",
        "command-ai": "../assets/originals/command-ai.mov",
      },
    },
    camera: {
      wrapperAttribute: "data-maho-camera-wrapper",
      animatedProperties: ["x", "y", "scale", "opacity"],
    },
  };

  function deepFreeze(value) {
    for (const child of Object.values(value)) {
      if (child !== null && typeof child === "object") deepFreeze(child);
    }
    return Object.freeze(value);
  }

  deepFreeze(contract);

  const compositionIds = new Set(contract.compositionIds);
  const cameraKeys = new Set(contract.camera.animatedProperties);
  const tweenKeys = new Set([...cameraKeys, "duration", "ease", "immediateRender", "overwrite"]);

  function assert(condition, message) {
    if (!condition) throw new Error(`Maho hero frame contract: ${message}`);
  }

  function validateMotionValue(key, value) {
    if (key === "ease") {
      assert(typeof value === "string" && value.length > 0, "ease must be a non-empty string");
      return;
    }
    if (key === "immediateRender") {
      assert(typeof value === "boolean", "immediateRender must be boolean");
      return;
    }
    if (key === "overwrite") {
      assert(typeof value === "boolean" || value === "auto", "overwrite must be boolean or auto");
      return;
    }
    assert(typeof value === "number" && Number.isFinite(value), `${key} must be finite`);
    if (key === "scale") assert(value > 0, "scale must be positive");
    if (key === "opacity") assert(value >= 0 && value <= 1, "opacity must be between 0 and 1");
    if (key === "duration") assert(value >= 0, "duration must be non-negative");
  }

  function validateVars(vars, allowedKeys, requireMotion) {
    assert(vars !== null && typeof vars === "object" && !Array.isArray(vars), "camera vars must be an object");
    let hasMotion = false;
    for (const [key, value] of Object.entries(vars)) {
      assert(allowedKeys.has(key), `forbidden camera property: ${key}`);
      validateMotionValue(key, value);
      if (cameraKeys.has(key)) hasMotion = true;
    }
    if (requireMotion) assert(hasMotion, "camera tween requires at least one camera property");
    return Object.freeze({ ...vars });
  }

  function assetPath(assetId) {
    if (assetId === "gsap" || assetId === "background") return contract.assets[assetId];
    assert(compositionIds.has(assetId), `unknown local asset: ${assetId}`);
    return contract.assets.videos[assetId];
  }

  function registerCameraTimeline(compositionId, build, environment = globalThis) {
    assert(compositionIds.has(compositionId), `unsupported composition ID: ${compositionId}`);
    assert(typeof build === "function", "timeline builder must be a function");
    const root = environment.document?.querySelector(`[data-composition-id="${compositionId}"]`);
    assert(root !== null && root !== undefined, `composition root not found: ${compositionId}`);
    assert(root.dataset.width === String(contract.canvas.width), "root data-width differs from contract");
    assert(root.dataset.height === String(contract.canvas.height), "root data-height differs from contract");
    assert(root.dataset.fps === String(contract.canvas.fps), "root data-fps differs from contract");

    const wrapperSelector = `[data-maho-camera-wrapper="${compositionId}"]`;
    const wrapper = environment.document.querySelector(wrapperSelector);
    assert(wrapper !== null, `camera wrapper not found: ${compositionId}`);
    for (const attribute of ["data-start", "data-duration", "data-track-index"]) {
      assert(!wrapper.hasAttribute(attribute), `camera wrapper must remain untimed: ${attribute}`);
    }

    const timelines = environment.window.__timelines ?? {};
    assert(timelines[compositionId] === undefined, `timeline already registered: ${compositionId}`);
    assert(typeof environment.gsap?.timeline === "function", "pinned local GSAP runtime is unavailable");
    const timeline = environment.gsap.timeline({ paused: true });
    let constructionActive = true;
    const assertConstructionActive = () => {
      assert(constructionActive, "camera facade construction window is closed");
    };
    const camera = {};
    camera.to = (vars, position) => {
      assertConstructionActive();
      timeline.to(wrapperSelector, validateVars(vars, tweenKeys, true), position);
      return camera;
    };
    camera.fromTo = (fromVars, toVars, position) => {
      assertConstructionActive();
      timeline.fromTo(
        wrapperSelector,
        validateVars(fromVars, cameraKeys, true),
        validateVars(toVars, tweenKeys, true),
        position,
      );
      return camera;
    };
    camera.set = (vars, position) => {
      assertConstructionActive();
      timeline.set(wrapperSelector, validateVars(vars, cameraKeys, true), position);
      return camera;
    };
    camera.addLabel = (label, position) => {
      assertConstructionActive();
      assert(typeof label === "string" && label.length > 0, "timeline label must be non-empty");
      timeline.addLabel(label, position);
      return camera;
    };
    Object.freeze(camera);

    try {
      const result = build(camera);
      assert(result === undefined || result === camera, "timeline builder must finish synchronously");
      assert(typeof timeline.paused === "function" && timeline.paused() === true, "timeline must remain paused");
      environment.window.__timelines = timelines;
      timelines[compositionId] = timeline;
    } finally {
      constructionActive = false;
    }
  }

  globalThis.MahoHeroFrame = Object.freeze({
    contract,
    assetPath,
    registerCameraTimeline,
  });
})();
