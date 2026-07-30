// Copyright 2026 Maho Browser. All rights reserved.

// On-device translation via the browser's built-in Translator / LanguageDetector
// APIs (Chrome built-in AI). No API key, no cloud round-trip: the browser manages
// and downloads the translation models locally. Used as the primary, free path for
// mail translation, with the BYOK/cloud backend as fallback (see api/index.ts).

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

export function isBrowserTranslatorAvailable(): boolean {
  return translatorApi() !== null;
}

function normalizeLang(code?: string): string | undefined {
  if (!code) return undefined;
  return code.split("-")[0].toLowerCase();
}

const translatorCache = new Map<string, Promise<BrowserTranslatorInstance>>();

function getTranslator(sourceLanguage: string, targetLanguage: string): Promise<BrowserTranslatorInstance> {
  const key = `${sourceLanguage}->${targetLanguage}`;
  let inst = translatorCache.get(key);
  if (!inst) {
    const api = translatorApi();
    if (!api) return Promise.reject(new Error("Translator API unavailable"));
    inst = api.create({ sourceLanguage, targetLanguage });
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

async function detectLang(text: string): Promise<string | null> {
  const api = detectorApi();
  if (!api) return null;
  try {
    if (!detectorInst) detectorInst = api.create();
    // Time-box detection: the detector model may need a first-run download, and we
    // must never let language detection stall the actual translation. On timeout we
    // fall back to the caller-provided source language hint.
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
  const detected = await detectLang(sample);
  return detected ?? normalizeLang(sourceLang) ?? "en";
}

export async function browserTranslate(text: string, targetLang: string, sourceLang?: string): Promise<string> {
  if (!text.trim()) return text;
  const target = normalizeLang(targetLang);
  if (!target) throw new Error("Missing target language");
  const source = await resolveSource(text, sourceLang);
  if (source === target) return text;
  const translator = await getTranslator(source, target);
  return translator.translate(text);
}

export async function browserTranslateSegments(
  segments: string[],
  targetLang: string,
  sourceLang?: string,
): Promise<string[]> {
  const target = normalizeLang(targetLang);
  if (!target) throw new Error("Missing target language");
  const sample = segments.find((s) => s.trim()) ?? "";
  const source = await resolveSource(sample, sourceLang);
  if (source === target) return segments;
  const translator = await getTranslator(source, target);
  const out: string[] = [];
  for (const seg of segments) {
    out.push(seg.trim() ? await translator.translate(seg) : seg);
  }
  return out;
}
