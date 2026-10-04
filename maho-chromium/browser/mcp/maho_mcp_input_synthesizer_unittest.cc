// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"

#include <cassert>
#include <iostream>
#include <vector>

#ifndef MAHO_STANDALONE_TEST
#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace maho {

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_NamedKeys) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Enter"),
            ui::VKEY_RETURN);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Tab"), ui::VKEY_TAB);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Escape"),
            ui::VKEY_ESCAPE);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Backspace"),
            ui::VKEY_BACK);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Delete"),
            ui::VKEY_DELETE);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_ArrowKeys) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowUp"),
            ui::VKEY_UP);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowDown"),
            ui::VKEY_DOWN);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowLeft"),
            ui::VKEY_LEFT);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowRight"),
            ui::VKEY_RIGHT);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_NavigationKeys) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Home"),
            ui::VKEY_HOME);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("End"), ui::VKEY_END);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("PageUp"),
            ui::VKEY_PRIOR);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("PageDown"),
            ui::VKEY_NEXT);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_Space) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode(" "),
            ui::VKEY_SPACE);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Space"),
            ui::VKEY_SPACE);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_FunctionKeys) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("F1"), ui::VKEY_F1);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("F5"), ui::VKEY_F5);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("F12"), ui::VKEY_F12);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_Alphanumeric) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("a"), ui::VKEY_A);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("Z"), ui::VKEY_Z);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("0"), ui::VKEY_0);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("9"), ui::VKEY_9);
}

TEST(MahoMcpInputSynthesizerTest, KeyStringToKeyCode_Unknown) {
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode(""),
            ui::VKEY_UNKNOWN);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("MadeUpKey"),
            ui::VKEY_UNKNOWN);
  EXPECT_EQ(MahoMcpInputSynthesizer::KeyStringToKeyCode("!"),
            ui::VKEY_UNKNOWN);
}

TEST(MahoMcpInputSynthesizerTest, MacSelectAllCommandTranslation) {
  const int meta = MahoMcpInputSynthesizer::ModifierListToWebModifiers({"meta"});
  EXPECT_EQ(MahoMcpInputSynthesizer::MacEditCommandForKeyPress(
                MahoMcpInputSynthesizer::KeyStringToKeyCode("a"), meta),
            "SelectAll");
  EXPECT_EQ(MahoMcpInputSynthesizer::MacEditCommandForKeyPress(
                MahoMcpInputSynthesizer::KeyStringToKeyCode("A"), meta),
            "SelectAll");
  for (int modifiers : {0, meta | blink::WebInputEvent::kShiftKey,
                       meta | blink::WebInputEvent::kControlKey,
                       meta | blink::WebInputEvent::kAltKey}) {
    EXPECT_TRUE(MahoMcpInputSynthesizer::MacEditCommandForKeyPress(
                    ui::VKEY_A, modifiers).empty());
  }
  for (auto key : {ui::VKEY_BACK, ui::VKEY_DELETE, ui::VKEY_Z}) {
    EXPECT_TRUE(MahoMcpInputSynthesizer::MacEditCommandForKeyPress(
                    key, meta).empty());
  }
}

TEST(MahoMcpInputSynthesizerTest, ModifierList_None) {
  EXPECT_EQ(MahoMcpInputSynthesizer::ModifierListToWebModifiers({}),
            static_cast<int>(blink::WebInputEvent::kNoModifiers));
}

TEST(MahoMcpInputSynthesizerTest, ModifierList_Shift) {
  int result = MahoMcpInputSynthesizer::ModifierListToWebModifiers({"shift"});
  EXPECT_TRUE(result & blink::WebInputEvent::kShiftKey);
  EXPECT_FALSE(result & blink::WebInputEvent::kControlKey);
}

TEST(MahoMcpInputSynthesizerTest, ModifierList_MultipleModifiers) {
  int result = MahoMcpInputSynthesizer::ModifierListToWebModifiers(
      {"shift", "ctrl", "alt", "meta"});
  EXPECT_TRUE(result & blink::WebInputEvent::kShiftKey);
  EXPECT_TRUE(result & blink::WebInputEvent::kControlKey);
  EXPECT_TRUE(result & blink::WebInputEvent::kAltKey);
  EXPECT_TRUE(result & blink::WebInputEvent::kMetaKey);
}

TEST(MahoMcpInputSynthesizerTest, ModifierList_UnknownIgnored) {
  int result = MahoMcpInputSynthesizer::ModifierListToWebModifiers(
      {"shift", "hyper", "unknown"});
  EXPECT_TRUE(result & blink::WebInputEvent::kShiftKey);
  EXPECT_EQ(result,
            static_cast<int>(blink::WebInputEvent::kShiftKey));
}

TEST(MahoMcpInputSynthesizerTest, NullWebContentsNeverDispatchesOrMarks) {
  EXPECT_FALSE(MahoMcpInputSynthesizer::HoverAt(nullptr, 10, 20));
  EXPECT_FALSE(MahoMcpInputSynthesizer::ClickAt(
      nullptr, 10, 20, {}, /*sensitive=*/false));
}

TEST(MahoMcpInputSynthesizerTest,
     AsyncTypingNullContentsCompletesOnceWithFailure) {
  base::test::TaskEnvironment task_environment;
  int completion_count = 0;
  bool completion_result = true;

  MahoMcpInputSynthesizer::TypeTextAsync(
      nullptr, "x", {.pacing_policy = TypingPacingPolicy::Instant()},
      base::BindOnce(
          [](int* completion_count, bool* completion_result, bool success) {
            ++*completion_count;
            *completion_result = success;
          },
          &completion_count, &completion_result));

  EXPECT_EQ(completion_count, 0);
  task_environment.RunUntilIdle();
  EXPECT_EQ(completion_count, 1);
  EXPECT_FALSE(completion_result);
  task_environment.RunUntilIdle();
  EXPECT_EQ(completion_count, 1);
}

// -----------------------------------------------------------------------------
// Gap #5: Bounded Natural Typing Policy Tests
// -----------------------------------------------------------------------------

TEST(MahoMcpInputSynthesizerTest, SeededRngProducesDeterministicDelaySequence) {
  const uint64_t kTestSeed = 0x123456789ABCDEF0ULL;
  TypingPacingPolicy policy;
  policy.base_range_ms = {10, 30};
  policy.punctuation_range_ms = {120, 240};
  policy.space_range_ms = {40, 80};
  policy.rng_seed = kTestSeed;

  const std::string text = "Hello, World! How are you doing today? 123.";

  auto generate_delays = [&](const TypingPacingPolicy& pol) {
    std::vector<int32_t> delays;
    SplitMix64Prng rng = pol.CreatePrng();
    for (size_t i = 0; i < text.size(); ++i) {
      char prev_c = (i == 0) ? '\0' : text[i - 1];
      char next_c = text[i];
      delays.push_back(pol.delay_for(prev_c, next_c, rng));
    }
    return delays;
  };

  std::vector<int32_t> run1 = generate_delays(policy);
  std::vector<int32_t> run2 = generate_delays(policy);

  ASSERT_EQ(run1.size(), text.size());
  ASSERT_EQ(run2.size(), text.size());
  EXPECT_EQ(run1, run2);  // Strict deterministic reproducibility

  // Verify that different seed yields different sequence
  TypingPacingPolicy other_policy = policy;
  other_policy.rng_seed = 0x987654321FEDCBA0ULL;
  std::vector<int32_t> run_diff = generate_delays(other_policy);
  EXPECT_NE(run1, run_diff);
}

TEST(MahoMcpInputSynthesizerTest, AllDelaysWithinDeclaredBounds) {
  TypingPacingPolicy policy;
  policy.base_range_ms = {0, 60};
  policy.punctuation_range_ms = {120, 240};
  policy.space_range_ms = {40, 80};
  policy.rng_seed = 42ULL;

  SplitMix64Prng rng = policy.CreatePrng();

  // Test standard characters: base range [0, 60]
  for (int i = 0; i < 200; ++i) {
    int32_t d = policy.delay_for('a', 'b', rng);
    EXPECT_GE(d, 0);
    EXPECT_LE(d, 60);
  }

  // Test punctuation pauses: punctuation range [120, 240]
  const std::vector<char> punctuations = {'.', ',', '!', '?', ';', ':', '-', '\n'};
  for (char p : punctuations) {
    for (int i = 0; i < 50; ++i) {
      int32_t d = policy.delay_for(p, 'a', rng);
      EXPECT_GE(d, 120);
      EXPECT_LE(d, 240);
    }
  }

  // Test space pacing: space range [40, 80]
  for (int i = 0; i < 200; ++i) {
    int32_t d1 = policy.delay_for('a', ' ', rng);
    EXPECT_GE(d1, 40);
    EXPECT_LE(d1, 80);

    int32_t d2 = policy.delay_for(' ', 'w', rng);
    EXPECT_GE(d2, 40);
    EXPECT_LE(d2, 80);
  }
}

TEST(MahoMcpInputSynthesizerTest, OutOfBoundsConfigRejectedByValidation) {
  // Default is valid
  TypingPacingPolicy valid_default;
  EXPECT_TRUE(valid_default.validate());

  // Inverted range (min > max) rejected
  TypingPacingPolicy inverted = valid_default;
  inverted.base_range_ms = {70, 60};
  EXPECT_FALSE(inverted.validate());

  inverted = valid_default;
  inverted.punctuation_range_ms = {250, 240};
  EXPECT_FALSE(inverted.validate());

  inverted = valid_default;
  inverted.space_range_ms = {90, 80};
  EXPECT_FALSE(inverted.validate());

  // Negative delay rejected
  TypingPacingPolicy negative = valid_default;
  negative.base_range_ms = {-1, 60};
  EXPECT_FALSE(negative.validate());

  negative = valid_default;
  negative.punctuation_range_ms = {120, -10};
  EXPECT_FALSE(negative.validate());

  negative = valid_default;
  negative.space_range_ms = {-50, -10};
  EXPECT_FALSE(negative.validate());

  // Delay > cap (> 5000ms) rejected
  TypingPacingPolicy over_cap = valid_default;
  over_cap.base_range_ms = {0, 5001};
  EXPECT_FALSE(over_cap.validate());

  over_cap = valid_default;
  over_cap.punctuation_range_ms = {100, 6000};
  EXPECT_FALSE(over_cap.validate());

  over_cap = valid_default;
  over_cap.space_range_ms = {0, 10000};
  EXPECT_FALSE(over_cap.validate());
}

TEST(MahoMcpInputSynthesizerTest, Utf8ToTypingUnits_Korean) {
  const std::string text = "윤인도";
  std::vector<TypingKeyUnit> units =
      MahoMcpInputSynthesizer::Utf8ToTypingUnits(text);
  ASSERT_EQ(units.size(), 3u);

  EXPECT_EQ(units[0].code_point, 0xC724u);  // '윤'
  EXPECT_EQ(units[0].utf8, "윤");
  EXPECT_EQ(units[0].utf16, u"윤");

  EXPECT_EQ(units[1].code_point, 0xC778u);  // '인'
  EXPECT_EQ(units[1].utf8, "인");
  EXPECT_EQ(units[1].utf16, u"인");

  EXPECT_EQ(units[2].code_point, 0xB3C4u);  // '도'
  EXPECT_EQ(units[2].utf8, "도");
  EXPECT_EQ(units[2].utf16, u"도");
}

TEST(MahoMcpInputSynthesizerTest, Utf8ToTypingUnits_Mixed) {
  const std::string text = "Maho 마호 123";
  std::vector<TypingKeyUnit> units =
      MahoMcpInputSynthesizer::Utf8ToTypingUnits(text);
  ASSERT_EQ(units.size(), 11u);

  EXPECT_EQ(units[0].code_point, static_cast<char32_t>('M'));
  EXPECT_EQ(units[0].utf8, "M");
  EXPECT_EQ(units[1].code_point, static_cast<char32_t>('a'));
  EXPECT_EQ(units[2].code_point, static_cast<char32_t>('h'));
  EXPECT_EQ(units[3].code_point, static_cast<char32_t>('o'));
  EXPECT_EQ(units[4].code_point, static_cast<char32_t>(' '));
  EXPECT_EQ(units[5].code_point, 0xB9C8u);  // '마'
  EXPECT_EQ(units[5].utf8, "마");
  EXPECT_EQ(units[6].code_point, 0xD638u);  // '호'
  EXPECT_EQ(units[6].utf8, "호");
  EXPECT_EQ(units[7].code_point, static_cast<char32_t>(' '));
  EXPECT_EQ(units[8].code_point, static_cast<char32_t>('1'));
  EXPECT_EQ(units[9].code_point, static_cast<char32_t>('2'));
  EXPECT_EQ(units[10].code_point, static_cast<char32_t>('3'));
}

TEST(MahoMcpInputSynthesizerTest, Utf8ToTypingUnits_EmojiSurrogatePair) {
  const std::string text = "👋🚀";
  std::vector<TypingKeyUnit> units =
      MahoMcpInputSynthesizer::Utf8ToTypingUnits(text);
  ASSERT_EQ(units.size(), 2u);

  EXPECT_EQ(units[0].code_point, 0x1F44Bu);  // '👋'
  EXPECT_EQ(units[0].utf16.size(), 2u);
  EXPECT_EQ(units[0].utf16[0], 0xD83Du);
  EXPECT_EQ(units[0].utf16[1], 0xDC4Bu);
  EXPECT_EQ(units[0].utf8, "👋");

  EXPECT_EQ(units[1].code_point, 0x1F680u);  // '🚀'
  EXPECT_EQ(units[1].utf16.size(), 2u);
  EXPECT_EQ(units[1].utf16[0], 0xD83Du);
  EXPECT_EQ(units[1].utf16[1], 0xDE80u);
  EXPECT_EQ(units[1].utf8, "🚀");
}

TEST(MahoMcpInputSynthesizerTest, Utf8ToTypingUnits_CrlfNormalization) {
  std::vector<TypingKeyUnit> units =
      MahoMcpInputSynthesizer::Utf8ToTypingUnits("A\r\nB\nC\rD");
  ASSERT_EQ(units.size(), 7u);
  EXPECT_EQ(units[0].code_point, static_cast<char32_t>('A'));
  EXPECT_EQ(units[1].code_point, static_cast<char32_t>('\n'));
  EXPECT_EQ(units[2].code_point, static_cast<char32_t>('B'));
  EXPECT_EQ(units[3].code_point, static_cast<char32_t>('\n'));
  EXPECT_EQ(units[4].code_point, static_cast<char32_t>('C'));
  EXPECT_EQ(units[5].code_point, static_cast<char32_t>('\n'));
  EXPECT_EQ(units[6].code_point, static_cast<char32_t>('D'));
}

TEST(MahoMcpInputSynthesizerTest, BuildNativeEventsForUnit_Newline) {
  TypingKeyUnit newline_unit;
  newline_unit.code_point = static_cast<char32_t>('\n');
  newline_unit.utf8 = "\n";
  newline_unit.utf16 = u"\n";

  std::vector<NativeEvent> events =
      MahoMcpInputSynthesizer::BuildNativeEventsForUnit(newline_unit);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].type, NativeEvent::Type::kKeyDown);
  EXPECT_EQ(events[0].key_code, ui::VKEY_RETURN);
  EXPECT_EQ(events[1].type, NativeEvent::Type::kKeyUp);
  EXPECT_EQ(events[1].key_code, ui::VKEY_RETURN);
}

TEST(MahoMcpInputSynthesizerTest, BuildNativeEventsForUnit_UnicodeText) {
  const std::vector<TypingKeyUnit> units =
      MahoMcpInputSynthesizer::Utf8ToTypingUnits("A한🚀");
  ASSERT_EQ(units.size(), 3u);
  for (const auto& unit : units) {
    const std::vector<NativeEvent> events =
        MahoMcpInputSynthesizer::BuildNativeEventsForUnit(unit);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, NativeEvent::Type::kKeyChar);
    EXPECT_EQ(events[0].code_point, unit.code_point);
    EXPECT_EQ(events[0].text, unit.utf16);
  }
}

TEST(MahoMcpInputSynthesizerTest, NormalizeCrlfBoundaries) {
  EXPECT_EQ(MahoMcpInputSynthesizer::NormalizeCrlf(""), "");
  EXPECT_EQ(MahoMcpInputSynthesizer::NormalizeCrlf("abc\r"), "abc\n");
  EXPECT_EQ(MahoMcpInputSynthesizer::NormalizeCrlf("\r\n\r\n"), "\n\n");
  EXPECT_EQ(MahoMcpInputSynthesizer::NormalizeCrlf("\r\r"), "\n\n");
}

TEST(MahoMcpInputSynthesizerTest, PacingPolicy_KoreanPunctuationAndWhitespace) {
  TypingPacingPolicy policy;
  policy.base_range_ms = {10, 20};
  policy.punctuation_range_ms = {120, 240};
  policy.space_range_ms = {40, 80};
  policy.rng_seed = 42ULL;

  SplitMix64Prng rng = policy.CreatePrng();

  // Korean characters: base range
  for (int i = 0; i < 50; ++i) {
    int32_t d = policy.delay_for(U'윤', U'인', rng);
    EXPECT_GE(d, 10);
    EXPECT_LE(d, 20);
  }

  // Punctuation before Korean: punctuation range
  for (int i = 0; i < 50; ++i) {
    int32_t d1 = policy.delay_for(U'.', U'윤', rng);
    EXPECT_GE(d1, 120);
    EXPECT_LE(d1, 240);

    int32_t d2 = policy.delay_for(U'!', U'도', rng);
    EXPECT_GE(d2, 120);
    EXPECT_LE(d2, 240);

    // CJK period
    int32_t d3 = policy.delay_for(0x3002u, U'윤', rng);
    EXPECT_GE(d3, 120);
    EXPECT_LE(d3, 240);
  }

  // Space pacing
  for (int i = 0; i < 50; ++i) {
    int32_t d1 = policy.delay_for(U'윤', U' ', rng);
    EXPECT_GE(d1, 40);
    EXPECT_LE(d1, 80);

    int32_t d2 = policy.delay_for(U' ', U'인', rng);
    EXPECT_GE(d2, 40);
    EXPECT_LE(d2, 80);
  }
}

TEST(MahoMcpInputSynthesizerTest, PacingPolicy_DeterministicSequence_Korean) {
  const uint64_t kTestSeed = 0xDEADBEEFCAFEULL;
  TypingPacingPolicy policy;
  policy.base_range_ms = {10, 30};
  policy.punctuation_range_ms = {120, 240};
  policy.space_range_ms = {40, 80};
  policy.rng_seed = kTestSeed;

  const std::string text = "안녕하세요, 마호 브라우저입니다! 123.";
  std::vector<TypingKeyUnit> units =
      MahoMcpInputSynthesizer::Utf8ToTypingUnits(text);

  auto generate_delays = [&](const TypingPacingPolicy& pol) {
    std::vector<int32_t> delays;
    SplitMix64Prng rng = pol.CreatePrng();
    for (size_t i = 0; i < units.size(); ++i) {
      char32_t prev_c = (i == 0) ? 0 : units[i - 1].code_point;
      char32_t next_c = units[i].code_point;
      delays.push_back(pol.delay_for(prev_c, next_c, rng));
    }
    return delays;
  };

  std::vector<int32_t> run1 = generate_delays(policy);
  std::vector<int32_t> run2 = generate_delays(policy);
  ASSERT_EQ(run1.size(), units.size());
  EXPECT_EQ(run1, run2);

  TypingPacingPolicy other_policy = policy;
  other_policy.rng_seed = 0x112233445566ULL;
  std::vector<int32_t> run_diff = generate_delays(other_policy);
  EXPECT_NE(run1, run_diff);
}

TEST(MahoMcpInputSynthesizerTest, PacingPolicy_CharOverloadNoSignedTruncation) {
  TypingPacingPolicy policy;
  policy.base_range_ms = {10, 30};
  policy.punctuation_range_ms = {120, 240};
  policy.space_range_ms = {40, 80};
  policy.rng_seed = 100ULL;

  SplitMix64Prng rng = policy.CreatePrng();
  // char16_t overload
  int32_t d16 = policy.delay_for(u'윤', u'인', rng);
  EXPECT_GE(d16, 10);
  EXPECT_LE(d16, 30);

  // char signed casting test
  char signed_c = static_cast<char>(0xEA);
  int32_t d_char = policy.delay_for(signed_c, signed_c, rng);
  EXPECT_GE(d_char, 10);
  EXPECT_LE(d_char, 30);
}

TEST(MahoMcpInputSynthesizerTest, EaseInOutFractionEndpointsAndMidpoint) {
  EXPECT_DOUBLE_EQ(MahoMcpInputSynthesizer::EaseInOutFraction(-0.5), 0.0);
  EXPECT_DOUBLE_EQ(MahoMcpInputSynthesizer::EaseInOutFraction(0.0), 0.0);
  EXPECT_DOUBLE_EQ(MahoMcpInputSynthesizer::EaseInOutFraction(1.0), 1.0);
  EXPECT_DOUBLE_EQ(MahoMcpInputSynthesizer::EaseInOutFraction(1.5), 1.0);
  EXPECT_NEAR(MahoMcpInputSynthesizer::EaseInOutFraction(0.5), 0.5, 1e-9);
}

TEST(MahoMcpInputSynthesizerTest, EaseInOutFractionMonotonic) {
  double previous = MahoMcpInputSynthesizer::EaseInOutFraction(0.0);
  for (int i = 1; i <= 100; ++i) {
    const double current =
        MahoMcpInputSynthesizer::EaseInOutFraction(i / 100.0);
    EXPECT_GE(current, previous);
    previous = current;
  }
}

TEST(MahoMcpInputSynthesizerTest, ComputeTrajectoryDeterministicExactEnd) {
  const gfx::PointF start(10.0f, 20.0f);
  const gfx::PointF end(310.0f, 220.0f);

  SplitMix64Prng rng_a(42ULL);
  const std::vector<gfx::PointF> first =
      MahoMcpInputSynthesizer::ComputeTrajectory(start, end, 12, 2, rng_a);
  SplitMix64Prng rng_b(42ULL);
  const std::vector<gfx::PointF> second =
      MahoMcpInputSynthesizer::ComputeTrajectory(start, end, 12, 2, rng_b);

  ASSERT_EQ(first.size(), 12u);
  ASSERT_EQ(first.size(), second.size());
  for (size_t i = 0; i < first.size(); ++i) {
    EXPECT_FLOAT_EQ(first[i].x(), second[i].x());
    EXPECT_FLOAT_EQ(first[i].y(), second[i].y());
  }
  // Final point is the exact click target.
  EXPECT_FLOAT_EQ(first.back().x(), end.x());
  EXPECT_FLOAT_EQ(first.back().y(), end.y());
  // Progress strictly advances toward the target on both axes.
  for (size_t i = 1; i + 1 < first.size(); ++i) {
    EXPECT_GT(first[i].x(), start.x());
    EXPECT_LT(first[i].x(), end.x());
    EXPECT_GT(first[i].y(), start.y());
    EXPECT_LT(first[i].y(), end.y());
  }
}

TEST(MahoMcpInputSynthesizerTest, ComputeTrajectoryJitterBounded) {
  const gfx::PointF start(0.0f, 0.0f);
  const gfx::PointF end(500.0f, -200.0f);
  const int kJitter = 3;

  SplitMix64Prng rng(7ULL);
  const std::vector<gfx::PointF> points =
      MahoMcpInputSynthesizer::ComputeTrajectory(start, end, 20, kJitter, rng);
  ASSERT_EQ(points.size(), 20u);
  for (size_t i = 0; i + 1 < points.size(); ++i) {
    // Ease-in-out progress stays within the axis bounds plus jitter slack.
    EXPECT_GE(points[i].x(), std::min(start.x(), end.x()) - kJitter - 1.0f);
    EXPECT_LE(points[i].x(), std::max(start.x(), end.x()) + kJitter + 1.0f);
    EXPECT_GE(points[i].y(), std::min(start.y(), end.y()) - kJitter - 1.0f);
    EXPECT_LE(points[i].y(), std::max(start.y(), end.y()) + kJitter + 1.0f);
  }
  EXPECT_FLOAT_EQ(points.back().x(), end.x());
  EXPECT_FLOAT_EQ(points.back().y(), end.y());
}

TEST(MahoMcpInputSynthesizerTest, ComputeTrajectoryClampsWaypointCount) {
  const gfx::PointF start(0.0f, 0.0f);
  const gfx::PointF end(10.0f, 10.0f);
  SplitMix64Prng rng(1ULL);

  const auto tiny =
      MahoMcpInputSynthesizer::ComputeTrajectory(start, end, 0, 0, rng);
  EXPECT_EQ(tiny.size(), 2u);
  EXPECT_FLOAT_EQ(tiny.back().x(), end.x());

  const auto huge =
      MahoMcpInputSynthesizer::ComputeTrajectory(start, end, 500, 0, rng);
  EXPECT_EQ(huge.size(), 64u);
}

TEST(MahoMcpInputSynthesizerClickMotionProfileTest, DefaultsValidate) {
  EXPECT_TRUE((ClickMotionProfile{}).validate());
}

TEST(MahoMcpInputSynthesizerClickMotionProfileTest,
     InvalidConfigurationsRejected) {
  ClickMotionProfile profile;

  profile.min_waypoints = 0;
  EXPECT_FALSE(profile.validate());

  profile = ClickMotionProfile{};
  profile.max_waypoints = 65;
  EXPECT_FALSE(profile.validate());

  profile = ClickMotionProfile{};
  profile.total_duration_ms = {250, 80};
  EXPECT_FALSE(profile.validate());

  profile = ClickMotionProfile{};
  profile.total_duration_ms = {80, 2000};
  EXPECT_FALSE(profile.validate());

  profile = ClickMotionProfile{};
  profile.approach_jitter_px = 11;
  EXPECT_FALSE(profile.validate());

  profile = ClickMotionProfile{};
  profile.pre_click_dwell_ms = {40, 6000};
  EXPECT_FALSE(profile.validate());
}

TEST(MahoMcpInputSynthesizerTest, HybridCoordinates_RetinaMac) {
  VisualFrame frame;
  frame.token = "token-retina";
  frame.viewport_css_size = gfx::SizeF(1280.0f, 800.0f);
  frame.bitmap_size = gfx::Size(2560, 1600);  // 2x Retina capture
  frame.device_scale_factor = 2.0f;
  frame.page_zoom_factor = 1.0f;
  frame.page_scale_factor = 1.0f;
  frame.visual_viewport_offset = gfx::PointF(0.0f, 0.0f);
  frame.view_bounds_in_screen = gfx::Rect(100, 200, 1280, 800);

  const gfx::PointF css(50.0f, 60.0f);
  auto resolved = MahoMcpInputSynthesizer::ResolveNativePoint(frame, css);

#if BUILDFLAG(IS_MAC)
  ASSERT_TRUE(resolved.has_value());
  // Screen DIP: 100 + 50 = 150, 200 + 60 = 260
  EXPECT_FLOAT_EQ(resolved->screen_dip.x(), 150.0f);
  EXPECT_FLOAT_EQ(resolved->screen_dip.y(), 260.0f);

  // macOS global CG points must be UNSCALED global display points (DIPs).
  // Strictly assert NO Retina multiplication (not 300, not 520).
  EXPECT_FLOAT_EQ(resolved->mac_cg_point.x(), 150.0f);
  EXPECT_FLOAT_EQ(resolved->mac_cg_point.y(), 260.0f);
  EXPECT_NE(resolved->mac_cg_point.x(), 150.0f * frame.device_scale_factor);
  EXPECT_NE(resolved->mac_cg_point.y(), 260.0f * frame.device_scale_factor);

  // Strictly assert NO Y re-inversion (CG event points have top-left origin).
  EXPECT_FLOAT_EQ(resolved->mac_cg_point.y(), 260.0f);
#else
  // Non-mac/non-win platforms must not report a native point.
  EXPECT_FALSE(resolved.has_value());
#endif
}

TEST(MahoMcpInputSynthesizerTest, HybridCoordinates_ZoomAndScroll) {
  VisualFrame frame;
  frame.token = "token-zoom-scroll";
  frame.viewport_css_size = gfx::SizeF(800.0f, 600.0f);
  frame.bitmap_size = gfx::Size(1200, 900);  // 1.5x zoom
  frame.page_zoom_factor = 1.5f;
  frame.page_scale_factor = 1.0f;
  frame.visual_viewport_offset = gfx::PointF(20.0f, 30.0f);
  frame.view_bounds_in_screen = gfx::Rect(50, 60, 800, 600);

  // Target point: CSS (100, 150)
  // Viewport relative: rel_x = 100 - 20 = 80, rel_y = 150 - 30 = 120
  // View DIP: 80 * 1.5 = 120, 120 * 1.5 = 180
  // Screen DIP: 50 + 120 = 170, 60 + 180 = 240
  auto dip = MahoMcpInputSynthesizer::CssToScreenDip(frame, gfx::PointF(100.0f, 150.0f));
  ASSERT_TRUE(dip.has_value());
  EXPECT_FLOAT_EQ(dip->x(), 170.0f);
  EXPECT_FLOAT_EQ(dip->y(), 240.0f);

  // Target point scrolled out of visible view (< offset)
  auto scrolled_out = MahoMcpInputSynthesizer::CssToScreenDip(frame, gfx::PointF(10.0f, 10.0f));
  EXPECT_FALSE(scrolled_out.has_value());
}

TEST(MahoMcpInputSynthesizerTest, HybridCoordinates_NegativeMonitorOrigins) {
  VisualFrame frame;
  frame.token = "token-neg-origin";
  // Secondary monitor to the left and above primary: origin (-1920, -500)
  frame.view_bounds_in_screen = gfx::Rect(-1920, -500, 1920, 1080);
  frame.viewport_css_size = gfx::SizeF(1920.0f, 1080.0f);
  frame.bitmap_size = gfx::Size(1920, 1080);
  frame.page_zoom_factor = 1.0f;
  frame.page_scale_factor = 1.0f;
  frame.visual_viewport_offset = gfx::PointF(0.0f, 0.0f);

  const gfx::PointF css(200.0f, 300.0f);
  auto dip = MahoMcpInputSynthesizer::CssToScreenDip(frame, css);
  ASSERT_TRUE(dip.has_value());
  EXPECT_FLOAT_EQ(dip->x(), -1720.0f);
  EXPECT_FLOAT_EQ(dip->y(), -200.0f);

  // Test Windows virtual desktop normalization with negative origin
  // Virtual screen spanning two monitors: left = -1920, top = -500, width = 3840, height = 1580
  const gfx::Rect virtual_screen(-1920, -500, 3840, 1580);
  const gfx::Point pixel_point(-1720, -200);

  auto win_pt = MahoMcpInputSynthesizer::NormalizeWindowsNativePoint(
      *dip, pixel_point, virtual_screen);
  ASSERT_TRUE(win_pt.has_value());
  EXPECT_EQ(win_pt->win_pixel_point, pixel_point);

  // Formula: (pixel - origin) * 65535 / (extent - 1)
  // x: (-1720 - (-1920)) * 65535 / (3840 - 1) = 200 * 65535 / 3839 = 3414
  // y: (-200 - (-500)) * 65535 / (1580 - 1) = 300 * 65535 / 1579 = 12451
  EXPECT_EQ(win_pt->win_normalized_x, 3414);
  EXPECT_EQ(win_pt->win_normalized_y, 12451);
  EXPECT_TRUE(win_pt->win_flags & kMouseEventFVirtualDesk);
  EXPECT_TRUE(win_pt->win_flags & kMouseEventFAbsolute);
  EXPECT_TRUE(win_pt->win_flags & kMouseEventFMove);

  // Corner point at exact origin (-1920, -500) must normalize to (0, 0)
  auto origin_pt = MahoMcpInputSynthesizer::NormalizeWindowsNativePoint(
      gfx::PointF(-1920.0f, -500.0f), gfx::Point(-1920, -500), virtual_screen);
  ASSERT_TRUE(origin_pt.has_value());
  EXPECT_EQ(origin_pt->win_normalized_x, 0);
  EXPECT_EQ(origin_pt->win_normalized_y, 0);
}

TEST(MahoMcpInputSynthesizerTest, HybridCoordinates_MixedDpiWindows) {
  // Test that Windows physical virtual-desktop normalization honors display-aware
  // pixel coordinates from ScreenWin and rejects a global naive dip*scale shortcut.
  // Display 1: 1.0x at (0, 0, 1920, 1080)
  // Display 2: 2.0x at (1920, 0, 1920, 1080 DIP -> physical 3840x2160)
  // Total virtual desktop physical bounds: (0, 0, 5760, 2160)
  const gfx::Rect virtual_desktop(0, 0, 5760, 2160);

  // Suppose target is on Display 2 at DIP (2000, 400).
  // DIP offset within Display 2: (2000 - 1920) = 80 DIP.
  // Display 2 has scale 2.0, so physical pixel is 1920 + 80 * 2 = 2080.
  const gfx::PointF screen_dip(2000.0f, 400.0f);
  const gfx::Point display_aware_pixel(2080, 800);

  auto normalized = MahoMcpInputSynthesizer::NormalizeWindowsNativePoint(
      screen_dip, display_aware_pixel, virtual_desktop);
  ASSERT_TRUE(normalized.has_value());

  // Expected normalized with display-aware pixel:
  // (2080 - 0) * 65535 / (5760 - 1) = 23669
  // (800 - 0) * 65535 / (2160 - 1) = 24283
  EXPECT_EQ(normalized->win_normalized_x, 23669);
  EXPECT_EQ(normalized->win_normalized_y, 24283);

  // If a naive global dip*scale shortcut was used:
  // Shortcut at 1.0x: pixel = (2000, 400) -> normalized_x = 2000 * 65535 / 5759 = 22759
  const int naive_1x_norm = static_cast<int>(2000LL * 65535 / 5759);
  EXPECT_NE(normalized->win_normalized_x, naive_1x_norm);

  // Shortcut at 2.0x: pixel = (4000, 800) -> normalized_x = 4000 * 65535 / 5759 = 45518
  const int naive_2x_norm = static_cast<int>(4000LL * 65535 / 5759);
  EXPECT_NE(normalized->win_normalized_x, naive_2x_norm);
}

TEST(MahoMcpInputSynthesizerTest, HybridCoordinates_MovedWindowRejected) {
  VisualFrame frame;
  frame.token = "token-moved";
  frame.viewport_css_size = gfx::SizeF(800.0f, 600.0f);
  frame.bitmap_size = gfx::Size(800, 600);
  frame.view_bounds_in_screen = gfx::Rect(100, 100, 800, 600);

  // Live bounds indicate the window moved to (200, 150)
  frame.live_view_bounds = gfx::Rect(200, 150, 800, 600);

  auto resolved = MahoMcpInputSynthesizer::ResolveNativePoint(frame, gfx::PointF(50.0f, 50.0f));
  EXPECT_FALSE(resolved.has_value());
}

TEST(MahoMcpInputSynthesizerTest, HybridCoordinates_StaleScreenshotRejected) {
  VisualFrame frame;
  frame.token = "token-valid";
  frame.viewport_css_size = gfx::SizeF(800.0f, 600.0f);
  frame.bitmap_size = gfx::Size(800, 600);
  frame.view_bounds_in_screen = gfx::Rect(0, 0, 800, 600);
  frame.view_transform_generation = 5;
  frame.document_epoch = 2;

  // Valid frame resolves
  EXPECT_TRUE(MahoMcpInputSynthesizer::CssToScreenDip(frame, gfx::PointF(10.0f, 10.0f)).has_value());

  // Stale flag set
  {
    VisualFrame stale_frame = frame;
    stale_frame.is_stale = true;
    EXPECT_FALSE(MahoMcpInputSynthesizer::CssToScreenDip(stale_frame, gfx::PointF(10.0f, 10.0f)).has_value());
  }

  // Transform generation mismatch (e.g. scrolled or resized since capture)
  {
    VisualFrame mismatched_gen = frame;
    mismatched_gen.live_transform_generation = 6;
    EXPECT_FALSE(MahoMcpInputSynthesizer::CssToScreenDip(mismatched_gen, gfx::PointF(10.0f, 10.0f)).has_value());
  }

  // Document epoch mismatch (navigated since capture)
  {
    VisualFrame mismatched_doc = frame;
    mismatched_doc.live_document_epoch = 3;
    EXPECT_FALSE(MahoMcpInputSynthesizer::CssToScreenDip(mismatched_doc, gfx::PointF(10.0f, 10.0f)).has_value());
  }

  // Empty token
  {
    VisualFrame empty_token = frame;
    empty_token.token = "";
    EXPECT_FALSE(MahoMcpInputSynthesizer::CssToScreenDip(empty_token, gfx::PointF(10.0f, 10.0f)).has_value());
  }
}

TEST(MahoMcpInputSynthesizerTest, HybridCoordinates_BitmapToCssMappingVerification) {
  VisualFrame frame;
  frame.token = "token-mapping";
  frame.viewport_css_size = gfx::SizeF(800.0f, 600.0f);
  frame.bitmap_size = gfx::Size(800, 600);
  frame.view_bounds_in_screen = gfx::Rect(0, 0, 800, 600);

  // Negative CSS coordinates rejected
  EXPECT_FALSE(MahoMcpInputSynthesizer::CssToScreenDip(frame, gfx::PointF(-1.0f, 50.0f)).has_value());

  // Coordinates exceeding viewport CSS size rejected
  EXPECT_FALSE(MahoMcpInputSynthesizer::CssToScreenDip(frame, gfx::PointF(801.0f, 50.0f)).has_value());
  EXPECT_FALSE(MahoMcpInputSynthesizer::CssToScreenDip(frame, gfx::PointF(50.0f, 601.0f)).has_value());

  // Inconsistent bitmap aspect ratio rejected (scale_x = 1.0, scale_y = 0.33)
  {
    VisualFrame bad_aspect = frame;
    bad_aspect.bitmap_size = gfx::Size(800, 200);
    EXPECT_FALSE(MahoMcpInputSynthesizer::CssToScreenDip(bad_aspect, gfx::PointF(50.0f, 50.0f)).has_value());
  }

  // Empty/zero bitmap dimensions rejected
  {
    VisualFrame empty_bm = frame;
    empty_bm.bitmap_size = gfx::Size(0, 0);
    EXPECT_FALSE(MahoMcpInputSynthesizer::CssToScreenDip(empty_bm, gfx::PointF(50.0f, 50.0f)).has_value());
  }
}

TEST(MahoMcpInputSynthesizerTest, HybridCoordinates_VirtualDeskBoundaryLimits) {
  const gfx::Rect virtual_screen(0, 0, 1920, 1080);

  // Origin pixel (0, 0) must normalize to exactly (0, 0)
  auto pt_origin = MahoMcpInputSynthesizer::NormalizeWindowsNativePoint(
      gfx::PointF(0.0f, 0.0f), gfx::Point(0, 0), virtual_screen);
  ASSERT_TRUE(pt_origin.has_value());
  EXPECT_EQ(pt_origin->win_normalized_x, 0);
  EXPECT_EQ(pt_origin->win_normalized_y, 0);

  // Maximum extent pixel (1919, 1079) must normalize to exactly (65535, 65535)
  auto pt_max = MahoMcpInputSynthesizer::NormalizeWindowsNativePoint(
      gfx::PointF(1919.0f, 1079.0f), gfx::Point(1919, 1079), virtual_screen);
  ASSERT_TRUE(pt_max.has_value());
  EXPECT_EQ(pt_max->win_normalized_x, 65535);
  EXPECT_EQ(pt_max->win_normalized_y, 65535);

  // Clamping: pixels outside virtual desktop must clamp to 0 and 65535
  auto pt_under = MahoMcpInputSynthesizer::NormalizeWindowsNativePoint(
      gfx::PointF(-50.0f, -50.0f), gfx::Point(-50, -50), virtual_screen);
  ASSERT_TRUE(pt_under.has_value());
  EXPECT_EQ(pt_under->win_normalized_x, 0);
  EXPECT_EQ(pt_under->win_normalized_y, 0);

  auto pt_over = MahoMcpInputSynthesizer::NormalizeWindowsNativePoint(
      gfx::PointF(2000.0f, 1200.0f), gfx::Point(2000, 1200), virtual_screen);
  ASSERT_TRUE(pt_over.has_value());
  EXPECT_EQ(pt_over->win_normalized_x, 65535);
  EXPECT_EQ(pt_over->win_normalized_y, 65535);

  // Virtual screen with degenerate extent returns nullopt
  EXPECT_FALSE(MahoMcpInputSynthesizer::NormalizeWindowsNativePoint(
                   gfx::PointF(0.0f, 0.0f), gfx::Point(0, 0), gfx::Rect(0, 0, 1, 1080))
                   .has_value());
}

TEST(MahoMcpInputSynthesizerTest, HybridMotion_GaussianJitterBoundedAndTerminalExact) {
  ClickMotionProfile profile;
  profile.rng_seed = 42u;
  const gfx::PointF start(50.0f, 50.0f);
  const gfx::RectF target(400.0f, 300.0f, 120.0f, 40.0f);
  const gfx::PointF center(target.x() + target.width() / 2.0f,
                           target.y() + target.height() / 2.0f);

  SplitMix64Prng rng(*profile.rng_seed);
  auto plan = MahoMcpInputSynthesizer::ComputeBoundedMotion(
      start, target, profile, rng);

  ASSERT_FALSE(plan.waypoints.empty());
  // Approach waypoints carry bounded perpendicular jitter (3-sigma clamp at
  // 2.4 px for sigma 0.8) around the start->center centerline.
  const float dx = center.x() - start.x();
  const float dy = center.y() - start.y();
  const float length = std::sqrt(dx * dx + dy * dy);
  const float perp_x = -dy / length;
  const float perp_y = dx / length;
  for (size_t i = 0; i + 1 < plan.waypoints.size(); ++i) {
    const gfx::PointF& p = plan.waypoints[i];
    const double t = (length > 0.0f)
                         ? ((p.x() - start.x()) * dx +
                            (p.y() - start.y()) * dy) /
                               (length * length)
                         : 0.0;
    const gfx::PointF on_line(start.x() + dx * static_cast<float>(t),
                              start.y() + dy * static_cast<float>(t));
    const float perp_offset =
        (p.x() - on_line.x()) * perp_x + (p.y() - on_line.y()) * perp_y;
    EXPECT_LE(std::abs(perp_offset), 2.4f) << i;
  }
  // Terminal waypoint is the exact target center.
  EXPECT_EQ(plan.waypoints.back().x(), center.x());
  EXPECT_EQ(plan.waypoints.back().y(), center.y());
  // Deterministic: same seed reproduces the same plan.
  SplitMix64Prng rng2(*profile.rng_seed);
  auto plan2 = MahoMcpInputSynthesizer::ComputeBoundedMotion(
      start, target, profile, rng2);
  EXPECT_EQ(plan.waypoints.size(), plan2.waypoints.size());
  EXPECT_EQ(plan.dwell_ms, plan2.dwell_ms);
  EXPECT_EQ(plan.overshoot_applied, plan2.overshoot_applied);
}

TEST(MahoMcpInputSynthesizerTest, HybridMotion_OvershootBoundedInsideTargetAndCorrected) {
  // Force the overshoot branch on every motion.
  ClickMotionProfile profile;
  profile.overshoot_probability = 1.0;
  profile.rng_seed = 7u;
  const gfx::PointF start(10.0f, 10.0f);
  const gfx::RectF target(300.0f, 200.0f, 200.0f, 80.0f);
  const gfx::PointF center(target.x() + target.width() / 2.0f,
                           target.y() + target.height() / 2.0f);

  int overshoot_count = 0;
  for (int seed = 0; seed < 50; ++seed) {
    profile.rng_seed = static_cast<uint64_t>(seed) + 100u;
    SplitMix64Prng rng(*profile.rng_seed);
    auto plan = MahoMcpInputSynthesizer::ComputeBoundedMotion(
        start, target, profile, rng);
    ASSERT_FALSE(plan.waypoints.empty());
    if (plan.overshoot_applied) {
      ++overshoot_count;
      // The overshoot waypoint sits before the exact-center correction and
      // stays inside the target rect.
      const gfx::PointF& past = plan.waypoints[plan.waypoints.size() - 2];
      EXPECT_GE(past.x(), target.x());
      EXPECT_LE(past.x(), target.x() + target.width());
      EXPECT_GE(past.y(), target.y());
      EXPECT_LE(past.y(), target.y() + target.height());
      // Overshoot magnitude along the approach direction is bounded 2-4 px.
      const float off = std::abs((past.x() - center.x()) *
                                     (center.x() - start.x()) +
                                 (past.y() - center.y()) *
                                     (center.y() - start.y())) /
                        std::sqrt((center.x() - start.x()) *
                                      (center.x() - start.x()) +
                                  (center.y() - start.y()) *
                                      (center.y() - start.y()));
      EXPECT_GE(off, 2.0f - 0.01f);
      EXPECT_LE(off, 4.0f + 0.01f);
    }
    // Correction/final: exact center.
    EXPECT_EQ(plan.waypoints.back().x(), center.x());
    EXPECT_EQ(plan.waypoints.back().y(), center.y());
  }
  EXPECT_GT(overshoot_count, 0);
}

TEST(MahoMcpInputSynthesizerTest, HybridMotion_TinyTargetDisablesOvershoot) {
  ClickMotionProfile profile;
  profile.overshoot_probability = 1.0;
  const gfx::PointF start(10.0f, 10.0f);
  const gfx::RectF tiny(300.0f, 200.0f, 4.0f, 4.0f);

  for (int seed = 0; seed < 50; ++seed) {
    profile.rng_seed = static_cast<uint64_t>(seed) + 300u;
    SplitMix64Prng rng(*profile.rng_seed);
    auto plan = MahoMcpInputSynthesizer::ComputeBoundedMotion(
        start, tiny, profile, rng);
    EXPECT_FALSE(plan.overshoot_applied) << seed;
  }
}

TEST(MahoMcpInputSynthesizerTest, HybridMotion_DwellTruncatedLogNormalInRange) {
  ClickMotionProfile profile;
  int in_range = 0;
  int64_t total = 0;
  const int kSamples = 500;
  for (int seed = 0; seed < kSamples; ++seed) {
    profile.rng_seed = static_cast<uint64_t>(seed) + 500u;
    SplitMix64Prng rng(*profile.rng_seed);
    const int32_t dwell = MahoMcpInputSynthesizer::NextDwellMs(profile, rng);
    ASSERT_GE(dwell, 80);
    ASSERT_LE(dwell, 220);
    ++in_range;
    total += dwell;
  }
  EXPECT_EQ(in_range, kSamples);
  // Truncation keeps the sample mean inside the window (not pinned to an
  // edge), i.e. the log-normal actually varies.
  const int64_t mean = total / kSamples;
  EXPECT_GT(mean, 100);
  EXPECT_LT(mean, 200);
}

TEST(MahoMcpInputSynthesizerTest, HybridMotion_TerminalStayInsideTargetRect) {
  ClickMotionProfile profile;
  profile.overshoot_probability = 1.0;
  const gfx::PointF start(0.0f, 0.0f);
  const gfx::RectF target(500.0f, 400.0f, 60.0f, 24.0f);

  for (int seed = 0; seed < 100; ++seed) {
    profile.rng_seed = static_cast<uint64_t>(seed) + 900u;
    SplitMix64Prng rng(*profile.rng_seed);
    auto plan = MahoMcpInputSynthesizer::ComputeBoundedMotion(
        start, target, profile, rng);
    ASSERT_GE(plan.waypoints.size(), 2u);
    // The terminal two waypoints (overshoot + correction) stay inside the
    // target rect.
    for (size_t i = plan.waypoints.size() - 2; i < plan.waypoints.size(); ++i) {
      const gfx::PointF& p = plan.waypoints[i];
      EXPECT_GE(p.x(), target.x()) << seed;
      EXPECT_LE(p.x(), target.x() + target.width()) << seed;
      EXPECT_GE(p.y(), target.y()) << seed;
      EXPECT_LE(p.y(), target.y() + target.height()) << seed;
    }
  }
}

TEST(MahoMcpInputSynthesizerTest, HybridAvailability_ResultContract) {
  NativeInputAvailability default_avail;
  EXPECT_FALSE(default_avail.available);
  EXPECT_TRUE(default_avail.reason.empty());
  EXPECT_EQ(default_avail.raw_os_status, 0);

  NativeInputPreflightResult alias_check;
  EXPECT_EQ(alias_check, default_avail);

  NativeInputAvailability custom;
  custom.available = true;
  custom.reason = "ready";
  custom.raw_os_status = 1;
  EXPECT_TRUE(custom.available);
  EXPECT_EQ(custom.reason, "ready");
  EXPECT_EQ(custom.raw_os_status, 1);
}

#if BUILDFLAG(IS_MAC)
TEST(MahoMcpInputSynthesizerTest, HybridAvailability_MacProbeHonestReporting) {
  // 1. Trusted override test: must never claim trust proves delivery
  NativeInputAvailability trusted_res =
      CheckNativeInputAvailabilityMac(/*trusted_override=*/true);
  EXPECT_TRUE(trusted_res.available);
  EXPECT_EQ(trusted_res.raw_os_status, 1);
  EXPECT_NE(trusted_res.reason.find("trust does not prove event delivery"),
            std::string::npos);
  EXPECT_EQ(trusted_res.reason.find("guarantees delivery"), std::string::npos);

  // 2. Untrusted override test: user prompt suppressed
  NativeInputAvailability untrusted_res =
      CheckNativeInputAvailabilityMac(/*trusted_override=*/false);
  EXPECT_FALSE(untrusted_res.available);
  EXPECT_EQ(untrusted_res.raw_os_status, 0);
  EXPECT_NE(untrusted_res.reason.find("untrusted"), std::string::npos);
  EXPECT_NE(untrusted_res.reason.find("prompt suppressed"), std::string::npos);

  // 3. Real probe execution: verifies live AXIsProcessTrustedWithOptions(nullptr) call
  NativeInputAvailability live_res = CheckNativeInputAvailabilityMac();
  EXPECT_TRUE(live_res.raw_os_status == 0 || live_res.raw_os_status == 1);
  EXPECT_FALSE(live_res.reason.empty());
}
#endif

#if BUILDFLAG(IS_WIN)
TEST(MahoMcpInputSynthesizerTest, HybridAvailability_WindowsProbeHonestReporting) {
  // 1. Interactive desktop check
  NativeInputAvailability live_res =
      CheckNativeInputAvailabilityWin(/*interactive_override=*/true,
                                     /*last_error_override=*/0,
                                     /*send_input_override=*/1);
  EXPECT_TRUE(live_res.available);
  EXPECT_EQ(live_res.raw_os_status, 0);

  // 2. Non-interactive desktop check
  NativeInputAvailability non_interactive =
      CheckNativeInputAvailabilityWin(/*interactive_override=*/false,
                                     /*last_error_override=*/5,
                                     /*send_input_override=*/std::nullopt);
  EXPECT_FALSE(non_interactive.available);
  EXPECT_EQ(non_interactive.raw_os_status, 5);
  EXPECT_NE(non_interactive.reason.find("non-interactive"), std::string::npos);

  // 3. SendInput insertion failure: must never claim a SendInput error proves UIPI
  NativeInputAvailability insertion_fail =
      CheckNativeInputAvailabilityWin(/*interactive_override=*/true,
                                     /*last_error_override=*/5,
                                     /*send_input_override=*/0);
  EXPECT_FALSE(insertion_fail.available);
  EXPECT_EQ(insertion_fail.raw_os_status, 5);
  EXPECT_NE(insertion_fail.reason.find("cannot be proven to be UIPI"),
            std::string::npos);
  EXPECT_EQ(insertion_fail.reason.find("proves UIPI"), std::string::npos);
}
#endif

#if !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_WIN)
TEST(MahoMcpInputSynthesizerTest, HybridAvailability_LinuxProbeReportsUnavailable) {
  NativeInputAvailability linux_res =
      MahoMcpInputSynthesizer::CheckNativeInputAvailability();
  EXPECT_FALSE(linux_res.available);
  EXPECT_EQ(linux_res.raw_os_status, 0);
  EXPECT_NE(linux_res.reason.find("unsupported on linux"), std::string::npos);
}
#endif

TEST(MahoMcpInputSynthesizerTest, HybridAvailability_OverrideTesting) {
  NativeInputAvailability override_val;
  override_val.available = false;
  override_val.reason = "simulated_override_unavailable";
  override_val.raw_os_status = 999;

  MahoMcpInputSynthesizer::SetNativeInputAvailabilityOverrideForTesting(override_val);
  EXPECT_EQ(MahoMcpInputSynthesizer::CheckNativeInputAvailability(), override_val);

  MahoMcpInputSynthesizer::SetNativeInputAvailabilityOverrideForTesting(std::nullopt);
  EXPECT_NE(MahoMcpInputSynthesizer::CheckNativeInputAvailability().reason,
            "simulated_override_unavailable");
}

namespace {

VisualFrame CreateValidVisualFrameForTesting(const std::string& token = "valid-token",
                                             uint64_t doc_epoch = 1,
                                             uint64_t lease_epoch = 1) {
  VisualFrame frame;
  frame.token = token;
  frame.viewport_css_size = gfx::SizeF(800.0f, 600.0f);
  frame.bitmap_size = gfx::Size(800, 600);
  frame.view_bounds_in_screen = gfx::Rect(0, 0, 800, 600);
  frame.view_transform_generation = 1;
  frame.document_epoch = doc_epoch;
  frame.lease_epoch = lease_epoch;
  return frame;
}

}  // namespace

TEST(MahoMcpInputSynthesizerTest, HybridNative_RequestValidation_Click) {
  NativeActionRequest req;
  req.type = NativeActionRequest::ActionType::kClick;
  req.frame_token = "token-1";
  req.lease_epoch = 10;
  req.click_point_css = gfx::PointF(100.0f, 150.0f);
  req.target_rect_css = gfx::RectF(50.0f, 50.0f, 200.0f, 200.0f);

  // 1. Valid click request
  EXPECT_TRUE(req.Validate());

  // 2. Empty token fails
  {
    NativeActionRequest invalid = req;
    invalid.frame_token.clear();
    EXPECT_FALSE(invalid.Validate());
  }

  // 3. Zero lease epoch fails
  {
    NativeActionRequest invalid = req;
    invalid.lease_epoch = 0;
    EXPECT_FALSE(invalid.Validate());
  }

  // 4. NaN / Inf coordinates fail
  {
    NativeActionRequest invalid = req;
    invalid.click_point_css = gfx::PointF(std::numeric_limits<float>::quiet_NaN(), 100.0f);
    EXPECT_FALSE(invalid.Validate());

    invalid.click_point_css = gfx::PointF(100.0f, std::numeric_limits<float>::infinity());
    EXPECT_FALSE(invalid.Validate());
  }

  // 5. Degenerate target rect fails
  {
    NativeActionRequest invalid = req;
    invalid.target_rect_css = gfx::RectF(50.0f, 50.0f, 0.0f, 100.0f);
    EXPECT_FALSE(invalid.Validate());

    invalid.target_rect_css = gfx::RectF(50.0f, 50.0f, 100.0f, -5.0f);
    EXPECT_FALSE(invalid.Validate());
  }

  // 6. Click point outside target rect fails
  {
    NativeActionRequest invalid = req;
    invalid.click_point_css = gfx::PointF(350.0f, 350.0f);
    EXPECT_FALSE(invalid.Validate());
  }
}

TEST(MahoMcpInputSynthesizerTest, HybridNative_RequestValidation_TypeAndKey) {
  // Type validation
  {
    NativeActionRequest req;
    req.type = NativeActionRequest::ActionType::kType;
    req.frame_token = "token-type";
    req.lease_epoch = 1;
    req.text = "hello";
    EXPECT_TRUE(req.Validate());

    req.text.clear();
    EXPECT_FALSE(req.Validate());
  }

  // Key validation
  {
    NativeActionRequest req;
    req.type = NativeActionRequest::ActionType::kKey;
    req.frame_token = "token-key";
    req.lease_epoch = 1;
    req.key = "Enter";
    EXPECT_TRUE(req.Validate());

    req.key.clear();
    EXPECT_FALSE(req.Validate());
  }
}

TEST(MahoMcpInputSynthesizerTest, HybridNative_TestDispatcherSeam_RecordsEvents) {
  TestNativeInputDispatcher dispatcher;
  EXPECT_EQ(dispatcher.event_count(), 0u);

  NativePoint pt;
  pt.screen_dip = gfx::PointF(10.0f, 20.0f);

  std::vector<NativeEvent> events;
  events.push_back(NativeEvent::MakeMouseMove(pt));
  events.push_back(NativeEvent::MakeMouseDown(pt, NativeEvent::MouseButton::kLeft));
  events.push_back(NativeEvent::MakeMouseUp(pt, NativeEvent::MouseButton::kLeft));
  events.push_back(NativeEvent::MakeKeyDown(ui::VKEY_RETURN));
  events.push_back(NativeEvent::MakeKeyUp(ui::VKEY_RETURN));
  events.push_back(NativeEvent::MakeKeyChar(U'A', u"A"));

  auto res = dispatcher.Dispatch(events);
  EXPECT_TRUE(res.ok());
  EXPECT_EQ(res.requested, 6u);
  EXPECT_EQ(res.inserted, 6u);
  EXPECT_EQ(dispatcher.event_count(), 6u);

  EXPECT_EQ(dispatcher.dispatched_events()[0].type, NativeEvent::Type::kMouseMove);
  EXPECT_EQ(dispatcher.dispatched_events()[1].type, NativeEvent::Type::kMouseDown);
  EXPECT_EQ(dispatcher.dispatched_events()[2].type, NativeEvent::Type::kMouseUp);
  EXPECT_EQ(dispatcher.dispatched_events()[3].type, NativeEvent::Type::kKeyDown);
  EXPECT_EQ(dispatcher.dispatched_events()[4].type, NativeEvent::Type::kKeyUp);
  EXPECT_EQ(dispatcher.dispatched_events()[5].type, NativeEvent::Type::kKeyChar);

  // Failure simulation
  dispatcher.set_fail_all(true);
  dispatcher.set_simulated_error(5);
  auto fail_res = dispatcher.Dispatch(events);
  EXPECT_FALSE(fail_res.ok());
  EXPECT_EQ(fail_res.inserted, 0u);
  EXPECT_EQ(fail_res.raw_os_error, 5);
}

TEST(MahoMcpInputSynthesizerTest, HybridNative_ClickLifecycle_CompleteSequence) {
  base::test::TaskEnvironment task_env;
  VisualFrame frame = CreateValidVisualFrameForTesting();
  NativeActionRequest req;
  req.type = NativeActionRequest::ActionType::kClick;
  req.frame_token = frame.token;
  req.tab_id = 1;
  req.lease_epoch = frame.lease_epoch;
  req.document_epoch = frame.document_epoch;
  req.click_point_css = gfx::PointF(100.0f, 100.0f);
  req.target_rect_css = gfx::RectF(50.0f, 50.0f, 200.0f, 200.0f);
  req.motion_profile.pre_click_dwell_ms = {0, 0};
  req.motion_profile.press_release_ms = {0, 0};

  TestNativeInputDispatcher dispatcher;
  // Before any press path, assert zero events observed
  EXPECT_EQ(dispatcher.event_count(), 0u);

  NativeActionResult result;
  bool completed = false;

  MahoMcpInputSynthesizer::DispatchNativeActionAsync(
      nullptr, frame, req, &dispatcher,
      base::BindOnce(
          [](NativeActionResult* out, bool* done, NativeActionResult r) {
            *out = std::move(r);
            *done = true;
          },
          &result, &completed));

  task_env.RunUntilIdle();
  EXPECT_TRUE(completed);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.cancelled);
  EXPECT_FALSE(result.cleanup_required);

  // Sequence: MouseMove -> MouseDown -> MouseUp
  ASSERT_EQ(dispatcher.event_count(), 3u);
  EXPECT_EQ(dispatcher.dispatched_events()[0].type, NativeEvent::Type::kMouseMove);
  EXPECT_EQ(dispatcher.dispatched_events()[1].type, NativeEvent::Type::kMouseDown);
  EXPECT_EQ(dispatcher.dispatched_events()[1].button, NativeEvent::MouseButton::kLeft);
  EXPECT_EQ(dispatcher.dispatched_events()[2].type, NativeEvent::Type::kMouseUp);
  EXPECT_EQ(dispatcher.dispatched_events()[2].button, NativeEvent::MouseButton::kLeft);
}

TEST(MahoMcpInputSynthesizerTest, HybridNative_CancellationBeforePress_ZeroEvents) {
  base::test::TaskEnvironment task_env;
  VisualFrame frame = CreateValidVisualFrameForTesting();
  NativeActionRequest req;
  req.type = NativeActionRequest::ActionType::kClick;
  req.frame_token = frame.token;
  req.tab_id = 1;
  req.lease_epoch = frame.lease_epoch;
  req.document_epoch = frame.document_epoch;
  req.click_point_css = gfx::PointF(100.0f, 100.0f);
  req.target_rect_css = gfx::RectF(50.0f, 50.0f, 200.0f, 200.0f);
  req.motion_profile.pre_click_dwell_ms = {50, 50};

  TestNativeInputDispatcher dispatcher;
  // Before press path, assert zero events observed
  EXPECT_EQ(dispatcher.event_count(), 0u);

  NativeActionResult result;
  bool completed = false;

  auto op = MahoMcpInputSynthesizer::DispatchNativeActionAsync(
      nullptr, frame, req, &dispatcher,
      base::BindOnce(
          [](NativeActionResult* out, bool* done, NativeActionResult r) {
            *out = std::move(r);
            *done = true;
          },
          &result, &completed));

  ASSERT_TRUE(op);
  // Cancel immediately before pre-click dwell expires (before press path)
  op->Cancel();

  task_env.RunUntilIdle();
  EXPECT_TRUE(completed);
  EXPECT_TRUE(result.cancelled);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cleanup_required);

  // Zero press events: no MouseDown or MouseUp ever dispatched!
  for (const auto& ev : dispatcher.dispatched_events()) {
    EXPECT_NE(ev.type, NativeEvent::Type::kMouseDown);
    EXPECT_NE(ev.type, NativeEvent::Type::kMouseUp);
  }
}

TEST(MahoMcpInputSynthesizerTest,
     HybridNative_CancellationDuringPress_ContextPreserved_CompensatingRelease) {
  base::test::TaskEnvironment task_env{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  VisualFrame frame = CreateValidVisualFrameForTesting();
  NativeActionRequest req;
  req.type = NativeActionRequest::ActionType::kClick;
  req.frame_token = frame.token;
  req.tab_id = 1;
  req.lease_epoch = frame.lease_epoch;
  req.document_epoch = frame.document_epoch;
  req.click_point_css = gfx::PointF(100.0f, 100.0f);
  req.target_rect_css = gfx::RectF(50.0f, 50.0f, 200.0f, 200.0f);
  req.motion_profile.pre_click_dwell_ms = {0, 0};
  req.motion_profile.press_release_ms = {100, 100};

  TestNativeInputDispatcher dispatcher;
  EXPECT_EQ(dispatcher.event_count(), 0u);

  NativeSecurityContext context;
  context.tab_id = 1;
  context.expected_lease_epoch = 1;
  context.expected_document_epoch = 1;
  context.is_foreground_window = true;
  context.is_target_tab_active = true;

  NativeActionResult result;
  bool completed = false;

  auto op = MahoMcpInputSynthesizer::DispatchNativeActionAsync(
      nullptr, frame, req, &dispatcher, context,
      base::BindOnce(
          [](NativeActionResult* out, bool* done, NativeActionResult r) {
            *out = std::move(r);
            *done = true;
          },
          &result, &completed));

  ASSERT_TRUE(op);
  task_env.FastForwardBy(base::Milliseconds(10));
  EXPECT_TRUE(op->mouse_pressed());
  EXPECT_EQ(dispatcher.dispatched_events()[1].type,
            NativeEvent::Type::kMouseDown);

  // Cancel while pressed with context preserved
  op->Cancel();

  EXPECT_TRUE(completed);
  EXPECT_TRUE(result.cancelled);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cleanup_required);
  // Compensating release dispatched safely
  ASSERT_GE(dispatcher.event_count(), 3u);
  EXPECT_EQ(dispatcher.dispatched_events().back().type,
            NativeEvent::Type::kMouseUp);
}

TEST(MahoMcpInputSynthesizerTest,
     HybridNative_CancellationDuringPress_ContextChanged_NeverBlindlyRelease) {
  base::test::TaskEnvironment task_env{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  VisualFrame frame = CreateValidVisualFrameForTesting();
  NativeActionRequest req;
  req.type = NativeActionRequest::ActionType::kClick;
  req.frame_token = frame.token;
  req.tab_id = 1;
  req.lease_epoch = frame.lease_epoch;
  req.document_epoch = frame.document_epoch;
  req.click_point_css = gfx::PointF(100.0f, 100.0f);
  req.target_rect_css = gfx::RectF(50.0f, 50.0f, 200.0f, 200.0f);
  req.motion_profile.pre_click_dwell_ms = {0, 0};
  req.motion_profile.press_release_ms = {100, 100};

  TestNativeInputDispatcher dispatcher;
  EXPECT_EQ(dispatcher.event_count(), 0u);

  bool foreground_active = true;
  NativeSecurityContext context;
  context.tab_id = 1;
  context.expected_lease_epoch = 1;
  context.expected_document_epoch = 1;
  context.is_foreground_window = true;
  context.is_target_tab_active = true;
  context.foreground_check =
      base::BindRepeating([](bool* fg) { return *fg; }, &foreground_active);

  NativeActionResult result;
  bool completed = false;

  auto op = MahoMcpInputSynthesizer::DispatchNativeActionAsync(
      nullptr, frame, req, &dispatcher, context,
      base::BindOnce(
          [](NativeActionResult* out, bool* done, NativeActionResult r) {
            *out = std::move(r);
            *done = true;
          },
          &result, &completed));

  ASSERT_TRUE(op);
  task_env.FastForwardBy(base::Milliseconds(10));
  EXPECT_TRUE(op->mouse_pressed());
  const size_t events_before_cancel = dispatcher.event_count();

  // Focus lost to a foreign window
  foreground_active = false;
  op->Cancel();

  EXPECT_TRUE(completed);
  EXPECT_TRUE(result.cancelled);
  EXPECT_FALSE(result.success);
  // Truthful cleanup_required reported!
  EXPECT_TRUE(result.cleanup_required);
  // NEVER blindly inject release into the new foreign window!
  EXPECT_EQ(dispatcher.event_count(), events_before_cancel);
}

TEST(MahoMcpInputSynthesizerTest,
     HybridNative_LeaseEpochAuthorization_RejectsStaleEpoch) {
  base::test::TaskEnvironment task_env;
  VisualFrame frame = CreateValidVisualFrameForTesting();
  NativeActionRequest req;
  req.type = NativeActionRequest::ActionType::kClick;
  req.frame_token = frame.token;
  req.tab_id = 1;
  req.lease_epoch = 5;  // Stale epoch
  req.document_epoch = frame.document_epoch;
  req.click_point_css = gfx::PointF(100.0f, 100.0f);
  req.target_rect_css = gfx::RectF(50.0f, 50.0f, 200.0f, 200.0f);

  TestNativeInputDispatcher dispatcher;
  EXPECT_EQ(dispatcher.event_count(), 0u);

  NativeSecurityContext context;
  context.tab_id = 1;
  context.expected_lease_epoch = 10;  // Registry is at epoch 10
  context.lease_epoch_lookup =
      base::BindRepeating([](int64_t) -> uint64_t { return 10; });

  NativeActionResult result;
  bool completed = false;

  MahoMcpInputSynthesizer::DispatchNativeActionAsync(
      nullptr, frame, req, &dispatcher, context,
      base::BindOnce(
          [](NativeActionResult* out, bool* done, NativeActionResult r) {
            *out = std::move(r);
            *done = true;
          },
          &result, &completed));

  task_env.RunUntilIdle();
  EXPECT_TRUE(completed);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, -32007);
  EXPECT_FALSE(result.cleanup_required);
  // Exactly 0 events dispatched!
  EXPECT_EQ(dispatcher.event_count(), 0u);
}

TEST(MahoMcpInputSynthesizerTest, HybridNative_Keyboard_UnicodeText) {
  base::test::TaskEnvironment task_env;
  VisualFrame frame = CreateValidVisualFrameForTesting();
  NativeActionRequest req;
  req.type = NativeActionRequest::ActionType::kType;
  req.frame_token = frame.token;
  req.tab_id = 1;
  req.lease_epoch = frame.lease_epoch;
  req.document_epoch = frame.document_epoch;
  req.text = "마호 🚀";

  TestNativeInputDispatcher dispatcher;
  EXPECT_EQ(dispatcher.event_count(), 0u);

  NativeActionResult result;
  bool completed = false;

  MahoMcpInputSynthesizer::DispatchNativeActionAsync(
      nullptr, frame, req, &dispatcher,
      base::BindOnce(
          [](NativeActionResult* out, bool* done, NativeActionResult r) {
            *out = std::move(r);
            *done = true;
          },
          &result, &completed));

  task_env.RunUntilIdle();
  EXPECT_TRUE(completed);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.cleanup_required);

  // 4 characters: '마', '호', ' ', '🚀'
  ASSERT_EQ(dispatcher.event_count(), 4u);
  EXPECT_EQ(dispatcher.dispatched_events()[0].code_point, 0xB9C8u);
  EXPECT_EQ(dispatcher.dispatched_events()[1].code_point, 0xD638u);
  EXPECT_EQ(dispatcher.dispatched_events()[2].code_point, U' ');
  EXPECT_EQ(dispatcher.dispatched_events()[3].code_point, 0x1F680u);
  EXPECT_EQ(dispatcher.dispatched_events()[3].text.size(), 2u);  // Surrogate pair
}

TEST(MahoMcpInputSynthesizerTest, HybridNative_Keyboard_NavigationAndChords) {
  base::test::TaskEnvironment task_env;
  VisualFrame frame = CreateValidVisualFrameForTesting();

  // Enter key press
  {
    NativeActionRequest req;
    req.type = NativeActionRequest::ActionType::kKey;
    req.frame_token = frame.token;
    req.tab_id = 1;
    req.lease_epoch = frame.lease_epoch;
    req.document_epoch = frame.document_epoch;
    req.key = "Enter";

    TestNativeInputDispatcher dispatcher;
    EXPECT_EQ(dispatcher.event_count(), 0u);

    NativeActionResult result;
    bool completed = false;

    MahoMcpInputSynthesizer::DispatchNativeActionAsync(
        nullptr, frame, req, &dispatcher,
        base::BindOnce(
            [](NativeActionResult* out, bool* done, NativeActionResult r) {
              *out = std::move(r);
              *done = true;
            },
            &result, &completed));

    task_env.RunUntilIdle();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(result.success);
    ASSERT_EQ(dispatcher.event_count(), 2u);
    EXPECT_EQ(dispatcher.dispatched_events()[0].type, NativeEvent::Type::kKeyDown);
    EXPECT_EQ(dispatcher.dispatched_events()[0].key_code, ui::VKEY_RETURN);
    EXPECT_EQ(dispatcher.dispatched_events()[1].type, NativeEvent::Type::kKeyUp);
    EXPECT_EQ(dispatcher.dispatched_events()[1].key_code, ui::VKEY_RETURN);
  }

  // Shift + Tab chord
  {
    NativeActionRequest req;
    req.type = NativeActionRequest::ActionType::kKey;
    req.frame_token = frame.token;
    req.tab_id = 1;
    req.lease_epoch = frame.lease_epoch;
    req.document_epoch = frame.document_epoch;
    req.key = "Tab";
    req.modifiers = {"shift"};

    TestNativeInputDispatcher dispatcher;
    EXPECT_EQ(dispatcher.event_count(), 0u);

    NativeActionResult result;
    bool completed = false;

    MahoMcpInputSynthesizer::DispatchNativeActionAsync(
        nullptr, frame, req, &dispatcher,
        base::BindOnce(
            [](NativeActionResult* out, bool* done, NativeActionResult r) {
              *out = std::move(r);
              *done = true;
            },
            &result, &completed));

    task_env.RunUntilIdle();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(result.success);
    // Sequence: Shift Down -> Tab Down -> Tab Up -> Shift Up
    ASSERT_EQ(dispatcher.event_count(), 4u);
    EXPECT_EQ(dispatcher.dispatched_events()[0].key_code, ui::VKEY_SHIFT);
    EXPECT_EQ(dispatcher.dispatched_events()[0].type, NativeEvent::Type::kKeyDown);
    EXPECT_EQ(dispatcher.dispatched_events()[1].key_code, ui::VKEY_TAB);
    EXPECT_EQ(dispatcher.dispatched_events()[1].type, NativeEvent::Type::kKeyDown);
    EXPECT_EQ(dispatcher.dispatched_events()[2].key_code, ui::VKEY_TAB);
    EXPECT_EQ(dispatcher.dispatched_events()[2].type, NativeEvent::Type::kKeyUp);
    EXPECT_EQ(dispatcher.dispatched_events()[3].key_code, ui::VKEY_SHIFT);
    EXPECT_EQ(dispatcher.dispatched_events()[3].type, NativeEvent::Type::kKeyUp);
  }
}

TEST(MahoMcpInputSynthesizerTest,
     HybridNative_Keyboard_ModifierSafety_NeverConsumeUserModifiers) {
  base::test::TaskEnvironment task_env;
  VisualFrame frame = CreateValidVisualFrameForTesting();
  NativeActionRequest req;
  req.type = NativeActionRequest::ActionType::kKey;
  req.frame_token = frame.token;
  req.tab_id = 1;
  req.lease_epoch = frame.lease_epoch;
  req.document_epoch = frame.document_epoch;
  req.key = "Enter";

  TestNativeInputDispatcher dispatcher;
  EXPECT_EQ(dispatcher.event_count(), 0u);

  // Unknown user is holding a physical modifier key (e.g. Alt)
  NativeSecurityContext context;
  context.tab_id = 1;
  context.expected_lease_epoch = 1;
  context.user_held_modifiers = true;

  NativeActionResult result;
  bool completed = false;

  MahoMcpInputSynthesizer::DispatchNativeActionAsync(
      nullptr, frame, req, &dispatcher, context,
      base::BindOnce(
          [](NativeActionResult* out, bool* done, NativeActionResult r) {
            *out = std::move(r);
            *done = true;
          },
          &result, &completed));

  task_env.RunUntilIdle();
  EXPECT_TRUE(completed);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, -32011);
  EXPECT_NE(result.error_message.find("User-held modifiers"), std::string::npos);
  // Aborted before any press path: 0 events observed!
  EXPECT_EQ(dispatcher.event_count(), 0u);
}

TEST(MahoMcpInputSynthesizerTest, HybridNative_WindowsKeyboardInput_PacketGeneration) {
  // Navigation key: ArrowDown
  {
    NativeEvent ev = NativeEvent::MakeKeyDown(ui::VKEY_DOWN);
    auto packets = BuildWindowsInputsForNativeEvent(ev);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].type, WinSimulatedInput::kKeyboard);
    EXPECT_EQ(packets[0].vk, 0x28);  // VK_DOWN
    EXPECT_EQ(packets[0].scan, 0x50);
    EXPECT_TRUE(packets[0].key_flags & 0x0001);  // KEYEVENTF_EXTENDEDKEY
  }

  // Enter key
  {
    NativeEvent ev = NativeEvent::MakeKeyDown(ui::VKEY_RETURN);
    auto packets = BuildWindowsInputsForNativeEvent(ev);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].vk, 0x0D);  // VK_RETURN
    EXPECT_EQ(packets[0].scan, 0x1C);
  }

  // Tab key
  {
    NativeEvent ev = NativeEvent::MakeKeyDown(ui::VKEY_TAB);
    auto packets = BuildWindowsInputsForNativeEvent(ev);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].vk, 0x09);  // VK_TAB
    EXPECT_EQ(packets[0].scan, 0x0F);
  }

  // Surrogate pair: U+1F680 (rocket emoji) -> 0xD83D, 0xDE80
  {
    NativeEvent ev = NativeEvent::MakeKeyChar(0x1F680u, u"🚀");
    auto packets = BuildWindowsInputsForNativeEvent(ev);
    // High surrogate down/up + Low surrogate down/up = 4 packets
    ASSERT_EQ(packets.size(), 4u);
    EXPECT_EQ(packets[0].unicode_char, 0xD83Du);
    EXPECT_TRUE(packets[0].key_flags & 0x0004);  // KEYEVENTF_UNICODE
    EXPECT_FALSE(packets[0].key_flags & 0x0002); // Down

    EXPECT_EQ(packets[1].unicode_char, 0xD83Du);
    EXPECT_TRUE(packets[1].key_flags & 0x0002);  // Up

    EXPECT_EQ(packets[2].unicode_char, 0xDE80u);
    EXPECT_FALSE(packets[2].key_flags & 0x0002); // Down

    EXPECT_EQ(packets[3].unicode_char, 0xDE80u);
    EXPECT_TRUE(packets[3].key_flags & 0x0002);  // Up
  }
}

#if BUILDFLAG(IS_MAC)
TEST(MahoMcpInputSynthesizerTest, HybridNative_MacKeyboardInput_PlatformMapping) {
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_RETURN), 0x24);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_TAB), 0x30);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_SPACE), 0x31);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_ESCAPE), 0x35);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_UP), 0x7E);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_DOWN), 0x7D);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_LEFT), 0x7B);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_RIGHT), 0x7C);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_SHIFT), 0x38);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_CONTROL), 0x3B);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_MENU), 0x3A);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_COMMAND), 0x37);
  EXPECT_EQ(MacKeyCodeForKeyboardCode(ui::VKEY_A), 0x00);
}
#endif

}  // namespace maho

#else

// Standalone runner for testing outside full Chromium build tree
int main() {
  using namespace maho;
  std::cout << "[RUN] MahoMcpInputSynthesizer & TypingPacingPolicy standalone tests..." << std::endl;

  // Test 1: Seeded RNG determinism
  {
    const uint64_t kTestSeed = 0x123456789ABCDEF0ULL;
    TypingPacingPolicy policy;
    policy.base_range_ms = {10, 30};
    policy.punctuation_range_ms = {120, 240};
    policy.space_range_ms = {40, 80};
    policy.rng_seed = kTestSeed;

    const std::string text = "Hello, World! How are you doing today? 123.";

    auto generate_delays = [&](const TypingPacingPolicy& pol) {
      std::vector<int32_t> delays;
      SplitMix64Prng rng = pol.CreatePrng();
      for (size_t i = 0; i < text.size(); ++i) {
        char prev_c = (i == 0) ? '\0' : text[i - 1];
        char next_c = text[i];
        delays.push_back(pol.delay_for(prev_c, next_c, rng));
      }
      return delays;
    };

    std::vector<int32_t> run1 = generate_delays(policy);
    std::vector<int32_t> run2 = generate_delays(policy);
    assert(run1.size() == text.size());
    assert(run1 == run2);

    TypingPacingPolicy other_policy = policy;
    other_policy.rng_seed = 0x987654321FEDCBA0ULL;
    std::vector<int32_t> run_diff = generate_delays(other_policy);
    assert(run1 != run_diff);
    std::cout << "  [PASS] SeededRngProducesDeterministicDelaySequence" << std::endl;
  }

  // Test 2: Bounds compliance
  {
    TypingPacingPolicy policy;
    policy.base_range_ms = {0, 60};
    policy.punctuation_range_ms = {120, 240};
    policy.space_range_ms = {40, 80};
    policy.rng_seed = 42ULL;

    SplitMix64Prng rng = policy.CreatePrng();
    for (int i = 0; i < 200; ++i) {
      int32_t d = policy.delay_for('a', 'b', rng);
      assert(d >= 0 && d <= 60);
    }
    const std::vector<char> punctuations = {'.', ',', '!', '?', ';', ':', '-', '\n'};
    for (char p : punctuations) {
      for (int i = 0; i < 50; ++i) {
        int32_t d = policy.delay_for(p, 'a', rng);
        assert(d >= 120 && d <= 240);
      }
    }
    for (int i = 0; i < 200; ++i) {
      int32_t d1 = policy.delay_for('a', ' ', rng);
      assert(d1 >= 40 && d1 <= 80);
      int32_t d2 = policy.delay_for(' ', 'w', rng);
      assert(d2 >= 40 && d2 <= 80);
    }
    std::cout << "  [PASS] AllDelaysWithinDeclaredBounds" << std::endl;
  }

  // Test 3: Out of bounds config validation
  {
    TypingPacingPolicy valid_default;
    assert(valid_default.validate());

    TypingPacingPolicy inverted = valid_default;
    inverted.base_range_ms = {70, 60};
    assert(!inverted.validate());

    TypingPacingPolicy negative = valid_default;
    negative.base_range_ms = {-1, 60};
    assert(!negative.validate());

    negative = valid_default;
    negative.punctuation_range_ms = {120, -10};
    assert(!negative.validate());

    negative = valid_default;
    negative.space_range_ms = {-50, -10};
    assert(!negative.validate());

    TypingPacingPolicy over_cap = valid_default;
    over_cap.base_range_ms = {0, 5001};
    assert(!over_cap.validate());

    over_cap = valid_default;
    over_cap.punctuation_range_ms = {100, 6000};
    assert(!over_cap.validate());

    over_cap = valid_default;
    over_cap.space_range_ms = {0, 10000};
    assert(!over_cap.validate());

    std::cout << "  [PASS] OutOfBoundsConfigRejectedByValidation" << std::endl;
  }

  // Test 4: KeyStringToKeyCode mappings
  {
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("Enter") == ui::VKEY_RETURN);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("Tab") == ui::VKEY_TAB);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("Escape") == ui::VKEY_ESCAPE);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("Backspace") == ui::VKEY_BACK);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("Delete") == ui::VKEY_DELETE);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowUp") == ui::VKEY_UP);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowDown") == ui::VKEY_DOWN);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowLeft") == ui::VKEY_LEFT);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("ArrowRight") == ui::VKEY_RIGHT);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("Home") == ui::VKEY_HOME);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("End") == ui::VKEY_END);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("PageUp") == ui::VKEY_PRIOR);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("PageDown") == ui::VKEY_NEXT);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode(" ") == ui::VKEY_SPACE);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("Space") == ui::VKEY_SPACE);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("a") == ui::VKEY_A);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("Z") == ui::VKEY_Z);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("0") == ui::VKEY_0);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("9") == ui::VKEY_9);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("F1") == ui::VKEY_F1);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("F12") == ui::VKEY_F12);
    assert(MahoMcpInputSynthesizer::KeyStringToKeyCode("UnknownKey") == ui::VKEY_UNKNOWN);
    std::cout << "  [PASS] KeyStringToKeyCodeMappings" << std::endl;
  }

  // Direct macOS forwarding must carry SelectAll on Cmd+A, not a character.
  {
    const int meta = MahoMcpInputSynthesizer::ModifierListToWebModifiers({"meta"});
    assert(MahoMcpInputSynthesizer::MacEditCommandForKeyPress(
               MahoMcpInputSynthesizer::KeyStringToKeyCode("a"), meta) ==
           "SelectAll");
    assert(MahoMcpInputSynthesizer::MacEditCommandForKeyPress(
               MahoMcpInputSynthesizer::KeyStringToKeyCode("A"), meta) ==
           "SelectAll");
    for (int modifiers : {0, meta | blink::WebInputEvent::kShiftKey,
                         meta | blink::WebInputEvent::kControlKey,
                         meta | blink::WebInputEvent::kAltKey}) {
      assert(MahoMcpInputSynthesizer::MacEditCommandForKeyPress(
                 ui::VKEY_A, modifiers).empty());
    }
    for (auto key : {ui::VKEY_BACK, ui::VKEY_DELETE, ui::VKEY_Z}) {
      assert(MahoMcpInputSynthesizer::MacEditCommandForKeyPress(
                 key, meta).empty());
    }
    std::cout << "  [PASS] MacSelectAllCommandTranslation" << std::endl;
  }

  // Test 5: ModifierListToWebModifiers
  {
    assert(MahoMcpInputSynthesizer::ModifierListToWebModifiers({}) ==
           blink::WebInputEvent::kNoModifiers);
    int shift_res = MahoMcpInputSynthesizer::ModifierListToWebModifiers({"shift"});
    assert(shift_res & blink::WebInputEvent::kShiftKey);
    assert(!(shift_res & blink::WebInputEvent::kControlKey));

    int multi_res = MahoMcpInputSynthesizer::ModifierListToWebModifiers(
        {"shift", "ctrl", "alt", "meta"});
    assert(multi_res & blink::WebInputEvent::kShiftKey);
    assert(multi_res & blink::WebInputEvent::kControlKey);
    assert(multi_res & blink::WebInputEvent::kAltKey);
    assert(multi_res & blink::WebInputEvent::kMetaKey);
    std::cout << "  [PASS] ModifierListToWebModifiers" << std::endl;
  }

  // Test 6: Utf8ToTypingUnits with Korean text
  {
    const std::string text = "윤인도";
    std::vector<TypingKeyUnit> units =
        MahoMcpInputSynthesizer::Utf8ToTypingUnits(text);
    assert(units.size() == 3);
    assert(units[0].code_point == 0xC724u);
    assert(units[0].utf8 == "윤");
    assert(units[0].utf16 == u"윤");
    assert(units[1].code_point == 0xC778u);
    assert(units[1].utf8 == "인");
    assert(units[1].utf16 == u"인");
    assert(units[2].code_point == 0xB3C4u);
    assert(units[2].utf8 == "도");
    assert(units[2].utf16 == u"도");
    std::cout << "  [PASS] Utf8ToTypingUnits_Korean" << std::endl;
  }

  // Test 7: Utf8ToTypingUnits mixed ASCII, Korean, numbers
  {
    const std::string text = "Maho 마호 123";
    std::vector<TypingKeyUnit> units =
        MahoMcpInputSynthesizer::Utf8ToTypingUnits(text);
    assert(units.size() == 11);
    assert(units[0].code_point == 'M');
    assert(units[0].utf8 == "M");
    assert(units[1].code_point == 'a');
    assert(units[2].code_point == 'h');
    assert(units[3].code_point == 'o');
    assert(units[4].code_point == ' ');
    assert(units[5].code_point == 0xB9C8u);
    assert(units[5].utf8 == "마");
    assert(units[6].code_point == 0xD638u);
    assert(units[6].utf8 == "호");
    assert(units[7].code_point == ' ');
    assert(units[8].code_point == '1');
    assert(units[9].code_point == '2');
    assert(units[10].code_point == '3');
    std::cout << "  [PASS] Utf8ToTypingUnits_Mixed" << std::endl;
  }

  // Test 8: Utf8ToTypingUnits emoji surrogate pairs
  {
    const std::string text = "👋🚀";
    std::vector<TypingKeyUnit> units =
        MahoMcpInputSynthesizer::Utf8ToTypingUnits(text);
    assert(units.size() == 2);
    assert(units[0].code_point == 0x1F44Bu);
    assert(units[0].utf16.size() == 2);
    assert(units[0].utf16[0] == 0xD83Du);
    assert(units[0].utf16[1] == 0xDC4Bu);
    assert(units[0].utf8 == "👋");

    assert(units[1].code_point == 0x1F680u);
    assert(units[1].utf16.size() == 2);
    assert(units[1].utf16[0] == 0xD83Du);
    assert(units[1].utf16[1] == 0xDE80u);
    assert(units[1].utf8 == "🚀");
    std::cout << "  [PASS] Utf8ToTypingUnits_EmojiSurrogatePair" << std::endl;
  }

  // Test 9: Korean pacing, punctuation and whitespace
  {
    TypingPacingPolicy policy;
    policy.base_range_ms = {10, 20};
    policy.punctuation_range_ms = {120, 240};
    policy.space_range_ms = {40, 80};
    policy.rng_seed = 42ULL;

    SplitMix64Prng rng = policy.CreatePrng();
    for (int i = 0; i < 50; ++i) {
      int32_t d = policy.delay_for(U'윤', U'인', rng);
      assert(d >= 10 && d <= 20);
    }
    for (int i = 0; i < 50; ++i) {
      int32_t d1 = policy.delay_for(U'.', U'윤', rng);
      assert(d1 >= 120 && d1 <= 240);

      int32_t d2 = policy.delay_for(U'!', U'도', rng);
      assert(d2 >= 120 && d2 <= 240);

      int32_t d3 = policy.delay_for(0x3002u, U'윤', rng);
      assert(d3 >= 120 && d3 <= 240);
    }
    for (int i = 0; i < 50; ++i) {
      int32_t d1 = policy.delay_for(U'윤', U' ', rng);
      assert(d1 >= 40 && d1 <= 80);
      int32_t d2 = policy.delay_for(U' ', U'인', rng);
      assert(d2 >= 40 && d2 <= 80);
    }
    std::cout << "  [PASS] PacingPolicy_KoreanPunctuationAndWhitespace" << std::endl;
  }

  // Test 10: Deterministic Korean sequence
  {
    const uint64_t kTestSeed = 0xDEADBEEFCAFEULL;
    TypingPacingPolicy policy;
    policy.base_range_ms = {10, 30};
    policy.punctuation_range_ms = {120, 240};
    policy.space_range_ms = {40, 80};
    policy.rng_seed = kTestSeed;

    const std::string text = "안녕하세요, 마호 브라우저입니다! 123.";
    std::vector<TypingKeyUnit> units =
        MahoMcpInputSynthesizer::Utf8ToTypingUnits(text);

    auto generate_delays = [&](const TypingPacingPolicy& pol) {
      std::vector<int32_t> delays;
      SplitMix64Prng rng = pol.CreatePrng();
      for (size_t i = 0; i < units.size(); ++i) {
        char32_t prev_c = (i == 0) ? 0 : units[i - 1].code_point;
        char32_t next_c = units[i].code_point;
        delays.push_back(pol.delay_for(prev_c, next_c, rng));
      }
      return delays;
    };

    std::vector<int32_t> run1 = generate_delays(policy);
    std::vector<int32_t> run2 = generate_delays(policy);
    assert(run1.size() == units.size());
    assert(run1 == run2);

    TypingPacingPolicy other_policy = policy;
    other_policy.rng_seed = 0x112233445566ULL;
    std::vector<int32_t> run_diff = generate_delays(other_policy);
    assert(run1 != run_diff);
    std::cout << "  [PASS] PacingPolicy_DeterministicSequence_Korean" << std::endl;
  }

  // Test 11: Char overloads without signed truncation issues
  {
    TypingPacingPolicy policy;
    policy.base_range_ms = {10, 30};
    policy.punctuation_range_ms = {120, 240};
    policy.space_range_ms = {40, 80};
    policy.rng_seed = 100ULL;

    SplitMix64Prng rng = policy.CreatePrng();
    int32_t d16 = policy.delay_for(u'윤', u'인', rng);
    assert(d16 >= 10 && d16 <= 30);

    char signed_c = static_cast<char>(0xEA);
    int32_t d_char = policy.delay_for(signed_c, signed_c, rng);
    assert(d_char >= 10 && d_char <= 30);
    std::cout << "  [PASS] PacingPolicy_CharOverloadNoSignedTruncation" << std::endl;
  }

  // Test 12: Utf8ToTypingUnits CRLF normalization
  {
    std::vector<TypingKeyUnit> units =
        MahoMcpInputSynthesizer::Utf8ToTypingUnits("A\r\nB\nC\rD");
    assert(units.size() == 7);
    assert(units[1].code_point == '\n');
    assert(units[3].code_point == '\n');
    assert(units[5].code_point == '\n');
    std::cout << "  [PASS] Utf8ToTypingUnits_CrlfNormalization" << std::endl;
  }

  // Test 13: BuildNativeEventsForUnit newline
  {
    TypingKeyUnit newline_unit;
    newline_unit.code_point = static_cast<char32_t>('\n');
    newline_unit.utf8 = "\n";
    newline_unit.utf16 = u"\n";

    std::vector<NativeEvent> events =
        MahoMcpInputSynthesizer::BuildNativeEventsForUnit(newline_unit);
    assert(events.size() == 2);
    assert(events[0].type == NativeEvent::Type::kKeyDown);
    assert(events[0].key_code == ui::VKEY_RETURN);
    assert(events[1].type == NativeEvent::Type::kKeyUp);
    assert(events[1].key_code == ui::VKEY_RETURN);
    std::cout << "  [PASS] BuildNativeEventsForUnit_Newline" << std::endl;
  }

  std::cout << "[PASS] All MahoMcpInputSynthesizer standalone tests passed!" << std::endl;
  return 0;
}
#endif
