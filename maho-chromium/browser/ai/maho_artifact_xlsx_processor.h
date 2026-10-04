// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_ARTIFACT_XLSX_PROCESSOR_H_
#define MAHO_BROWSER_AI_MAHO_ARTIFACT_XLSX_PROCESSOR_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(MAHO_STANDALONE_TEST)
namespace base {
template <typename E>
struct unexpected {
  E error;
  explicit unexpected(E err) : error(std::move(err)) {}
};

template <typename T>
struct ok {
  T value;
  explicit ok(T val) : value(std::move(val)) {}
};

template <typename T, typename E>
class expected {
 public:
  expected(const T& val) : val_(val), has_val_(true) {}
  expected(T&& val) : val_(std::move(val)), has_val_(true) {}
  expected(ok<T> o) : val_(std::move(o.value)), has_val_(true) {}
  expected(unexpected<E> unexp) : err_(std::move(unexp.error)), has_val_(false) {}

  bool has_value() const { return has_val_; }
  explicit operator bool() const { return has_val_; }
  const T& value() const { return val_; }
  T& value() { return val_; }
  const T& operator*() const { return val_; }
  T& operator*() { return val_; }
  const T* operator->() const { return &val_; }
  T* operator->() { return &val_; }
  const E& error() const { return err_; }
  E& error() { return err_; }
  T value_or(T default_val) const { return has_val_ ? val_ : default_val; }

 private:
  T val_{};
  E err_{};
  bool has_val_ = false;
};
}  // namespace base
#else
#include "base/types/expected.h"
#endif

namespace maho::ai {

struct MahoXlsxSheet {
  MahoXlsxSheet();
  MahoXlsxSheet(const MahoXlsxSheet&);
  MahoXlsxSheet& operator=(const MahoXlsxSheet&);
  ~MahoXlsxSheet();

  std::string name;
  std::vector<std::vector<std::string>> cells;

  bool operator==(const MahoXlsxSheet& other) const {
    return name == other.name && cells == other.cells;
  }
};

struct MahoXlsxModel {
  MahoXlsxModel();
  MahoXlsxModel(const MahoXlsxModel&);
  MahoXlsxModel& operator=(const MahoXlsxModel&);
  ~MahoXlsxModel();

  std::vector<MahoXlsxSheet> sheets;

  bool operator==(const MahoXlsxModel& other) const {
    return sheets == other.sheets;
  }

  // Serializes the model into sheet model JSON:
  // {"sheets":[{"name":"Sheet1","cells":[["A1","B1"],["A2","B2"]]}]}
  std::string ToJson() const;

  // Parses from JSON string representation.
  static base::expected<MahoXlsxModel, std::string> FromJson(
      std::string_view json_str);
};

class MahoArtifactXlsxProcessor {
 public:
  static constexpr size_t kMaxXlsxBytes = 32 * 1024 * 1024;        // 32 MiB
  static constexpr size_t kMaxExtractedXmlBytes = 64 * 1024 * 1024; // 64 MiB
  static constexpr size_t kMaxRowsPerSheet = 5000;
  static constexpr size_t kMaxColsPerSheet = 256;
  static constexpr size_t kMaxSheets = 50;

  // Processes raw XLSX bytes (ZIP archive containing workbook, shared strings,
  // and worksheet XMLs). Also supports CSV / delimited plaintext fallback.
  // Returns MahoXlsxModel on success, or a descriptive typed error on failure.
  static base::expected<MahoXlsxModel, std::string> ProcessBytes(
      const uint8_t* data, size_t size);

  static base::expected<MahoXlsxModel, std::string> Process(
      std::string_view raw_data);

  // Directly generates the sheet model JSON string for preview rendering.
  static base::expected<std::string, std::string> GeneratePreviewJson(
      std::string_view raw_data);
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_ARTIFACT_XLSX_PROCESSOR_H_
