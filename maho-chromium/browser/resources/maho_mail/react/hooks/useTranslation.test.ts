import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import {
  useTranslation,
  getTranslationTargetLang,
  saveTranslationTargetLang,
} from "./useTranslation";

vi.mock("../api", () => ({
  translateText: vi.fn(),
  translateLocal: vi.fn(),
}));

import * as api from "../api";

const mockTranslateText = vi.mocked(api.translateText);
const mockTranslateLocal = vi.mocked(api.translateLocal);

function setConfig(provider: string) {
  localStorage.setItem(
    "maho-translation-config",
    JSON.stringify({ provider, defaultTargetLang: "ko", alwaysTranslateFrom: [] }),
  );
}

describe("useTranslation", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    localStorage.clear();
  });

  it("returns initial state", () => {
    const { result } = renderHook(() => useTranslation());
    expect(result.current.translatedText).toBeNull();
    expect(result.current.isTranslating).toBe(false);
    expect(result.current.error).toBeNull();
    expect(result.current.fromLang).toBe("en");
    // No saved config: follows the browser language (jsdom reports en-US).
    expect(result.current.toLang).toBe("en");
    expect(result.current.isModelLoading).toBe(false);
  });

  it("setFromLang clears translatedText", () => {
    const { result } = renderHook(() => useTranslation());
    act(() => { result.current.setFromLang("ja"); });
    expect(result.current.fromLang).toBe("ja");
    expect(result.current.translatedText).toBeNull();
  });

  it("setToLang clears translatedText", () => {
    const { result } = renderHook(() => useTranslation());
    act(() => { result.current.setToLang("fr"); });
    expect(result.current.toLang).toBe("fr");
    expect(result.current.translatedText).toBeNull();
  });

  it("empty text is a no-op", async () => {
    setConfig("byok");
    const { result } = renderHook(() => useTranslation());
    await act(async () => { await result.current.translate("   "); });
    expect(mockTranslateText).not.toHaveBeenCalled();
    expect(result.current.isTranslating).toBe(false);
  });

  it("same source and target language sets error", async () => {
    setConfig("byok");
    const { result } = renderHook(() => useTranslation());
    await act(async () => { await result.current.translate("Hello", "en", "en"); });
    expect(result.current.error).toBe("Source and target languages must differ");
    expect(mockTranslateText).not.toHaveBeenCalled();
  });

  it("BYOK happy path calls translateText", async () => {
    setConfig("byok");
    mockTranslateText.mockResolvedValue("안녕하세요");
    const { result } = renderHook(() => useTranslation());
    await act(async () => { await result.current.translate("Hello", "en", "ko"); });
    expect(mockTranslateText).toHaveBeenCalledWith({ text: "Hello", targetLang: "ko", sourceLang: "en" });
    expect(result.current.translatedText).toBe("안녕하세요");
    expect(result.current.isTranslating).toBe(false);
  });

  it("skip mode sets error without calling API", async () => {
    setConfig("skip");
    const { result } = renderHook(() => useTranslation());
    await act(async () => { await result.current.translate("Hello", "en", "ko"); });
    expect(result.current.error).toBe("Translation is disabled in Settings.");
    expect(mockTranslateText).not.toHaveBeenCalled();
    expect(mockTranslateLocal).not.toHaveBeenCalled();
  });

  it("local mode calls translateLocal", async () => {
    setConfig("local");
    mockTranslateLocal.mockResolvedValue("こんにちは");
    const { result } = renderHook(() => useTranslation());
    await act(async () => { await result.current.translate("Hello", "en", "ja"); });
    expect(mockTranslateLocal).toHaveBeenCalledWith({ text: "Hello", targetLang: "ja", sourceLang: "en" });
    expect(result.current.translatedText).toBe("こんにちは");
  });

  it("API error sets error state", async () => {
    setConfig("byok");
    mockTranslateText.mockRejectedValue(new Error("Network timeout"));
    const { result } = renderHook(() => useTranslation());
    await act(async () => { await result.current.translate("Hello", "en", "ko"); });
    expect(result.current.error).toBe("Network timeout");
    expect(result.current.translatedText).toBeNull();
    expect(result.current.isTranslating).toBe(false);
  });

  it("clearTranslation resets text and error", async () => {
    setConfig("byok");
    mockTranslateText.mockResolvedValue("Hola");
    const { result } = renderHook(() => useTranslation());
    await act(async () => { await result.current.translate("Hello", "en", "es"); });
    expect(result.current.translatedText).toBe("Hola");
    act(() => { result.current.clearTranslation(); });
    expect(result.current.translatedText).toBeNull();
    expect(result.current.error).toBeNull();
  });

  it("cleanup is a no-op and does not throw", () => {
    const { result } = renderHook(() => useTranslation());
    expect(() => { result.current.cleanup(); }).not.toThrow();
  });

  it("exports SUPPORTED_LANGUAGES with 12 entries", () => {
    const { result } = renderHook(() => useTranslation());
    expect(result.current.SUPPORTED_LANGUAGES).toHaveLength(12);
    expect(result.current.SUPPORTED_LANGUAGES[0].code).toBe("en");
  });

  it("initial target comes from the saved config", () => {
    localStorage.setItem(
      "maho-translation-config",
      JSON.stringify({ provider: "byok", defaultTargetLang: "ja", alwaysTranslateFrom: [] }),
    );
    const { result } = renderHook(() => useTranslation());
    expect(result.current.toLang).toBe("ja");
  });

  it("without a saved config the target follows the browser language", () => {
    const spy = vi.spyOn(navigator, "language", "get").mockReturnValue("fr-FR");
    expect(getTranslationTargetLang()).toBe("fr");
    spy.mockReturnValue("xx-YY");
    expect(getTranslationTargetLang()).toBe("en");
    spy.mockRestore();
  });

  it("saveTranslationTargetLang persists the choice and keeps the provider", () => {
    setConfig("local");
    saveTranslationTargetLang("de");
    expect(getTranslationTargetLang()).toBe("de");
    const saved = JSON.parse(localStorage.getItem("maho-translation-config") ?? "{}");
    expect(saved).toMatchObject({ provider: "local", defaultTargetLang: "de" });
  });
});
