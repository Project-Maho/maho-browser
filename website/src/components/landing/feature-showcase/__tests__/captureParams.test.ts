import { describe, test, expect } from "bun:test";
import {
  DEFAULT_CAPTURE_PARAMS,
  parseCaptureParams,
} from "../captureParams";

describe("capture parameter normalization", () => {
  test("returns typed values for a valid capture query", () => {
    const query = new URLSearchParams({
      scene: "command-bar",
      theme: "#ff6f91",
      p: "0.625",
      profile: "reference",
      surface: "showcase",
    });

    const result = parseCaptureParams(query);

    expect(result).toEqual({
      scene: "command-bar",
      theme: "#ff6f91",
      progress: 0.625,
      profile: "reference",
      surface: "showcase",
    });
  });

  test("normalizes unknown and prototype-like scenes to spaces", () => {
    for (const scene of ["unknown", "constructor", "__proto__", "toString"]) {
      const result = parseCaptureParams(new URLSearchParams({ scene }));

      expect(result.scene).toBe("spaces");
    }
  });

  test("normalizes the unreleased optional-ai scene key to spaces", () => {
    const result = parseCaptureParams(new URLSearchParams({ scene: "optional-ai" }));

    expect(result.scene).toBe("spaces");
  });

  test("normalizes malformed themes to the launch seed", () => {
    for (const theme of ["", "purple", "#12", "#1234567"]) {
      const result = parseCaptureParams(new URLSearchParams({ theme }));

      expect(result.theme).toBe(DEFAULT_CAPTURE_PARAMS.theme);
    }
  });

  test("parses finite progress and applies the existing clamp behavior", () => {
    expect(parseCaptureParams(new URLSearchParams({ p: "-1" })).progress).toBe(0);
    expect(parseCaptureParams(new URLSearchParams({ p: "0.4" })).progress).toBe(0.4);
    expect(parseCaptureParams(new URLSearchParams({ p: "2" })).progress).toBe(1);
  });

  test("normalizes non-finite and partially parsed progress to zero", () => {
    for (const progress of ["NaN", "Infinity", "-Infinity", "0.5extra"]) {
      const result = parseCaptureParams(new URLSearchParams({ p: progress }));

      expect(result.progress).toBe(0);
    }
  });

  test("normalizes invalid profiles to feature", () => {
    for (const profile of ["", "preview", "constructor"]) {
      const result = parseCaptureParams(new URLSearchParams({ profile }));

      expect(result.profile).toBe("feature");
    }
  });
});
