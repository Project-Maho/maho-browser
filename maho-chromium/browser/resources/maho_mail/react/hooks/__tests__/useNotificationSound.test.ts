import { renderHook, act } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";

const { playMock, audioInstances } = vi.hoisted(() => {
  const playMock = vi.fn();
  const audioInstances: Array<{ currentTime: number; preload: string; volume: number; play: typeof playMock }> = [];

  class MockAudio {
    currentTime = 123;
    preload = "";
    volume = 1;
    play = playMock;

    constructor(_src: string) {
      audioInstances.push(this);
    }
  }

  vi.stubGlobal("Audio", MockAudio);

  return { playMock, audioInstances };
});

import { useNotificationSound } from "../useNotificationSound";

describe("useNotificationSound", () => {
  beforeEach(() => {
    playMock.mockReset();
    playMock.mockResolvedValue(undefined);
  });

  it("does not play when disabled", () => {
    const { result } = renderHook(() => useNotificationSound(false));

    act(() => {
      result.current.playSound();
    });

    expect(playMock).not.toHaveBeenCalled();
  });

  it("resets playback and plays the shared audio element when enabled", () => {
    const { result } = renderHook(() => useNotificationSound(true));

    act(() => {
      result.current.playSound();
    });

    expect(playMock).toHaveBeenCalledTimes(1);
    expect(audioInstances).toHaveLength(1);
    expect(audioInstances[0]?.currentTime).toBe(0);
    expect(audioInstances[0]?.preload).toBe("auto");
    expect(audioInstances[0]?.volume).toBe(0.5);
  });

  it("silently handles play rejection", async () => {
    playMock.mockRejectedValueOnce(new Error("blocked"));
    const { result } = renderHook(() => useNotificationSound(true));

    act(() => {
      result.current.playSound();
    });

    await Promise.resolve();

    expect(playMock).toHaveBeenCalledTimes(1);
  });
});
