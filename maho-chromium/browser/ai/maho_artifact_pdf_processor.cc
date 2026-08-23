// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/ai/maho_artifact_pdf_processor.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace maho::ai {

MahoPdfModel::MahoPdfModel() = default;
MahoPdfModel::MahoPdfModel(const MahoPdfModel&) = default;
MahoPdfModel& MahoPdfModel::operator=(const MahoPdfModel&) = default;
MahoPdfModel::~MahoPdfModel() = default;

namespace {

constexpr size_t kMaxHeaderScanBytes = 1024;
constexpr size_t kMaxTrailerScanBytes = 2048;

std::optional<std::string> ExtractPdfVersion(std::string_view header_view) {
  size_t pos = header_view.find("%PDF-");
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  size_t ver_start = pos + 5;
  size_t ver_end = ver_start;
  while (ver_end < header_view.size() &&
         (std::isdigit(static_cast<unsigned char>(header_view[ver_end])) ||
          header_view[ver_end] == '.')) {
    ver_end++;
  }
  if (ver_end > ver_start) {
    return std::string(header_view.substr(ver_start, ver_end - ver_start));
  }
  return "1.4";
}

std::optional<size_t> EstimatePageCount(std::string_view pdf_data) {
  // Strategy 1: Check for /Pages << /Count N >> catalog entry
  size_t count_pos = 0;
  while ((count_pos = pdf_data.find("/Count", count_pos)) != std::string_view::npos) {
    size_t val_start = count_pos + 6;
    while (val_start < pdf_data.size() && (pdf_data[val_start] == ' ' || pdf_data[val_start] == '\t' ||
                                           pdf_data[val_start] == '\r' || pdf_data[val_start] == '\n')) {
      val_start++;
    }
    size_t val_end = val_start;
    while (val_end < pdf_data.size() && std::isdigit(static_cast<unsigned char>(pdf_data[val_end]))) {
      val_end++;
    }
    if (val_end > val_start) {
      unsigned long n = 0;
      for (size_t k = val_start; k < val_end; ++k) {
        n = n * 10 + (pdf_data[k] - '0');
        if (n >= 100000) break;
      }
      if (n > 0 && n < 100000) {
        return static_cast<size_t>(n);
      }
    }
    count_pos += 6;
  }

  // Strategy 2: Count /Type /Page occurrences
  size_t pages = 0;
  size_t pos = 0;
  while ((pos = pdf_data.find("/Type", pos)) != std::string_view::npos) {
    size_t after_type = pos + 5;
    while (after_type < pdf_data.size() && (pdf_data[after_type] == ' ' || pdf_data[after_type] == '\t' ||
                                            pdf_data[after_type] == '\r' || pdf_data[after_type] == '\n')) {
      after_type++;
    }
    if (pdf_data.substr(after_type, 5) == "/Page" &&
        (after_type + 5 >= pdf_data.size() || pdf_data[after_type + 5] == ' ' ||
         pdf_data[after_type + 5] == '\t' || pdf_data[after_type + 5] == '\r' ||
         pdf_data[after_type + 5] == '\n' || pdf_data[after_type + 5] == '/' ||
         pdf_data[after_type + 5] == '>')) {
      pages++;
    }
    pos += 5;
  }

  if (pages > 0) {
    return pages;
  }
  return std::nullopt;
}

}  // namespace

base::expected<MahoPdfModel, std::string> MahoArtifactPdfProcessor::ProcessBytes(
    const uint8_t* data, size_t size) {
  if (!data || size == 0) {
    return base::unexpected(std::string("Input PDF data is empty"));
  }
  if (size > kMaxPdfBytes) {
    return base::unexpected(std::string("PDF data exceeds size limit (64 MiB)"));
  }

  std::string_view data_view(reinterpret_cast<const char*>(data), size);

  // Check PDF header
  size_t header_len = std::min(size, kMaxHeaderScanBytes);
  std::string_view header_view = data_view.substr(0, header_len);
  std::optional<std::string> version = ExtractPdfVersion(header_view);
  if (!version) {
    return base::unexpected(std::string("Invalid PDF header: missing %PDF- magic signature"));
  }

  // Check EOF trailer
  size_t trailer_len = std::min(size, kMaxTrailerScanBytes);
  std::string_view trailer_view = data_view.substr(size - trailer_len);
  if (trailer_view.find("%%EOF") == std::string_view::npos) {
    return base::unexpected(std::string("Truncated PDF: missing %%EOF trailer marker"));
  }

  MahoPdfModel model;
  model.version = std::move(*version);
  model.size_bytes = size;
  model.page_count = EstimatePageCount(data_view);
  model.mime_type = "application/pdf";
  model.data.assign(reinterpret_cast<const char*>(data), size);

  return model;
}

base::expected<MahoPdfModel, std::string> MahoArtifactPdfProcessor::Process(
    std::string_view raw_data) {
  return ProcessBytes(reinterpret_cast<const uint8_t*>(raw_data.data()), raw_data.size());
}

base::expected<std::string, std::string> MahoArtifactPdfProcessor::ValidateForPreview(
    std::string_view raw_data) {
  auto model_res = Process(raw_data);
  if (!model_res.has_value()) {
    return base::unexpected(std::string(model_res.error()));
  }
  return base::ok(std::string(model_res->data));
}

}  // namespace maho::ai
