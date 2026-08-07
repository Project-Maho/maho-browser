// Copyright 2026 Maho Browser. All rights reserved.

// Single on-device translation backend (built-in Translator / LanguageDetector).
// No cloud, no API key, no fallback by design: an unsupported pair returns
// "unavailable" and callers surface that state instead of routing elsewhere.
// DOM-free and dependency-free so both the Mail bundle and the injected
// page-translation content script can import it.

export type TranslatorAvailability =
  | "unavailable"
  | "downloadable"
  | "downloading"
  | "available";

interface BrowserTranslatorInstance {
  translate(text: string): Promise<string>;
}

interface TranslatorFactory {
  availability(opts: { sourceLanguage: string; targetLanguage: string }): Promise<string>;
  create(opts: {
    sourceLanguage: string;
    targetLanguage: string;
    monitor?: (m: EventTarget) => void;
  }): Promise<BrowserTranslatorInstance>;
}

interface LanguageDetectorInstance {
  detect(text: string): Promise<Array<{ detectedLanguage: string; confidence: number }>>;
}

interface LanguageDetectorFactory {
  create(opts?: { monitor?: (m: EventTarget) => void }): Promise<LanguageDetectorInstance>;
}

function translatorApi(): TranslatorFactory | null {
  const t = (globalThis as unknown as { Translator?: TranslatorFactory }).Translator;
  return t && typeof t.create === "function" ? t : null;
}

function detectorApi(): LanguageDetectorFactory | null {
  const d = (globalThis as unknown as { LanguageDetector?: LanguageDetectorFactory }).LanguageDetector;
  return d && typeof d.create === "function" ? d : null;
}

export function isTranslatorAvailable(): boolean {
  return translatorApi() !== null;
}

export function normalizeLang(code?: string): string | undefined {
  if (!code) return undefined;
  return code.split("-")[0]?.toLowerCase();
}

export async function availability(
  sourceLang: string,
  targetLang: string,
): Promise<TranslatorAvailability> {
  const api = translatorApi();
  const source = normalizeLang(sourceLang);
  const target = normalizeLang(targetLang);
  if (!api || !source || !target) return "unavailable";
  try {
    const a = await api.availability({ sourceLanguage: source, targetLanguage: target });
    return (a as TranslatorAvailability) ?? "unavailable";
  } catch {
    return "unavailable";
  }
}

export type DownloadProgress = (fraction: number) => void;

const translatorCache = new Map<string, Promise<BrowserTranslatorInstance>>();

function getTranslator(
  sourceLanguage: string,
  targetLanguage: string,
  onProgress?: DownloadProgress,
): Promise<BrowserTranslatorInstance> {
  const key = `${sourceLanguage}->${targetLanguage}`;
  let inst = translatorCache.get(key);
  if (!inst) {
    const api = translatorApi();
    if (!api) return Promise.reject(new Error("Translator API unavailable"));
    inst = api.create({
      sourceLanguage,
      targetLanguage,
      monitor(m: EventTarget) {
        if (!onProgress) return;
        m.addEventListener("downloadprogress", (e: Event) => {
          const loaded = (e as unknown as { loaded?: number }).loaded;
          if (typeof loaded === "number") onProgress(loaded);
        });
      },
    });
    translatorCache.set(key, inst);
    void inst.catch(() => translatorCache.delete(key));
  }
  return inst;
}

let detectorInst: Promise<LanguageDetectorInstance> | null = null;
const DETECT_TIMEOUT_MS = 4000;

function withTimeout<T>(promise: Promise<T>, ms: number): Promise<T | null> {
  let timer: ReturnType<typeof setTimeout>;
  const timeout = new Promise<null>((resolve) => {
    timer = setTimeout(() => resolve(null), ms);
  });
  return Promise.race([promise.finally(() => clearTimeout(timer)), timeout]);
}

export async function detectLanguage(text: string): Promise<string | null> {
  const api = detectorApi();
  if (!api) return null;
  try {
    if (!detectorInst) detectorInst = api.create();
    const detector = await withTimeout(detectorInst, DETECT_TIMEOUT_MS);
    if (!detector) return null;
    const results = await withTimeout(detector.detect(text.slice(0, 2000)), DETECT_TIMEOUT_MS);
    const top = results && results[0];
    if (top && top.confidence >= 0.5 && top.detectedLanguage && top.detectedLanguage !== "und") {
      return normalizeLang(top.detectedLanguage) ?? null;
    }
  } catch {
    detectorInst = null;
  }
  return null;
}

async function resolveSource(sample: string, sourceLang?: string): Promise<string> {
  const detected = await detectLanguage(sample);
  return detected ?? normalizeLang(sourceLang) ?? "en";
}

export async function translate(
  text: string,
  targetLang: string,
  sourceLang?: string,
  onProgress?: DownloadProgress,
): Promise<string> {
  if (!text.trim()) return text;
  const target = normalizeLang(targetLang);
  if (!target) throw new Error("Missing target language");
  const source = await resolveSource(text, sourceLang);
  if (source === target) return text;
  const translator = await getTranslator(source, target, onProgress);
  return translator.translate(text);
}

export async function translateSegments(
  segments: string[],
  targetLang: string,
  sourceLang?: string,
  onProgress?: DownloadProgress,
): Promise<string[]> {
  const target = normalizeLang(targetLang);
  if (!target) throw new Error("Missing target language");
  const sample = segments.find((s) => s.trim()) ?? "";
  const source = await resolveSource(sample, sourceLang);
  if (source === target) return segments;
  const translator = await getTranslator(source, target, onProgress);
  const out: string[] = [];
  for (const seg of segments) {
    out.push(seg.trim() ? await translator.translate(seg) : seg);
  }
  return out;
}
