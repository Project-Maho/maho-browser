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
  translatePage(target: string, nonce?: string): void;
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
  const NO_TRANSLATE = '[translate="no"], .notranslate';
  // Attributes whose values are user-visible text (tooltips, placeholders,
  // accessible names, image descriptions).
  const TRANSLATABLE_ATTRIBUTES = ["placeholder", "title", "aria-label", "alt"];
  const OBSERVE_OPTIONS: MutationObserverInit = {
    childList: true,
    subtree: true,
    characterData: true,
    attributes: true,
    attributeFilter: TRANSLATABLE_ATTRIBUTES,
  };
  // A translatable unit is a text node or one of the attributes above. Both
  // expose their text through nodeValue and keep their identity while the page
  // rewrites the value, so one set of bookkeeping covers both.
  type Unit = Text | Attr;
  // Pre-translation text of every unit this script rewrote.
  const originals = new WeakMap<Unit, string>();
  // The translation this script last wrote into each unit. A unit whose live
  // value differs was rewritten by the page since, so it is neither reverted on
  // restore nor mistaken for this script's own write.
  const written = new WeakMap<Unit, string>();
  // Open shadow roots found while collecting; observed alongside the body.
  // Weak so a detached root does not pin its host (and that host's subtree) for
  // the life of the document; entries are pruned when a removal disconnects the
  // host and cleared whenever the observer goes away.
  const shadowRoots = new Set<WeakRef<ShadowRoot>>();
  // Membership for |shadowRoots| without dereferencing every entry. Replaced
  // (not cleared: WeakSet has no clear) together with the registry so a later
  // translation re-registers and re-observes every root.
  let knownShadowRoots = new WeakSet<ShadowRoot>();
  let status: TranslateStatus = "idle";
  let generation = 0;
  let observer: MutationObserver | null = null;
  let target = "";
  let source: string | undefined;
  // Per-request token from the browser. The page shares the console with this
  // isolated world, so the browser only accepts state messages carrying it.
  let token = "";

  const normalize = (code?: string): string | undefined =>
    code ? code.split("-")[0]?.toLowerCase() : undefined;

  // Reports a state transition to the browser. The C++ injector observes these
  // console messages (WebContentsObserver::OnDidAddMessageToConsole) because
  // ExecuteJavaScriptInIsolatedWorld does not resolve promises, so this is the
  // only channel the browser has to learn when on-device translation actually
  // finished or failed (rather than optimistically assuming success). The
  // message is emitted at the verbose level, which the DevTools console hides
  // by default.
  const report = (next: TranslateStatus, source_lang?: string): void => {
    console.debug(
        "__mahoTranslate:state:" + token + ":" + next +
        (source_lang ? ":" + source_lang : ""));
  };

  function isNoTranslate(el: Element): boolean {
    return el.closest(NO_TRANSLATE) !== null;
  }

  function translatable(node: Text): boolean {
    if (!node.nodeValue || !node.nodeValue.trim()) return false;
    const p = node.parentElement;
    if (!p || SKIP_TAGS.has(p.tagName) || p.isContentEditable) return false;
    if (isNoTranslate(p)) return false;
    return true;
  }

  function translatableAttr(attr: Attr): boolean {
    if (!attr.value.trim()) return false;
    const owner = attr.ownerElement;
    return !!owner && !isNoTranslate(owner);
  }

  function observeRoot(root: Node): void {
    observer?.observe(root, OBSERVE_OPTIONS);
  }

  // Drops every registered root whose host left the document (a component the
  // page unmounted). The page keeps rendering new components into a translated
  // tab, so without this the registry grows monotonically. MutationObserver has
  // no per-node unobserve; the registration dies with the detached subtree
  // (Blink keeps observed nodes out of the observer's strong graph), so dropping
  // the entry is enough to make the root collectable.
  function pruneShadowRoots(): void {
    for (const ref of shadowRoots) {
      const root = ref.deref();
      if (root && root.host.isConnected) continue;
      shadowRoots.delete(ref);
      // Forget the membership too, so a host the page re-attaches (portals and
      // virtualized lists detach and re-add nodes) is registered again.
      if (root) knownShadowRoots.delete(root);
    }
  }

  // Forgets the registry; the caller has already stopped the observer, so no
  // observation needs to be undone.
  function clearShadowRoots(): void {
    shadowRoots.clear();
    knownShadowRoots = new WeakSet<ShadowRoot>();
  }

  function visitElement(el: Element, out: Unit[]): void {
    for (const name of TRANSLATABLE_ATTRIBUTES) {
      const attr = el.getAttributeNode(name);
      if (attr && translatableAttr(attr)) out.push(attr);
    }
    const shadow = el.shadowRoot;
    if (shadow) {
      if (!knownShadowRoots.has(shadow)) {
        knownShadowRoots.add(shadow);
        shadowRoots.add(new WeakRef(shadow));
        observeRoot(shadow);
      }
      collect(shadow, out);
    }
  }

  // Collects text nodes and translatable attributes under |root|, descending
  // into open shadow roots.
  function collect(root: Node, out: Unit[] = []): Unit[] {
    if (root.nodeType === Node.ELEMENT_NODE) {
      if (isNoTranslate(root as Element)) return out;
      visitElement(root as Element, out);
    }
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT, {
      acceptNode: (n) => {
        if (n.nodeType === Node.TEXT_NODE) {
          return translatable(n as Text) ? NodeFilter.FILTER_ACCEPT : NodeFilter.FILTER_SKIP;
        }
        return (n as Element).matches(NO_TRANSLATE) ? NodeFilter.FILTER_REJECT : NodeFilter.FILTER_ACCEPT;
      },
    });
    let cur: Node | null;
    while ((cur = walker.nextNode())) {
      if (cur.nodeType === Node.TEXT_NODE) out.push(cur as Text);
      else visitElement(cur as Element, out);
    }
    return out;
  }

  // Reverts every unit this script translated, except ones the page rewrote
  // after the translation landed (their live text is the page's, not ours).
  function restoreUnits(): void {
    for (const u of collect(document.body)) {
      const orig = originals.get(u);
      if (orig !== undefined && u.nodeValue === written.get(u)) u.nodeValue = orig;
      originals.delete(u);
      written.delete(u);
    }
  }

  // True when the unit's current value came from the page rather than from
  // this script's own write; the page's new text becomes the unit's original.
  function pageRewrote(u: Unit): boolean {
    const mine = written.get(u);
    if (mine !== undefined && u.nodeValue === mine) return false;
    originals.delete(u);
    written.delete(u);
    return true;
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

  // Translates |units| and returns how many of them visibly changed.
  async function translateNodes(units: Unit[], request: number): Promise<number> {
    if (request !== generation) return 0;
    const pending = units.filter((u) => u.nodeValue && u.nodeValue.trim());
    if (!pending.length || source === target) return 0;
    if (!w.Translator || !source) throw new Error('Translation is unavailable');
    const translator = await w.Translator.create({ sourceLanguage: source, targetLanguage: target });
    if (request !== generation) return 0;
    // Translate all units first, buffering results, then apply them in one
    // synchronous pass so the page swaps at once instead of revealing
    // top-to-bottom.
    const translations = new Map<Unit, [string, string]>();
    for (const u of pending) {
      const value = u.nodeValue as string;
      translations.set(u, [value, await translator.translate(value)]);
      if (request !== generation) return 0;
    }
    let changed = 0;
    for (const [u, [value, translated]] of translations) {
      // The page rewrote this unit while it was being translated; the observer
      // picks up the page's new text instead.
      if (u.nodeValue !== value) continue;
      if (!originals.has(u)) originals.set(u, value);
      written.set(u, translated);
      u.nodeValue = translated;
      if (translated !== value) changed += 1;
    }
    return changed;
  }

  function stopObserver(): void {
    observer?.disconnect();
    observer = null;
  }

  function startObserver(request: number): void {
    if (observer || request !== generation) return;
    observer = new MutationObserver((muts) => {
      if (status !== "translated") return;
      const fresh = new Set<Unit>();
      for (const m of muts) {
        if (m.type === "childList") {
          if (m.removedNodes.length) pruneShadowRoots();
          m.addedNodes.forEach((n) => {
            if (n.nodeType === Node.TEXT_NODE) {
              if (translatable(n as Text)) fresh.add(n as Text);
            } else if (n.nodeType === Node.ELEMENT_NODE) {
              for (const u of collect(n)) fresh.add(u);
            }
          });
        } else if (m.type === "characterData") {
          const n = m.target as Text;
          if (pageRewrote(n) && translatable(n)) fresh.add(n);
        } else if (m.type === "attributes" && m.attributeName) {
          const attr = (m.target as Element).getAttributeNode(m.attributeName);
          if (attr && pageRewrote(attr) && translatableAttr(attr)) fresh.add(attr);
        }
      }
      if (!fresh.size) return;
      const batch = [...fresh];
      // One retry absorbs a transient model failure; a second failure is
      // reported so the browser stops claiming the page is fully translated.
      void translateNodes(batch, request)
        .catch(() => translateNodes(batch, request))
        .catch(() => {
          if (request !== generation) return;
          status = "error";
          report("error");
          stopObserver();
        });
    });
    observeRoot(document.body);
    for (const ref of shadowRoots) {
      const root = ref.deref();
      if (root) observeRoot(root);
    }
  }

  w.__mahoTranslate = {
    translatePage(t: string, nonce = ""): void {
      const request = ++generation;
      stopObserver();
      token = nonce;
      // Restore first so a repeated or re-targeted request translates the
      // original text rather than the previous translation.
      restoreUnits();
      clearShadowRoots();
      status = "translating";
      target = normalize(t) ?? "en";
      report("translating");
      const units = collect(document.body);
      // Prefer the declared <html lang>: it is instant, so Translator.create()
      // runs inside the fresh transient-activation window. Falling back to the
      // detector first would await its own model download and let activation
      // expire, making create() throw on first use.
      const hinted = normalize(document.documentElement.lang);
      const resolveSource = hinted
        ? Promise.resolve(hinted)
        : detect(units.map((u) => u.nodeValue).join(" ").slice(0, 2000));
      void resolveSource
        .then((s) => {
          if (request !== generation) return 0;
          source = s;
          return translateNodes(units, request);
        })
        .then((changed) => {
          if (request !== generation) return;
          if (!changed) {
            // Already in the target language, or nothing translatable: the
            // page is unchanged, so do not claim it was translated.
            status = "idle";
            report("idle", source);
            return;
          }
          status = "translated";
          report("translated", source);
          startObserver(request);
        })
        .catch(() => {
          if (request === generation) {
            status = "error";
            report("error");
          }
        });
    },
    restorePage(): void {
      generation += 1;
      stopObserver();
      clearShadowRoots();
      status = "idle";
      report("idle");
      restoreUnits();
    },
    status(): TranslateStatus {
      return status;
    },
  };
})();

export {};
