const MIN_DETECTION_LENGTH = 10;
const MAX_SAMPLE_LENGTH = 2000;
const NON_LATIN_THRESHOLD = 0.2;

type SupportedDetectionCode = "en" | "ko" | "ja" | "zh" | "ru" | "ar" | "hi";

const HANGUL_RANGES: ReadonlyArray<readonly [number, number]> = [
  [0xac00, 0xd7a3],
  [0x1100, 0x11ff],
  [0x3130, 0x318f],
];

const HIRAGANA_RANGES: ReadonlyArray<readonly [number, number]> = [[0x3040, 0x309f]];
const KATAKANA_RANGES: ReadonlyArray<readonly [number, number]> = [[0x30a0, 0x30ff]];
const CJK_RANGES: ReadonlyArray<readonly [number, number]> = [[0x4e00, 0x9fff]];
const CYRILLIC_RANGES: ReadonlyArray<readonly [number, number]> = [[0x0400, 0x04ff]];
const ARABIC_RANGES: ReadonlyArray<readonly [number, number]> = [
  [0x0600, 0x06ff],
  [0x0750, 0x077f],
  [0xfb50, 0xfdff],
  [0xfe70, 0xfeff],
];
const DEVANAGARI_RANGES: ReadonlyArray<readonly [number, number]> = [[0x0900, 0x097f]];

function isInRanges(codePoint: number, ranges: ReadonlyArray<readonly [number, number]>): boolean {
  return ranges.some(([start, end]) => codePoint >= start && codePoint <= end);
}

function isLetter(char: string): boolean {
  return /\p{Letter}/u.test(char);
}

function isLatinLetter(char: string): boolean {
  return /\p{Script=Latin}/u.test(char) && isLetter(char);
}

function filterSupported(code: SupportedDetectionCode, supportedCodes: readonly string[]): string | null {
  return supportedCodes.includes(code) ? code : null;
}

/**
 * Lightweight script-based language detection.
 * Returns an ISO 639-1 code that matches one of SUPPORTED_LANGUAGES, or null
 * when detection is too ambiguous (e.g. text < 10 chars or purely Latin
 * script with no diacritic signal).
 *
 * NOTE: this is NOT a high-accuracy detector. It distinguishes script
 * families well (CJK/Cyrillic/Arabic/Devanagari/Latin) but cannot reliably
 * separate Latin-script languages from each other (es vs fr vs de vs it
 * vs pt vs en) without a real language model. Returns "en" as a safe
 * fallback for unambiguous Latin script.
 */
export function detectLanguage(text: string, supportedCodes: readonly string[]): string | null {
  if (text.trim().length < MIN_DETECTION_LENGTH) {
    return null;
  }

  const sample = text.trim().slice(0, MAX_SAMPLE_LENGTH);

  const counts = {
    hangul: 0,
    hiragana: 0,
    katakana: 0,
    cjk: 0,
    cyrillic: 0,
    arabic: 0,
    devanagari: 0,
    latin: 0,
  };

  let totalLetterCodepoints = 0;

  for (const char of sample) {
    if (!isLetter(char)) {
      continue;
    }

    totalLetterCodepoints += 1;

    const codePoint = char.codePointAt(0);
    if (codePoint === undefined) {
      continue;
    }

    if (isInRanges(codePoint, HANGUL_RANGES)) {
      counts.hangul += 1;
      continue;
    }

    if (isInRanges(codePoint, HIRAGANA_RANGES)) {
      counts.hiragana += 1;
      continue;
    }

    if (isInRanges(codePoint, KATAKANA_RANGES)) {
      counts.katakana += 1;
      continue;
    }

    if (isInRanges(codePoint, CJK_RANGES)) {
      counts.cjk += 1;
      continue;
    }

    if (isInRanges(codePoint, CYRILLIC_RANGES)) {
      counts.cyrillic += 1;
      continue;
    }

    if (isInRanges(codePoint, ARABIC_RANGES)) {
      counts.arabic += 1;
      continue;
    }

    if (isInRanges(codePoint, DEVANAGARI_RANGES)) {
      counts.devanagari += 1;
      continue;
    }

    if (isLatinLetter(char)) {
      counts.latin += 1;
    }
  }

  if (totalLetterCodepoints === 0) {
    return null;
  }

  const kanaCount = counts.hiragana + counts.katakana;

  if (counts.hangul > 0) {
    return filterSupported("ko", supportedCodes);
  }

  if (kanaCount > 0 && counts.cjk > 0) {
    return filterSupported("ja", supportedCodes);
  }

  const nonLatinScores: Array<{ code: SupportedDetectionCode; count: number }> = [
    { code: "ja", count: kanaCount },
    { code: "zh", count: counts.cjk },
    { code: "ru", count: counts.cyrillic },
    { code: "ar", count: counts.arabic },
    { code: "hi", count: counts.devanagari },
  ];

  const dominantNonLatin = nonLatinScores.reduce((best, current) => (
    current.count > best.count ? current : best
  ));

  if (dominantNonLatin.count / totalLetterCodepoints >= NON_LATIN_THRESHOLD) {
    return filterSupported(dominantNonLatin.code, supportedCodes);
  }

  if (counts.latin > 0 && counts.latin / totalLetterCodepoints >= 0.5) {
    return filterSupported("en", supportedCodes);
  }

  return null;
}

export function isPotentialIdnHomograph(email: string): boolean {
  if (!email) return false;
  const parts = email.split("@");
  if (parts.length < 2) return false;
  return isHomographDomain(parts[1]);
}

/**
 * Detects whether a domain string exhibits IDN-homograph attack
 * characteristics. A domain is considered homograph-suspicious when it
 *
 *  1. is in Punycode form (starts with `xn--` or contains `.xn--`), OR
 *  2. mixes Latin script with one of the lookalike scripts
 *     (Cyrillic / Greek / Armenian / Cherokee).
 *
 * The function is permissive: it returns true for both legitimate IDN
 * domains and spoofed ones. Callers are expected to combine the result
 * with additional context (e.g. visible-text vs href comparison) to
 * decide whether a warning is warranted.
 */
export function isHomographDomain(domain: string): boolean {
  if (!domain) return false;
  const lower = domain.toLowerCase();

  if (lower.startsWith("xn--") || lower.includes(".xn--")) {
    return true;
  }

  const latinRegex = /\p{Script=Latin}/u;
  const cyrillicRegex = /\p{Script=Cyrillic}/u;
  const greekRegex = /\p{Script=Greek}/u;
  const armenianRegex = /\p{Script=Armenian}/u;
  const cherokeeRegex = /\p{Script=Cherokee}/u;

  let hasLatin = false;
  let hasLookalike = false;

  for (const char of lower) {
    if (latinRegex.test(char)) hasLatin = true;
    if (
      cyrillicRegex.test(char) ||
      greekRegex.test(char) ||
      armenianRegex.test(char) ||
      cherokeeRegex.test(char)
    ) {
      hasLookalike = true;
    }
  }

  return hasLatin && hasLookalike;
}
