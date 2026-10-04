// Copyright 2026 Maho Browser. All rights reserved.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "maho/browser/ui/theme/maho_space_theme_io.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "maho/browser/ai/maho_ai_llm_client.h"
#include "maho/browser/ai/maho_model_list_fetcher.h"
#include "base/test/task_environment.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "components/prefs/testing_pref_service.h"
#include "components/prefs/pref_registry_simple.h"
#include "maho/browser/ai/maho_unified_agent_adapter.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"

namespace maho_theme {
namespace {

std::string MakeSolidThemeJson() {
  base::DictValue color;
  color.Set("hue", 240.0);
  color.Set("saturation", 0.6);
  color.Set("brightness", 0.8);
  color.Set("grain", 0.3);

  base::DictValue theme;
  theme.Set("type", "solid");
  theme.Set("color", std::move(color));

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  return json;
}

std::string MakeGradientThemeJson() {
  base::ListValue colors;
  base::DictValue stop1;
  base::ListValue c1;
  c1.Append(74);
  c1.Append(144);
  c1.Append(217);
  stop1.Set("c", std::move(c1));
  stop1.Set("algorithm", "tetradic");
  stop1.Set("lightness", 0.5);
  base::DictValue p1;
  p1.Set("x", 0.0);
  p1.Set("y", 0.5);
  stop1.Set("position", std::move(p1));
  stop1.Set("type", "accent");
  stop1.Set("isPrimary", true);
  stop1.Set("isCustom", false);
  colors.Append(std::move(stop1));

  base::DictValue stop2;
  stop2.Set("c", "#ff6b35");
  stop2.Set("algorithm", "analogous");
  stop2.Set("lightness", 0.6);
  base::DictValue p2;
  p2.Set("x", 1.0);
  p2.Set("y", 0.0);
  stop2.Set("position", std::move(p2));
  stop2.Set("type", "accent");
  stop2.Set("isPrimary", false);
  stop2.Set("isCustom", true);
  colors.Append(std::move(stop2));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "floating");
  theme.Set("scheme", "auto");
  theme.Set("opacity", 0.95);
  theme.Set("texture", 0.2);

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  return json;
}

base::DictValue MakeValidGradientEntry(bool is_primary) {
  base::DictValue entry;
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);
  c_rgb.Append(217);
  entry.Set("c", std::move(c_rgb));
  entry.Set("algorithm", "tetradic");
  entry.Set("lightness", 0.5);
  base::DictValue pos;
  pos.Set("x", is_primary ? 0.0 : 1.0);
  pos.Set("y", 0.5);
  entry.Set("position", std::move(pos));
  entry.Set("type", "accent");
  entry.Set("isPrimary", is_primary);
  entry.Set("isCustom", false);
  return entry;
}

// The old malformed gradient shape used a "gradientChooser" dict instead of a
// flat "gradientColors" list. Importing such payloads must be rejected.
std::string MakeOldChooserGradientShapeJson() {
  base::DictValue chooser;
  chooser.Set("preset", "sunset");
  chooser.Set("angle", 135);

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientChooser", std::move(chooser));

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  return json;
}

std::string MakeFlatHsbGradientThemeJson() {
  base::DictValue stop1;
  stop1.Set("hue", 220.0);
  stop1.Set("saturation", 0.7);
  stop1.Set("brightness", 0.85);
  stop1.Set("isPrimary", true);
  stop1.Set("isCustom", false);

  base::DictValue stop2;
  stop2.Set("hue", 40.0);
  stop2.Set("saturation", 0.5);
  stop2.Set("brightness", 0.9);
  stop2.Set("isPrimary", false);
  stop2.Set("isCustom", false);

  base::ListValue colors;
  colors.Append(std::move(stop1));
  colors.Append(std::move(stop2));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "analogous");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  return json;
}

base::FilePath WriteToTempFile(const base::ScopedTempDir& dir,
                                 const std::string& content,
                                 const std::string& filename) {
  base::FilePath path = dir.GetPath().AppendASCII(filename);
  base::WriteFile(path, content);
  return path;
}

class MahoSpaceThemeIoValidationTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  std::optional<std::string> Validate(const std::string& json,
                                       const std::string& filename) {
    base::FilePath path = WriteToTempFile(temp_dir_, json, filename);
    return ReadValidatedThemeJson(path);
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(MahoSpaceThemeIoValidationTest, ValidSolid) {
  auto result = Validate(MakeSolidThemeJson(), "solid.json");
  ASSERT_TRUE(result.has_value()) << "Valid solid theme must be accepted";
  EXPECT_FALSE(result->empty());
}

TEST_F(MahoSpaceThemeIoValidationTest, ValidGradient) {
  auto result = Validate(MakeGradientThemeJson(), "gradient.json");
  ASSERT_TRUE(result.has_value()) << "Valid gradient theme must be accepted";
  EXPECT_FALSE(result->empty());
}

TEST_F(MahoSpaceThemeIoValidationTest, ValidFlatHsbGradient) {
  auto result = Validate(MakeFlatHsbGradientThemeJson(), "flat_hsb_gradient.json");
  ASSERT_TRUE(result.has_value())
      << "Flat HSB gradient entry (hue/saturation/brightness) must be accepted";
  EXPECT_FALSE(result->empty());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsMissingType) {
  base::DictValue theme;
  theme.Set("color", base::DictValue());
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "no_type.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsUnknownType) {
  base::DictValue theme;
  theme.Set("type", "pattern");
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "unknown_type.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsSolidMissingColorDict) {
  base::DictValue theme;
  theme.Set("type", "solid");
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "solid_no_color.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsSolidMissingHue) {
  base::DictValue color;
  color.Set("saturation", 0.5);
  color.Set("brightness", 0.8);
  color.Set("grain", 0.1);

  base::DictValue theme;
  theme.Set("type", "solid");
  theme.Set("color", std::move(color));
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "solid_no_hue.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsSolidMissingSaturation) {
  base::DictValue color;
  color.Set("hue", 180.0);
  color.Set("brightness", 0.8);
  color.Set("grain", 0.1);

  base::DictValue theme;
  theme.Set("type", "solid");
  theme.Set("color", std::move(color));
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "solid_no_saturation.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsSolidMissingBrightness) {
  base::DictValue color;
  color.Set("hue", 180.0);
  color.Set("saturation", 0.5);
  color.Set("grain", 0.1);

  base::DictValue theme;
  theme.Set("type", "solid");
  theme.Set("color", std::move(color));
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "solid_no_brightness.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsGradientMissingGradientColors) {
  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("harmony", "complementary");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "gradient_no_colors.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsGradientEmptyColorsList) {
  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", base::ListValue());
  theme.Set("harmony", "complementary");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "gradient_empty_colors.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsGradientMissingHarmony) {
  base::ListValue colors;
  colors.Append(MakeValidGradientEntry(true));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_TRUE(Validate(json, "gradient_no_harmony.json").has_value())
      << "harmony is optional for Zen-style themes";
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsGradientMissingOpacity) {
  base::ListValue colors;
  colors.Append(MakeValidGradientEntry(true));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "analogous");
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "gradient_no_opacity.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsGradientMissingTexture) {
  base::ListValue colors;
  colors.Append(MakeValidGradientEntry(true));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "analogous");
  theme.Set("opacity", 0.9);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "gradient_no_texture.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsOldChooserGradientShape) {
  auto result = Validate(MakeOldChooserGradientShapeJson(),
                         "old_chooser_gradient.json");
  EXPECT_FALSE(result.has_value())
      << "Old malformed gradientChooser shape must be rejected";
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsSolidMissingGrain) {
  base::DictValue color;
  color.Set("hue", 240.0);
  color.Set("saturation", 0.6);
  color.Set("brightness", 0.8);

  base::DictValue theme;
  theme.Set("type", "solid");
  theme.Set("color", std::move(color));
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "solid_no_grain.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsGradientEntryMissingIsPrimary) {
  base::ListValue colors;
  base::DictValue entry;
  entry.Set("hue", 200.0);
  entry.Set("saturation", 0.5);
  entry.Set("brightness", 0.9);
  entry.Set("isCustom", false);
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "analogous");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "gradient_entry_no_is_primary.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsGradientEntryMissingIsCustom) {
  base::ListValue colors;
  base::DictValue entry;
  entry.Set("hue", 200.0);
  entry.Set("saturation", 0.5);
  entry.Set("brightness", 0.9);
  entry.Set("isPrimary", true);
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "analogous");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "gradient_entry_no_is_custom.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsUnknownHarmony) {
  base::ListValue colors;
  colors.Append(MakeValidGradientEntry(true));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "rainbow");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "gradient_unknown_harmony.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsAllKnownHarmonies) {
  static constexpr const char* kHarmonies[] = {
      "single_analogous", "analogous", "complementary",
      "triadic", "split_complementary", "custom",
  };
  for (const char* h : kHarmonies) {
    base::ListValue colors;
    colors.Append(MakeValidGradientEntry(true));

    base::DictValue theme;
    theme.Set("type", "gradient");
    theme.Set("gradientColors", std::move(colors));
    theme.Set("harmony", h);
    theme.Set("opacity", 1.0);
    theme.Set("texture", 0.0);
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(theme)), &json);
    EXPECT_TRUE(Validate(json, std::string("harmony_") + h + ".json").has_value())
        << "harmony '" << h << "' must be accepted";
  }
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsCustomHarmony) {
  base::ListValue colors;
  colors.Append(MakeValidGradientEntry(true));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "custom");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_TRUE(Validate(json, "harmony_custom.json").has_value())
      << "custom harmony from create/config editors must be accepted";
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsFloatingHarmonyWithScheme) {
  static constexpr const char* kSchemes[] = {"auto", "light", "dark"};
  for (const char* s : kSchemes) {
    base::ListValue colors;
    colors.Append(MakeValidGradientEntry(true));

    base::DictValue theme;
    theme.Set("type", "gradient");
    theme.Set("gradientColors", std::move(colors));
    theme.Set("harmony", "floating");
    theme.Set("scheme", s);
    theme.Set("opacity", 1.0);
    theme.Set("texture", 0.0);
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(theme)), &json);
    EXPECT_TRUE(Validate(json, std::string("floating_") + s + ".json").has_value())
        << "floating harmony with scheme '" << s << "' must be accepted";
  }
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsFloatingHarmonyWithoutScheme) {
  base::ListValue colors;
  colors.Append(MakeValidGradientEntry(true));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "floating");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "floating_no_scheme.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsFloatingHarmonyWithUnknownScheme) {
  base::ListValue colors;
  colors.Append(MakeValidGradientEntry(true));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "floating");
  theme.Set("scheme", "neon");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "floating_unknown_scheme.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsFloatingSingleDotGradient) {
  base::ListValue colors;
  colors.Append(MakeValidGradientEntry(true));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "floating");
    theme.Set("scheme", "auto");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  auto result = Validate(json, "floating_single_dot.json");
  ASSERT_TRUE(result.has_value())
      << "1-dot gradient with floating harmony must be accepted";
  EXPECT_FALSE(result->empty());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsNonObjectTopLevel) {
  EXPECT_FALSE(Validate("[1, 2, 3]", "array.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsInvalidJson) {
  EXPECT_FALSE(Validate("{not json", "bad.json").has_value());
}

TEST(MahoSpaceThemeIoApplyTest, NullCoreReturnsFalse) {
  EXPECT_FALSE(
      ApplyThemeJsonToSpace(nullptr, "space-1", MakeSolidThemeJson()));
}

TEST(MahoSpaceThemeIoApplyTest, NullCoreWithInvalidJsonReturnsFalse) {
  EXPECT_FALSE(
      ApplyThemeJsonToSpace(nullptr, "space-1", MakeOldChooserGradientShapeJson()));
}

TEST(MahoSpaceThemeIoSerializeTest, NullCoreReturnsNullopt) {
  EXPECT_FALSE(SerializeSpaceThemeJson(nullptr, "space-1").has_value());
}

TEST(MahoSpaceThemeIoSerializeTest, NullCoreExportReturnsFalse) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath path = dir.GetPath().AppendASCII("out.json");
  EXPECT_FALSE(ExportSpaceThemeToFile(nullptr, "space-1", path));
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsZenGradientEntry) {
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);
  c_rgb.Append(217);

  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("algorithm", "tetradic");
  entry.Set("isPrimary", true);
  entry.Set("isCustom", false);
  entry.Set("lightness", 0.5);
  base::DictValue pos;
  pos.Set("x", 0.0);
  pos.Set("y", 0.5);
  entry.Set("position", std::move(pos));
  entry.Set("type", "accent");

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  auto result = Validate(json, "zen_gradient.json");
  ASSERT_TRUE(result.has_value()) << "Zen-style gradient entry must be accepted";
}

TEST_F(MahoSpaceThemeIoValidationTest, ValidGradientCanonicalizesFloatingScheme) {
  auto result = Validate(MakeGradientThemeJson(), "gradient.json");
  ASSERT_TRUE(result.has_value()) << "Valid gradient theme must be accepted";
  EXPECT_NE(result->find("\"harmony\":\"floating\""), std::string::npos);
  EXPECT_NE(result->find("\"scheme\":\"auto\""), std::string::npos);
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsZenCustomColorString) {
  base::DictValue entry;
  entry.Set("c", "#ff6b35");
  entry.Set("algorithm", "analogous");
  entry.Set("isPrimary", true);
  entry.Set("isCustom", true);
  entry.Set("lightness", 0.6);
  base::DictValue pos;
  pos.Set("x", 0.0);
  pos.Set("y", 0.0);
  entry.Set("position", std::move(pos));
  entry.Set("type", "accent");

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 0.9);
  theme.Set("texture", 0.1);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_TRUE(Validate(json, "zen_custom_string.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsZenMultiStopGradient) {
  base::ListValue colors;
  {
    base::ListValue c_rgb;
    c_rgb.Append(255);
    c_rgb.Append(107);
    c_rgb.Append(53);
    base::DictValue e;
    e.Set("c", std::move(c_rgb));
    e.Set("algorithm", "analogous");
    e.Set("isPrimary", true);
    e.Set("isCustom", false);
    e.Set("lightness", 0.6);
    base::DictValue pos;
    pos.Set("x", 0.0);
    pos.Set("y", 0.0);
    e.Set("position", std::move(pos));
    e.Set("type", "accent");
    colors.Append(std::move(e));
  }
  {
    base::ListValue c_rgb;
    c_rgb.Append(0);
    c_rgb.Append(78);
    c_rgb.Append(137);
    base::DictValue e;
    e.Set("c", std::move(c_rgb));
    e.Set("algorithm", "analogous");
    e.Set("isPrimary", false);
    e.Set("isCustom", false);
    e.Set("lightness", 0.4);
    base::DictValue pos;
    pos.Set("x", 1.0);
    pos.Set("y", 1.0);
    e.Set("position", std::move(pos));
    e.Set("type", "accent");
    colors.Append(std::move(e));
  }

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.2);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_TRUE(Validate(json, "zen_multi_stop.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsZenEntryMissingC) {
  base::DictValue entry;
  entry.Set("algorithm", "tetradic");
  entry.Set("isPrimary", true);
  entry.Set("isCustom", false);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "zen_no_c.json").has_value())
      << "Entry with neither HSB nor c must be rejected";
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsZenEntryMissingIsPrimary) {
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);
  c_rgb.Append(217);

  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("isCustom", false);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "zen_no_is_primary.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsZenEntryMissingIsCustom) {
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);
  c_rgb.Append(217);

  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("isPrimary", true);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "zen_no_is_custom.json").has_value());
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsZenEntryInvalidCWrongLength) {
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);

  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("isPrimary", true);
  entry.Set("isCustom", false);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "zen_c_wrong_length.json").has_value())
      << "c array with != 3 elements must be rejected";
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsZenEntryInvalidCNonNumeric) {
  base::ListValue c_rgb;
  c_rgb.Append("red");
  c_rgb.Append("green");
  c_rgb.Append("blue");

  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("isPrimary", true);
  entry.Set("isCustom", false);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "zen_c_non_numeric.json").has_value())
      << "c array with non-numeric values must be rejected";
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsGradientWithInvalidHarmony) {
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);
  c_rgb.Append(217);

  base::ListValue colors;
  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("isPrimary", true);
  entry.Set("isCustom", false);
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "gradient");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "invalid_harmony");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "zen_invalid_harmony.json").has_value());
}

std::string MakeZenThemeJson() {
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);
  c_rgb.Append(217);

  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("isPrimary", true);
  entry.Set("isCustom", false);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "zen");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.2);

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  return json;
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsZenType) {
  auto result = Validate(MakeZenThemeJson(), "zen_type.json");
  ASSERT_TRUE(result.has_value()) << "zen type must be accepted";
  EXPECT_FALSE(result->empty());
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsZenTypeWithHarmony) {
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);
  c_rgb.Append(217);

  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("isPrimary", true);
  entry.Set("isCustom", false);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "zen");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "analogous");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_TRUE(Validate(json, "zen_with_harmony.json").has_value())
      << "zen type with valid harmony must be accepted";
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsZenTypeWithoutHarmony) {
  auto result = Validate(MakeZenThemeJson(), "zen_no_harmony.json");
  ASSERT_TRUE(result.has_value()) << "zen type without harmony must be accepted";
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsZenTypeMissingGradientColors) {
  base::DictValue theme;
  theme.Set("type", "zen");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "zen_no_colors.json").has_value())
      << "zen type without gradientColors must be rejected";
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsZenTypeMissingOpacity) {
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);
  c_rgb.Append(217);

  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("isPrimary", true);
  entry.Set("isCustom", false);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "zen");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "zen_no_opacity.json").has_value())
      << "zen type without opacity must be rejected";
}

TEST_F(MahoSpaceThemeIoValidationTest, AcceptsZenTypeWithHexColorString) {
  base::DictValue entry;
  entry.Set("c", "#ff6b35");
  entry.Set("isPrimary", true);
  entry.Set("isCustom", true);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "zen");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 0.9);
  theme.Set("texture", 0.1);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_TRUE(Validate(json, "zen_hex_color.json").has_value())
      << "zen type with hex c-string must be accepted";
}

TEST_F(MahoSpaceThemeIoValidationTest, SchemaVersionFieldIgnoredDuringValidation) {
  base::DictValue color;
  color.Set("hue", 240.0);
  color.Set("saturation", 0.6);
  color.Set("brightness", 0.8);
  color.Set("grain", 0.3);

  base::DictValue theme;
  theme.Set("type", "solid");
  theme.Set("color", std::move(color));
  theme.Set("schemaVersion", 1);

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_TRUE(Validate(json, "solid_with_schema_version.json").has_value())
      << "schemaVersion field must not break validation";
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsZenTypeMissingTexture) {
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);
  c_rgb.Append(217);

  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("isPrimary", true);
  entry.Set("isCustom", false);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "zen");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("opacity", 1.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "zen_no_texture.json").has_value())
      << "zen type without texture must be rejected";
}

TEST_F(MahoSpaceThemeIoValidationTest, RejectsZenTypeWithInvalidHarmony) {
  base::ListValue c_rgb;
  c_rgb.Append(74);
  c_rgb.Append(144);
  c_rgb.Append(217);

  base::DictValue entry;
  entry.Set("c", std::move(c_rgb));
  entry.Set("isPrimary", true);
  entry.Set("isCustom", false);

  base::ListValue colors;
  colors.Append(std::move(entry));

  base::DictValue theme;
  theme.Set("type", "zen");
  theme.Set("gradientColors", std::move(colors));
  theme.Set("harmony", "bad_value");
  theme.Set("opacity", 1.0);
  theme.Set("texture", 0.0);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(theme)), &json);
  EXPECT_FALSE(Validate(json, "zen_invalid_harmony.json").has_value())
      << "zen type with unknown harmony value must be rejected";
}

class MahoSpaceThemeStateTest : public testing::Test {
 protected:
  Browser* CreateBrowserForProfile(Profile* profile) {
    BrowserWindowCreateParams params(profile, /*user_gesture=*/true);
    params.window = new TestBrowserWindow();
    std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();
    browsers_.push_back(std::move(owned_browser));
    return browser;
  }

  bool DestroyBrowser(Browser* browser) {
    auto it = std::find_if(
        browsers_.begin(), browsers_.end(),
        [browser](const std::unique_ptr<Browser>& candidate) {
          return candidate.get() == browser;
        });
    if (it == browsers_.end()) {
      return false;
    }
    NotifyBrowserClosed(browser);
    browsers_.erase(it);
    task_environment_.RunUntilIdle();
    return true;
  }

  // Test browsers are destroyed directly, bypassing BrowserManagerService,
  // so deliver the close notification the tab registry would otherwise get.
  static void NotifyBrowserClosed(Browser* browser) {
    maho::MahoTabRegistry::Get()->OnBrowserClosed(browser);
  }

  void SetUp() override {
    MahoSpaceThemeState::Clear();
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    bridge->ClearBrowserActiveSpacesForTesting();
    bridge->EnsureBrowserCollectionObservationForTesting();
    bridge->SetActiveSpaceId("");
    profile_ = TestingProfile::Builder().Build();
    // The bridge only maps a browser to a space registered to its profile.
    bridge->RegisterSpace("space-a", profile_->GetPath().BaseName());
    bridge->RegisterSpace("space-b", profile_->GetPath().BaseName());
    browser_a_ = CreateBrowserForProfile(profile_.get());
    browser_b_ = CreateBrowserForProfile(profile_.get());
  }

  void TearDown() override {
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    // Reset the bridge while the browsers are alive: SetActiveSpaceId()
    // re-announces tabs by walking every observed browser's tab strip.
    bridge->ClearBrowserActiveSpacesForTesting();
    bridge->UnregisterSpace("space-a");
    bridge->UnregisterSpace("space-b");
    bridge->SetActiveSpaceId("");
    for (const auto& browser : browsers_) {
      NotifyBrowserClosed(browser.get());
    }
    browsers_.clear();
    task_environment_.RunUntilIdle();
    browser_a_ = nullptr;
    browser_b_ = nullptr;
    MahoSpaceThemeState::Clear();
    bridge->ResetBrowserCollectionObservationForTesting();
  }

  TestingProfile* CreateAdditionalProfile() {
    additional_profiles_.push_back(TestingProfile::Builder().Build());
    return additional_profiles_.back().get();
  }

  static std::string TwoSpaceSnapshot(double hue_a, double hue_b) {
    return base::StringPrintf(
        R"([{"id":"space-a","theme":{"type":"solid","color":{"hue":%.1f,"saturation":0.6,"brightness":0.8,"grain":0.3}},"color":{"hue":%.1f,"saturation":0.6,"brightness":0.8}},)"
        R"({"id":"space-b","theme":{"type":"gradient","gradientColors":[{"hue":%.1f,"saturation":0.7,"brightness":0.9,"isPrimary":true,"isCustom":false}],"opacity":0.75,"texture":0.2},"color":{"hue":%.1f,"saturation":0.7,"brightness":0.9}}])",
        hue_a, hue_a, hue_b, hue_b);
  }

  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<TestingProfile> profile_;
  std::vector<std::unique_ptr<TestingProfile>> additional_profiles_;
  std::vector<std::unique_ptr<Browser>> browsers_;
  raw_ptr<Browser> browser_a_ = nullptr;
  raw_ptr<Browser> browser_b_ = nullptr;
};

TEST_F(MahoSpaceThemeStateTest,
       BrowserRegistrationPublishesInitialActiveSpace) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  bridge->SetActiveSpaceId("space-a");
  bridge->ClearBrowserActiveSpacesForTesting();

  class Observer : public maho::MahoSpaceProfileBridge::Observer {
   public:
    void OnSpaceProfileBridgeChanged() override { ++changes; }
    int changes = 0;
  } observer;
  bridge->AddObserver(&observer);
  bridge->OnBrowserCreatedForTesting(browser_a_);
  EXPECT_EQ(bridge->GetActiveSpaceId(browser_a_), "space-a");
  EXPECT_EQ(observer.changes, 1);

  bridge->OnBrowserCreatedForTesting(browser_a_);
  EXPECT_EQ(observer.changes, 1);
  bridge->RemoveObserver(&observer);
}

TEST_F(MahoSpaceThemeStateTest,
       ResolvesCommittedThemesByOwningBrowserActiveSpace) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  bridge->SetActiveSpaceId(browser_a_, "space-a");
  bridge->SetActiveSpaceId(browser_b_, "space-b");

  const std::vector<std::string> changed =
      MahoSpaceThemeState::UpdateFromSnapshotForTesting(
          TwoSpaceSnapshot(30.0, 210.0));

  EXPECT_EQ(changed, (std::vector<std::string>{"space-a", "space-b"}));
  const std::optional<MahoSpaceThemeState::ThemeData> theme_a =
      MahoSpaceThemeState::GetThemeDataForBrowser(browser_a_);
  const std::optional<MahoSpaceThemeState::ThemeData> theme_b =
      MahoSpaceThemeState::GetThemeDataForBrowser(browser_b_);
  ASSERT_TRUE(theme_a);
  ASSERT_TRUE(theme_b);
  EXPECT_FLOAT_EQ(theme_a->primary_hsv.hue, 30.0f);
  EXPECT_FLOAT_EQ(theme_a->grain, 0.3f);
  EXPECT_FLOAT_EQ(theme_b->primary_hsv.hue, 210.0f);
  EXPECT_FLOAT_EQ(theme_b->opacity, 0.75f);
  EXPECT_FLOAT_EQ(theme_b->texture, 0.2f);
}

TEST_F(MahoSpaceThemeStateTest,
       UnmappedRegularBrowserDoesNotInheritGlobalCommittedTheme) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  Browser* other_profile_browser =
      CreateBrowserForProfile(CreateAdditionalProfile());
  // No profile owns the spaces here, so no browser may be mapped to them.
  bridge->ClearBrowserActiveSpacesForTesting();
  bridge->UnregisterSpace("space-a");
  bridge->UnregisterSpace("space-b");
  bridge->SetActiveSpaceId("space-a");
  MahoSpaceThemeState::UpdateFromSnapshotForTesting(
      TwoSpaceSnapshot(30.0, 210.0));

  EXPECT_TRUE(bridge->GetActiveSpaceId(browser_a_).empty());
  EXPECT_TRUE(bridge->GetActiveSpaceId(other_profile_browser).empty());
  EXPECT_FALSE(MahoSpaceThemeState::GetThemeDataForBrowser(browser_a_));
  EXPECT_FALSE(
      MahoSpaceThemeState::GetThemeDataForBrowser(other_profile_browser));
  EXPECT_TRUE(bridge->GetBrowsersForSpaces({"space-a"}).empty());
}

TEST_F(MahoSpaceThemeStateTest, PreviewIsIsolatedAndInvalidJsonPreservesOwner) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  bridge->SetActiveSpaceId("space-a");
  bridge->SetActiveSpaceId(browser_a_, "space-a");
  bridge->SetActiveSpaceId(browser_b_, "space-b");
  MahoSpaceThemeState::UpdateFromSnapshotForTesting(
      TwoSpaceSnapshot(30.0, 210.0));

  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":120.0,"saturation":0.9,"brightness":0.7,"grain":0.4}})"));
  EXPECT_TRUE(MahoSpaceThemeState::HasPreviewOverride(browser_a_));
  EXPECT_FALSE(MahoSpaceThemeState::HasPreviewOverride(browser_b_));
  ASSERT_FALSE(MahoSpaceThemeState::SetPreviewOverride(browser_a_, "{bad"));

  const auto theme_a = MahoSpaceThemeState::GetThemeDataForBrowser(browser_a_);
  const auto theme_b = MahoSpaceThemeState::GetThemeDataForBrowser(browser_b_);
  ASSERT_TRUE(theme_a);
  ASSERT_TRUE(theme_b);
  EXPECT_FLOAT_EQ(theme_a->primary_hsv.hue, 120.0f);
  EXPECT_FLOAT_EQ(theme_a->grain, 0.4f);
  EXPECT_FLOAT_EQ(theme_b->primary_hsv.hue, 210.0f);
  ASSERT_TRUE(MahoSpaceThemeState::GetThemeData());
  EXPECT_FLOAT_EQ(MahoSpaceThemeState::GetThemeData()->primary_hsv.hue, 30.0f);
}

TEST_F(MahoSpaceThemeStateTest,
       ClearAndBridgeCleanupRemoveOnlyOwnedPreviewAndMapping) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  bridge->SetActiveSpaceId(browser_a_, "space-a");
  bridge->SetActiveSpaceId(browser_b_, "space-b");
  MahoSpaceThemeState::UpdateFromSnapshotForTesting(
      TwoSpaceSnapshot(30.0, 210.0));
  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":120.0,"saturation":0.9,"brightness":0.7,"grain":0.4}})"));
  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_b_,
      R"({"type":"solid","color":{"hue":280.0,"saturation":0.5,"brightness":0.6,"grain":0.1}})"));

  MahoSpaceThemeState::ClearPreviewOverride(browser_a_);
  EXPECT_FALSE(MahoSpaceThemeState::HasPreviewOverride(browser_a_));
  EXPECT_TRUE(MahoSpaceThemeState::HasPreviewOverride(browser_b_));
  ASSERT_TRUE(MahoSpaceThemeState::GetThemeDataForBrowser(browser_a_));
  ASSERT_TRUE(MahoSpaceThemeState::GetThemeDataForBrowser(browser_b_));
  EXPECT_FLOAT_EQ(
      MahoSpaceThemeState::GetThemeDataForBrowser(browser_a_)->primary_hsv.hue,
      30.0f);
  EXPECT_FLOAT_EQ(
      MahoSpaceThemeState::GetThemeDataForBrowser(browser_b_)->primary_hsv.hue,
      280.0f);

  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":120.0,"saturation":0.9,"brightness":0.7,"grain":0.4}})"));
  bridge->ClearBrowserActiveSpace(browser_a_);
  EXPECT_FALSE(MahoSpaceThemeState::HasPreviewOverride(browser_a_));
  EXPECT_TRUE(MahoSpaceThemeState::HasPreviewOverride(browser_b_));
  EXPECT_TRUE(bridge->GetActiveSpaceId(browser_a_).empty());
  EXPECT_FALSE(MahoSpaceThemeState::GetThemeDataForBrowser(browser_a_));
  ASSERT_TRUE(MahoSpaceThemeState::GetThemeDataForBrowser(browser_b_));
  EXPECT_FLOAT_EQ(
      MahoSpaceThemeState::GetThemeDataForBrowser(browser_b_)->primary_hsv.hue,
      280.0f);
}

TEST_F(MahoSpaceThemeStateTest,
       BrowserCollectionCloseRemovesOnlyClosedBrowserState) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  bridge->SetActiveSpaceId(browser_a_, "space-a");
  bridge->SetActiveSpaceId(browser_b_, "space-b");
  MahoSpaceThemeState::UpdateFromSnapshotForTesting(
      TwoSpaceSnapshot(30.0, 210.0));
  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":120.0,"saturation":0.9,"brightness":0.7,"grain":0.4}})"));
  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_b_,
      R"({"type":"solid","color":{"hue":280.0,"saturation":0.5,"brightness":0.6,"grain":0.1}})"));
  ASSERT_EQ(MahoSpaceThemeState::GetPreviewOverrideCountForTesting(), 2u);

  const size_t browser_count_before =
      GlobalBrowserCollection::GetInstance()->GetSize();
  Browser* closed_browser = browser_a_;
  browser_a_ = nullptr;
  ASSERT_TRUE(DestroyBrowser(closed_browser));

  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(),
            browser_count_before - 1);
  EXPECT_EQ(MahoSpaceThemeState::GetPreviewOverrideCountForTesting(), 1u);
  EXPECT_TRUE(bridge->GetBrowsersForSpaces({"space-a"}).empty());
  EXPECT_EQ(bridge->GetBrowsersForSpaces({"space-b"}),
            (std::vector<Browser*>{browser_b_}));
  EXPECT_TRUE(MahoSpaceThemeState::HasPreviewOverride(browser_b_));
  ASSERT_TRUE(MahoSpaceThemeState::GetThemeDataForBrowser(browser_b_));
  EXPECT_FLOAT_EQ(
      MahoSpaceThemeState::GetThemeDataForBrowser(browser_b_)->primary_hsv.hue,
      280.0f);
}

TEST_F(MahoSpaceThemeStateTest,
       SharedSpaceUpdateIdentifiesEveryDisplayingBrowser) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  bridge->SetActiveSpaceId(browser_a_, "space-a");
  bridge->SetActiveSpaceId(browser_b_, "space-a");
  MahoSpaceThemeState::UpdateFromSnapshotForTesting(
      TwoSpaceSnapshot(30.0, 210.0));

  const std::vector<std::string> changed =
      MahoSpaceThemeState::UpdateFromSnapshotForTesting(
          TwoSpaceSnapshot(60.0, 210.0));
  std::vector<Browser*> affected = bridge->GetBrowsersForSpaces(changed);

  EXPECT_EQ(changed, (std::vector<std::string>{"space-a"}));
  EXPECT_EQ(affected.size(), 2u);
  EXPECT_NE(std::find(affected.begin(), affected.end(), browser_a_),
            affected.end());
  EXPECT_NE(std::find(affected.begin(), affected.end(), browser_b_),
            affected.end());
  ASSERT_TRUE(MahoSpaceThemeState::GetThemeDataForBrowser(browser_a_));
  ASSERT_TRUE(MahoSpaceThemeState::GetThemeDataForBrowser(browser_b_));
  EXPECT_FLOAT_EQ(
      MahoSpaceThemeState::GetThemeDataForBrowser(browser_a_)->primary_hsv.hue,
      60.0f);
  EXPECT_FLOAT_EQ(
      MahoSpaceThemeState::GetThemeDataForBrowser(browser_b_)->primary_hsv.hue,
      60.0f);
}

TEST_F(MahoSpaceThemeStateTest, OtrAndNoBrowserNeverInheritRegularPreview) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  bridge->SetActiveSpaceId(browser_a_, "space-a");
  MahoSpaceThemeState::UpdateFromSnapshotForTesting(
      TwoSpaceSnapshot(30.0, 210.0));
  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":120.0,"saturation":0.9,"brightness":0.7,"grain":0.4}})"));

  TestingProfile* otr_profile =
      TestingProfile::Builder().BuildIncognito(profile_.get());
  Browser* otr_browser = CreateBrowserForProfile(otr_profile);
  bridge->SetActiveSpaceId(otr_browser, "space-a");

  EXPECT_FALSE(MahoSpaceThemeState::GetThemeDataForBrowser(nullptr));
  EXPECT_FALSE(MahoSpaceThemeState::GetThemeDataForBrowser(otr_browser));
  EXPECT_FALSE(MahoSpaceThemeState::HasPreviewOverride(nullptr));
  EXPECT_FALSE(MahoSpaceThemeState::HasPreviewOverride(otr_browser));
  EXPECT_FALSE(MahoSpaceThemeState::SetPreviewOverride(
      otr_browser,
      R"({"type":"solid","color":{"hue":280.0,"saturation":0.5,"brightness":0.6,"grain":0.1}})"));
  const std::vector<Browser*> affected =
      bridge->GetBrowsersForSpaces({"space-a"});
  EXPECT_EQ(affected, (std::vector<Browser*>{browser_a_}));
}

}  // namespace
}  // namespace maho_theme

class MahoModelListFetcherTestHelper {
 public:
  static std::vector<std::string> GetHardcodedFallback(const maho::ai::MahoModelListFetcher& fetcher, const std::string& provider_id) {
    return fetcher.GetHardcodedFallback(provider_id);
  }
};

class MahoAiLlmClientTestHelper {
 public:
  static void SetIsRetry(MahoAiLlmClient* client, bool is_retry) {
    client->is_retry_ = is_retry;
  }
  static bool GetIsRetry(const MahoAiLlmClient* client) {
    return client->is_retry_;
  }
  static void SetPendingRequest(MahoAiLlmClient* client, MahoAiLlmClient::CompletionRequest req) {
    client->pending_request_ = MahoAiLlmClient::PendingRequest(
        std::move(req),
        MahoAiLlmClient::TokenCallback(),
        MahoAiLlmClient::CompleteCallback(),
        MahoAiLlmClient::ErrorCallback());
  }
  static bool HasPendingRequest(const MahoAiLlmClient* client) {
    return client->pending_request_.has_value();
  }
};

TEST(MahoModelListFetcherTest, GetHardcodedFallback) {
  base::test::TaskEnvironment task_environment;
  maho::ai::MahoModelListFetcher fetcher(nullptr, nullptr);
  std::vector<std::string> anthropic_models =
      MahoModelListFetcherTestHelper::GetHardcodedFallback(fetcher, "anthropic");
  ASSERT_EQ(anthropic_models.size(), 3u);
  EXPECT_EQ(anthropic_models[0], "claude-sonnet-4-20250514");
  EXPECT_EQ(anthropic_models[1], "claude-opus-4-20250514");
  EXPECT_EQ(anthropic_models[2], "claude-3-5-haiku-20241022");
}

TEST(MahoAiLlmClientTest, ResetClearsRetryAndPendingRequest) {
  base::test::TaskEnvironment task_environment;
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterStringPref("maho.ai.provider", "maho-managed");
  MahoAiLlmClient client(&prefs, nullptr, nullptr, false);
  MahoAiLlmClientTestHelper::SetIsRetry(&client, true);
  
  MahoAiLlmClient::CompletionRequest req;
  req.endpoint = "http://localhost:8000";
  req.model = "test-model";
  MahoAiLlmClientTestHelper::SetPendingRequest(&client, std::move(req));

  EXPECT_TRUE(MahoAiLlmClientTestHelper::GetIsRetry(&client));
  EXPECT_TRUE(MahoAiLlmClientTestHelper::HasPendingRequest(&client));

  client.Reset();

  EXPECT_FALSE(MahoAiLlmClientTestHelper::GetIsRetry(&client));
  EXPECT_FALSE(MahoAiLlmClientTestHelper::HasPendingRequest(&client));
}

class MahoUnifiedAgentAdapterTestHelper {
 public:
  static MahoAgentPermissionDecision EvaluatePermissionSync(
      MahoUnifiedAgentAdapter* adapter,
      const char* tool_name,
      const char* arguments) {
    std::optional<PromptDecision> decided = adapter->EvaluatePermissionSync(
        tool_name ? tool_name : "", adapter->permission_extension_id_,
        arguments ? arguments : "{}");
    if (!decided) {
      return MahoAgentPermissionDecision_Deny;
    }
    return *decided == PromptDecision::kDeny ? MahoAgentPermissionDecision_Deny
                                             : MahoAgentPermissionDecision_Allow;
  }

  static MahoAgentPermissionDecision OnAgentPermission(void* user_data,
                                                       const char* tool_name,
                                                       const char* arguments) {
    return MahoUnifiedAgentAdapter::OnAgentPermission(user_data, tool_name, arguments);
  }

  static void SetPromptOverride(MahoUnifiedAgentAdapter* adapter,
                                PromptDecision decision) {
    adapter->permission_prompt_override_for_testing_ = base::BindRepeating(
        [](PromptDecision d, const std::string&, const std::string&) { return d; },
        decision);
  }

  static MahoAgentPermissionDecision RunDialog(MahoUnifiedAgentAdapter* adapter,
                                               const std::string& tool,
                                               const std::string& ext) {
    return adapter->RunPermissionDialogForTest(tool, ext);
  }

  static MahoAgentPermissionDecision EvaluateWithExt(MahoUnifiedAgentAdapter* adapter,
                                                     const std::string& tool,
                                                     const std::string& ext) {
    std::optional<PromptDecision> decided = adapter->EvaluatePermissionSync(tool, ext, "{}");
    if (!decided) {
      return MahoAgentPermissionDecision_Deny;
    }
    return *decided == PromptDecision::kDeny ? MahoAgentPermissionDecision_Deny
                                             : MahoAgentPermissionDecision_Allow;
  }
};

TEST(MahoUnifiedAgentAdapterTest, ApprovalPolicyThreeWaySplit) {
  base::test::TaskEnvironment task_environment;
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterStringPref(maho::ai_prefs::kApprovalPolicy, "prompt");
  
  MahoUnifiedAgentAdapter adapter(&prefs, nullptr);

  // 1. With "allow" policy: should return Allow immediately for fs_read
  prefs.SetString(maho::ai_prefs::kApprovalPolicy, "allow");
  EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::EvaluatePermissionSync(&adapter, "fs_read", "{}"),
            MahoAgentPermissionDecision_Allow);

  // 2. With "deny" policy: should return Deny immediately for fs_read
  prefs.SetString(maho::ai_prefs::kApprovalPolicy, "deny");
  EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::EvaluatePermissionSync(&adapter, "fs_read", "{}"),
            MahoAgentPermissionDecision_Deny);

  // 3. With "prompt" policy: when permission_extension_id_ is empty and tool is shell_exec, should return Deny
  prefs.SetString(maho::ai_prefs::kApprovalPolicy, "prompt");
  EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::EvaluatePermissionSync(&adapter, "shell_exec", "{}"),
            MahoAgentPermissionDecision_Deny);
}

TEST(MahoUnifiedAgentAdapterTest, PolicyDenyBlocksAutoAllowedPageTools) {
  base::test::TaskEnvironment task_environment;
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterStringPref(maho::ai_prefs::kApprovalPolicy, "prompt");
  prefs.registry()->RegisterDictionaryPref(maho::ai_prefs::kAgentToolGrants);
  MahoUnifiedAgentAdapter adapter(&prefs, nullptr);

  prefs.SetString(maho::ai_prefs::kApprovalPolicy, "deny");
  for (const char* tool : {"read_current_page", "get_selected_text",
                           "search_in_page", "fs_read", "web_search",
                           "unknown_tool"}) {
    EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::EvaluatePermissionSync(&adapter, tool, "{}"),
              MahoAgentPermissionDecision_Deny)
        << "tool=" << tool;
  }
}

TEST(MahoUnifiedAgentAdapterTest, PolicyAllowPermitsUnknownTools) {
  base::test::TaskEnvironment task_environment;
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterStringPref(maho::ai_prefs::kApprovalPolicy, "prompt");
  prefs.registry()->RegisterDictionaryPref(maho::ai_prefs::kAgentToolGrants);
  MahoUnifiedAgentAdapter adapter(&prefs, nullptr);

  prefs.SetString(maho::ai_prefs::kApprovalPolicy, "allow");
  for (const char* tool : {"read_current_page", "fs_read", "unknown_tool"}) {
    EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::EvaluatePermissionSync(&adapter, tool, "{}"),
              MahoAgentPermissionDecision_Allow)
        << "tool=" << tool;
  }
}

TEST(MahoUnifiedAgentAdapterTest, ShellExecAlwaysDeniedEvenUnderAllow) {
  base::test::TaskEnvironment task_environment;
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterStringPref(maho::ai_prefs::kApprovalPolicy, "prompt");
  prefs.registry()->RegisterDictionaryPref(maho::ai_prefs::kAgentToolGrants);
  MahoUnifiedAgentAdapter adapter(&prefs, nullptr);

  for (const char* policy : {"allow", "prompt", ""}) {
    prefs.SetString(maho::ai_prefs::kApprovalPolicy, policy);
    EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::EvaluatePermissionSync(&adapter, "shell_exec", "{}"),
              MahoAgentPermissionDecision_Deny)
        << "policy=" << policy;
  }
}

TEST(MahoUnifiedAgentAdapterTest, PolicyPromptAutoAllowsPageTools) {
  base::test::TaskEnvironment task_environment;
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterStringPref(maho::ai_prefs::kApprovalPolicy, "prompt");
  prefs.registry()->RegisterDictionaryPref(maho::ai_prefs::kAgentToolGrants);
  MahoUnifiedAgentAdapter adapter(&prefs, nullptr);

  for (const char* tool : {"read_current_page", "get_active_tab",
                           "extract_structured_page_context"}) {
    EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::EvaluatePermissionSync(&adapter, tool, "{}"),
              MahoAgentPermissionDecision_Allow)
        << "tool=" << tool;
  }
}

TEST(MahoUnifiedAgentAdapterTest, NullUserDataDeniesAllTools) {
  for (const char* tool : {"shell_exec", "fs_read", "web_search",
                           "read_current_page"}) {
    EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::OnAgentPermission(nullptr, tool, "{}"),
              MahoAgentPermissionDecision_Deny)
        << "tool=" << tool;
  }
  EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::OnAgentPermission(nullptr, nullptr, "{}"),
            MahoAgentPermissionDecision_Deny);
}

TEST(MahoUnifiedAgentAdapterTest, DialogAllowPersistsGrantToPrefs) {
  base::test::TaskEnvironment task_environment;
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterStringPref(maho::ai_prefs::kApprovalPolicy, "prompt");
  prefs.registry()->RegisterDictionaryPref(maho::ai_prefs::kAgentToolGrants);
  MahoUnifiedAgentAdapter adapter(&prefs, nullptr);
  MahoUnifiedAgentAdapterTestHelper::SetPromptOverride(&adapter, PromptDecision::kAllow);

  // #5/#6 dialog path: "kAllow" must persist a durable grant into prefs.
  EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::RunDialog(&adapter, "mytool", "myext"),
            MahoAgentPermissionDecision_Allow);

  const auto& grants = prefs.GetDict(maho::ai_prefs::kAgentToolGrants);
  const auto* list = grants.FindList("myext");
  ASSERT_TRUE(list);
  bool found = false;
  for (const auto& item : *list) {
    if (item.is_string() && item.GetString() == "mytool") {
      found = true;
    }
  }
  EXPECT_TRUE(found) << "kAllow must persist the grant under the extension id";
}

TEST(MahoUnifiedAgentAdapterTest, DialogAllowOnceGrantsSessionButNotPersisted) {
  base::test::TaskEnvironment task_environment;
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterStringPref(maho::ai_prefs::kApprovalPolicy, "prompt");
  prefs.registry()->RegisterDictionaryPref(maho::ai_prefs::kAgentToolGrants);
  MahoUnifiedAgentAdapter adapter(&prefs, nullptr);
  MahoUnifiedAgentAdapterTestHelper::SetPromptOverride(&adapter, PromptDecision::kAllowOnce);

  EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::RunDialog(&adapter, "mytool", "myext"),
            MahoAgentPermissionDecision_Allow);

  // kAllowOnce must NOT persist to prefs...
  const auto& grants = prefs.GetDict(maho::ai_prefs::kAgentToolGrants);
  EXPECT_FALSE(grants.FindList("myext"))
      << "kAllowOnce must not write a durable grant";

  // ...but must grant for the rest of the session, so a re-evaluation resolves
  // to Allow via session_grants without needing another dialog.
  EXPECT_EQ(MahoUnifiedAgentAdapterTestHelper::EvaluateWithExt(&adapter, "mytool", "myext"),
            MahoAgentPermissionDecision_Allow);
}

TEST(MahoAuthUtilsTest, HasValidRelaySession) {
  base::test::TaskEnvironment task_environment;
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterStringPref(maho::account_prefs::kRelayAccessTokenEncryptedB64, "");
  prefs.registry()->RegisterInt64Pref(maho::account_prefs::kRelayAccessTokenExpiresAt, 0);
  prefs.registry()->RegisterStringPref(maho::account_prefs::kRelayRefreshTokenEncryptedB64, "");
  prefs.registry()->RegisterInt64Pref(maho::account_prefs::kRelayRefreshTokenExpiresAt, 0);

  // 1. Initially empty should be invalid
  EXPECT_FALSE(maho::auth::HasValidRelaySession(&prefs));

  // 2. Access token set and not expired (expires in future)
  prefs.SetString(maho::account_prefs::kRelayAccessTokenEncryptedB64, "some-token");
  prefs.SetInt64(maho::account_prefs::kRelayAccessTokenExpiresAt, base::Time::Now().ToTimeT() + 3600);
  EXPECT_TRUE(maho::auth::HasValidRelaySession(&prefs));

  // 3. Access token expired but refresh token valid
  prefs.SetInt64(maho::account_prefs::kRelayAccessTokenExpiresAt, base::Time::Now().ToTimeT() - 3600);
  prefs.SetString(maho::account_prefs::kRelayRefreshTokenEncryptedB64, "some-refresh");
  prefs.SetInt64(maho::account_prefs::kRelayRefreshTokenExpiresAt, base::Time::Now().ToTimeT() + 3600);
  EXPECT_TRUE(maho::auth::HasValidRelaySession(&prefs));

  // 4. Both expired
  prefs.SetInt64(maho::account_prefs::kRelayRefreshTokenExpiresAt, base::Time::Now().ToTimeT() - 3600);
  EXPECT_FALSE(maho::auth::HasValidRelaySession(&prefs));
}
