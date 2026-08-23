// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_ARTIFACT_PDF_PROCESSOR_H_
#define MAHO_BROWSER_AI_MAHO_ARTIFACT_PDF_PROCESSOR_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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

struct MahoPdfModel {
  MahoPdfModel();
  MahoPdfModel(const MahoPdfModel&);
  MahoPdfModel& operator=(const MahoPdfModel&);
  ~MahoPdfModel();

  std::string version;               // e.g. "1.4", "1.7", "2.0"
  size_t size_bytes = 0;
  std::optional<size_t> page_count;
  std::string mime_type = "application/pdf";
  std::string data;                  // Validated PDF raw bytes

  bool operator==(const MahoPdfModel& other) const {
    return version == other.version && size_bytes == other.size_bytes &&
           page_count == other.page_count && mime_type == other.mime_type &&
           data == other.data;
  }
};

class MahoArtifactPdfProcessor {
 public:
  static constexpr size_t kMaxPdfBytes = 64 * 1024 * 1024;  // 64 MiB

  // Validates raw PDF bytes, checks `%PDF-` header and structure,
  // extracts version and metadata.
  // Returns MahoPdfModel on success or a descriptive typed error on failure.
  static base::expected<MahoPdfModel, std::string> ProcessBytes(
      const uint8_t* data, size_t size);

  static base::expected<MahoPdfModel, std::string> Process(
      std::string_view raw_data);

  // Validates PDF bytes and returns the bytes for preview rendering with
  // mime "application/pdf".
  static base::expected<std::string, std::string> ValidateForPreview(
      std::string_view raw_data);
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_ARTIFACT_PDF_PROCESSOR_H_
