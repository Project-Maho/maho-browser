import { describe, expect, it } from "vitest";

import { detectLanguage, isPotentialIdnHomograph } from "../languageDetect";

const DEFAULT_SUPPORTED = ["en", "ko", "ja", "zh", "es", "fr", "de", "pt", "ru", "it", "ar", "hi"] as const;

describe("detectLanguage", () => {
  it("detects Korean text", () => {
    expect(detectLanguage("안녕하세요. 이번 주 회의 일정 확인 부탁드립니다.", DEFAULT_SUPPORTED)).toBe("ko");
  });

  it("detects Korean when Hangul is mixed with Latin text", () => {
    expect(detectLanguage("Maho 회의 일정은 tomorrow morning에 다시 공유드릴게요.", DEFAULT_SUPPORTED)).toBe("ko");
  });

  it("detects Japanese mixed kana and kanji", () => {
    expect(detectLanguage("こんにちは、来週の会議資料を共有しますのでご確認ください。", DEFAULT_SUPPORTED)).toBe("ja");
  });

  it("detects Japanese kana-heavy text", () => {
    expect(detectLanguage("これはテストメールですので、あとで返信してください。", DEFAULT_SUPPORTED)).toBe("ja");
  });

  it("detects Chinese when only CJK ideographs are present", () => {
    expect(detectLanguage("您好这封邮件是关于下周会议安排的通知请及时查看。", DEFAULT_SUPPORTED)).toBe("zh");
  });

  it("detects Russian text", () => {
    expect(detectLanguage("Здравствуйте, пожалуйста подтвердите получение письма сегодня.", DEFAULT_SUPPORTED)).toBe("ru");
  });

  it("detects Arabic text", () => {
    expect(detectLanguage("مرحبًا، يرجى مراجعة تفاصيل الاجتماع وإرسال ملاحظاتك اليوم.", DEFAULT_SUPPORTED)).toBe("ar");
  });

  it("detects Hindi text", () => {
    expect(detectLanguage("नमस्ते, कृपया इस ईमेल के संलग्न दस्तावेज़ की समीक्षा करें।", DEFAULT_SUPPORTED)).toBe("hi");
  });

  it("returns en for plain English text", () => {
    expect(detectLanguage("Hello team, please review the attached proposal before Friday.", DEFAULT_SUPPORTED)).toBe("en");
  });

  it("returns en for mixed Latin-script languages", () => {
    expect(detectLanguage("Bonjour team, please review the attached proposal before Friday.", DEFAULT_SUPPORTED)).toBe("en");
  });

  it("returns null for short text", () => {
    expect(detectLanguage("안녕", DEFAULT_SUPPORTED)).toBeNull();
  });

  it("returns null when the detected language is excluded by supported codes", () => {
    expect(detectLanguage("안녕하세요. 이번 주 회의 일정 확인 부탁드립니다.", ["en", "ja", "zh"])).toBeNull();
  });
});

describe("isPotentialIdnHomograph", () => {
  it("returns false for standard Latin domains", () => {
    expect(isPotentialIdnHomograph("test@gmail.com")).toBe(false);
    expect(isPotentialIdnHomograph("user@apple.com")).toBe(false);
  });

  it("returns true for domains starting with xn-- (Punycode)", () => {
    expect(isPotentialIdnHomograph("user@xn--apple-43d.com")).toBe(true);
  });

  it("returns true for mixed Latin and Cyrillic/Greek domains", () => {
    // Contains Cyrillic 'а' (U+0430) instead of Latin 'a'
    expect(isPotentialIdnHomograph("user@аpple.com")).toBe(true); 
  });
});
