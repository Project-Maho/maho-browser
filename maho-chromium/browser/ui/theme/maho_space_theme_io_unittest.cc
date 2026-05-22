// Copyright 2026 Maho Browser. All rights reserved.

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "maho/browser/ui/theme/maho_space_theme_io.h"
#include "testing/gtest/include/gtest/gtest.h"

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

}  // namespace
}  // namespace maho_theme
