import { useState, useCallback } from "react";
import * as api from "../api";
import type { TranslationConfig, TranslationProvider } from "../types";

export const SUPPORTED_LANGUAGES = [
  { code: "en", name: "English" },
  { code: "ko", name: "Korean" },
  { code: "ja", name: "Japanese" },
  { code: "zh", name: "Chinese" },
  { code: "es", name: "Spanish" },
  { code: "fr", name: "French" },
  { code: "de", name: "German" },
  { code: "pt", name: "Portuguese" },
  { code: "ru", name: "Russian" },
  { code: "it", name: "Italian" },
  { code: "ar", name: "Arabic" },
  { code: "hi", name: "Hindi" },
] as const;

export const SUPPORTED_LANGUAGE_CODES = SUPPORTED_LANGUAGES.map((l) => l.code);

export type LanguageCode = (typeof SUPPORTED_LANGUAGES)[number]["code"];

const STORAGE_KEY = "maho-translation-config";

function isSupportedLanguage(code: string | undefined): code is LanguageCode {
  return !!code && (SUPPORTED_LANGUAGE_CODES as readonly string[]).includes(code);
}

// The user's browser language (e.g. "ko-KR" -> "ko"), or English when Maho
// Mail cannot translate into it.
function browserLanguage(): LanguageCode {
  const base = (typeof navigator !== "undefined" ? navigator.language : "")
    .split("-")[0]
    ?.toLowerCase();
  return isSupportedLanguage(base) ? base : "en";
}

const DEFAULT_CONFIG: TranslationConfig = {
  provider: "byok",
  defaultTargetLang: "en",
  alwaysTranslateFrom: [],
};

function loadConfig(): TranslationConfig {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) return JSON.parse(raw) as TranslationConfig;
  } catch {
    // Fall through
  }
  return { ...DEFAULT_CONFIG, defaultTargetLang: browserLanguage() };
}

// The language mail is translated into: the saved choice, else the browser
// language. Previously the manual Translate action ignored the saved choice and
// always used a hardcoded Korean target that the UI offered no way to change.
export function getTranslationTargetLang(): LanguageCode {
  const saved = loadConfig().defaultTargetLang;
  return isSupportedLanguage(saved) ? saved : browserLanguage();
}

// Persists the target language picked in the translation bar.
export function saveTranslationTargetLang(lang: LanguageCode): void {
  const config = { ...loadConfig(), defaultTargetLang: lang };
  localStorage.setItem(STORAGE_KEY, JSON.stringify(config));
  window.dispatchEvent(new Event("maho-translation-config-changed"));
}

interface TranslationState {
  translatedText: string | null;
  translatedHtml: string | null;
  isTranslating: boolean;
  error: string | null;
  fromLang: LanguageCode;
  toLang: LanguageCode;
  isModelLoading: boolean;
  partialFailureCount: number;
}

export function useTranslation() {
  const [state, setState] = useState<TranslationState>(() => ({
    translatedText: null,
    translatedHtml: null,
    isTranslating: false,
    error: null,
    fromLang: "en",
    toLang: getTranslationTargetLang(),
    isModelLoading: false,
    partialFailureCount: 0,
  }));

  const translate = useCallback(async (text: string, from?: string, to?: string) => {
    const config = loadConfig();
    const fromLang = (from ?? state.fromLang) as LanguageCode;
    const toLang = (to ?? state.toLang) as LanguageCode;

    if (!text.trim()) return;

    if (fromLang === toLang) {
      setState((s) => ({ ...s, error: "Source and target languages must differ" }));
      return;
    }

    if (config.provider === "skip") {
      setState((s) => ({ ...s, error: "Translation is disabled in Settings." }));
      return;
    }

    setState((s) => ({ ...s, isTranslating: true, error: null, fromLang, toLang }));

    try {
      let result: string;
      if (config.provider === "local") {
        result = await api.translateLocal({ text, targetLang: toLang, sourceLang: fromLang });
      } else {
        result = await api.translateText({ text, targetLang: toLang, sourceLang: fromLang });
      }
      setState((s) => ({ ...s, translatedText: result, translatedHtml: result, isTranslating: false }));
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      setState((s) => ({ ...s, error: message, isTranslating: false }));
    }
  }, [state.fromLang, state.toLang]);

  const translateHtml = useCallback(async (html: string, from?: string, to?: string) => {
    await translate(html, from, to);
  }, [translate]);

  const setFromLang = useCallback((lang: LanguageCode) => {
    setState((s) => ({ ...s, fromLang: lang, translatedText: null, translatedHtml: null }));
  }, []);

  const setToLang = useCallback((lang: LanguageCode) => {
    setState((s) => ({ ...s, toLang: lang, translatedText: null, translatedHtml: null }));
  }, []);

  const clearTranslation = useCallback(() => {
    setState((s) => ({ ...s, translatedText: null, translatedHtml: null, error: null }));
  }, []);

  const cleanup = useCallback(() => {
    // No-op: BYOK/local don't need browser-side cleanup
  }, []);

  return {
    ...state,
    translate,
    translateHtml,
    setFromLang,
    setToLang,
    clearTranslation,
    cleanup,
    SUPPORTED_LANGUAGES,
  };
}

export type { TranslationConfig, TranslationProvider };
