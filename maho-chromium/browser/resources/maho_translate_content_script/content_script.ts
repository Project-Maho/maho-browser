// Copyright 2026 Maho Browser. All rights reserved.

// Standalone page-translation content script (injected into user pages, like
// the Boost content script — no imports, no Mojo). Uses the browser's built-in
// on-device Translator / LanguageDetector APIs directly (isolated-world exposed,
// no API key, no cloud). Exposes window.__mahoTranslate for the C++ injector.
// The shared translator_core.ts (Mail) mirrors this logic; injection constraints
// require this file to stay import-free, so the small overlap is intentional.

interface BrowserTranslatorInstance {
  translate(text: string): Promise<string>;
}
interface TranslatorFactory {
  create(o: { sourceLanguage: string; targetLanguage: string }): Promise<BrowserTranslatorInstance>;
}
interface LanguageDetectorInstance {
  detect(text: string): Promise<Array<{ detectedLanguage: string; confidence: number }>>;
}
interface LanguageDetectorFactory {
  create(): Promise<LanguageDetectorInstance>;
}
type TranslateStatus = "idle" | "translating" | "translated" | "error";
interface MahoTranslateNamespace {
  translatePage(target: string): void;
  restorePage(): void;
  status(): TranslateStatus;
}

(function () {
  "use strict";
  const w = window as unknown as {
    __mahoTranslate?: MahoTranslateNamespace;
    Translator?: TranslatorFactory;
    LanguageDetector?: LanguageDetectorFactory;
  };
  if (w.__mahoTranslate) {
    return;
  }

  const SKIP_TAGS = new Set(["SCRIPT", "STYLE", "NOSCRIPT", "TEXTAREA", "CODE", "PRE", "KBD", "SAMP"]);
  const originals = new WeakMap<Text, string>();
  let status: TranslateStatus = "idle";
  let generation = 0;
  let observer: MutationObserver | null = null;
  let target = "";
  let source: string | undefined;

  const normalize = (code?: string): string | undefined =>
    code ? code.split("-")[0]?.toLowerCase() : undefined;

  function translatable(node: Text): boolean {
    if (!node.nodeValue || !node.nodeValue.trim()) return false;
    const p = node.parentElement;
    if (!p || SKIP_TAGS.has(p.tagName) || p.isContentEditable) return false;
    if (p.closest('[translate="no"], .notranslate')) return false;
    return true;
  }

  function collect(root: Node): Text[] {
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, {
      acceptNode: (n) => (translatable(n as Text) ? NodeFilter.FILTER_ACCEPT : NodeFilter.FILTER_REJECT),
    });
    const out: Text[] = [];
    let cur: Node | null;
    while ((cur = walker.nextNode())) out.push(cur as Text);
    return out;
  }

  async function detect(sample: string): Promise<string | undefined> {
    if (!w.LanguageDetector) return undefined;
    try {
      const d = await w.LanguageDetector.create();
      const r = await d.detect(sample.slice(0, 2000));
      const top = r && r[0];
      if (top && top.confidence >= 0.5 && top.detectedLanguage && top.detectedLanguage !== "und") {
        return normalize(top.detectedLanguage);
      }
    } catch {
      return undefined;
    }
    return undefined;
  }

  async function translateNodes(nodes: Text[], request: number): Promise<void> {
    if (request !== generation) return;
    if (!w.Translator || !source) throw new Error('Translation is unavailable');
    if (source === target) return;
    const pending = nodes.filter((n) => n.nodeValue && n.nodeValue.trim());
    if (!pending.length) return;
    const translator = await w.Translator.create({ sourceLanguage: source, targetLanguage: target });
    if (request !== generation) return;
    // Translate all nodes first, buffering results, then apply them in one
    // synchronous pass so the page swaps at once instead of revealing
    // top-to-bottom.
    const translations = new Map<Text, string>();
    for (const n of pending) {
      const value = n.nodeValue as string;
      translations.set(n, await translator.translate(value));
      if (request !== generation) return;
    }
    for (const [n, translated] of translations) {
      if (!originals.has(n)) originals.set(n, n.nodeValue as string);
      n.nodeValue = translated;
    }
  }

  function startObserver(request: number): void {
    if (observer || request !== generation) return;
    observer = new MutationObserver((muts) => {
      if (status !== "translated") return;
      const fresh: Text[] = [];
      for (const m of muts) {
        m.addedNodes.forEach((n) => {
          if (n.nodeType === Node.TEXT_NODE && translatable(n as Text)) fresh.push(n as Text);
          else if (n.nodeType === Node.ELEMENT_NODE) fresh.push(...collect(n));
        });
      }
      if (fresh.length) void translateNodes(fresh, request).catch(() => {
        if (request !== generation) return;
        status = "error";
        observer?.disconnect();
        observer = null;
      });
    });
    observer.observe(document.body, { childList: true, subtree: true });
  }

  w.__mahoTranslate = {
    translatePage(t: string): void {
      const request = ++generation;
      observer?.disconnect();
      observer = null;
      status = "translating";
      target = normalize(t) ?? "en";
      const nodes = collect(document.body);
      // Prefer the declared <html lang>: it is instant, so Translator.create()
      // runs inside the fresh transient-activation window. Falling back to the
      // detector first would await its own model download and let activation
      // expire, making create() throw on first use.
      const hinted = normalize(document.documentElement.lang);
      const resolveSource = hinted
        ? Promise.resolve(hinted)
        : detect(nodes.map((n) => n.nodeValue).join(" ").slice(0, 2000));
      void resolveSource
        .then((s) => {
          if (request !== generation) return;
          source = s;
          return translateNodes(nodes, request);
        })
        .then(() => {
          if (request !== generation) return;
          status = "translated";
          startObserver(request);
        })
        .catch(() => {
          if (request === generation) status = "error";
        });
    },
    restorePage(): void {
      generation += 1;
      observer?.disconnect();
      observer = null;
      status = "idle";
      for (const n of collect(document.body)) {
        const orig = originals.get(n);
        if (orig !== undefined) n.nodeValue = orig;
      }
    },
    status(): TranslateStatus {
      return status;
    },
  };
})();

export {};
