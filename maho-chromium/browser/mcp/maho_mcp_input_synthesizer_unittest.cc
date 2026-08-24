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

  std::cout << "[PASS] All MahoMcpInputSynthesizer standalone tests passed!" << std::endl;
  return 0;
}
#endif
