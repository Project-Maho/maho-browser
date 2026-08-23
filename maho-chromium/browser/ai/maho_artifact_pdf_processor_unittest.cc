// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_artifact_pdf_processor.h"

#include <cassert>
#include <iostream>
#include <string>

#ifndef MAHO_STANDALONE_TEST
#include "testing/gtest/include/gtest/gtest.h"
#endif

namespace maho::ai {
namespace {

std::string BuildSyntheticPdf(std::string_view version, size_t page_count) {
  std::string pdf;
  pdf.append("%PDF-");
  pdf.append(version);
  pdf.append("\n%âãÏÓ\n");

  pdf.append("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  pdf.append("2 0 obj\n<< /Type /Pages /Count ");
  pdf.append(std::to_string(page_count));
  pdf.append(" /Kids [");
  for (size_t i = 0; i < page_count; ++i) {
    pdf.append(std::to_string(3 + i * 2) + " 0 R ");
  }
  pdf.append("] >>\nendobj\n");

  for (size_t i = 0; i < page_count; ++i) {
    pdf.append(std::to_string(3 + i * 2) +
               " 0 obj\n<< /Type /Page /Parent 2 0 R >>\nendobj\n");
    pdf.append(std::to_string(4 + i * 2) +
               " 0 obj\n<< /Length 20 >>\nstream\nBT /F1 12 Tf ET\nendstream\n"
               "endobj\n");
  }

  pdf.append("xref\n0 ");
  pdf.append(std::to_string(3 + page_count * 2));
  pdf.append("\n0000000000 65535 f \n");
  pdf.append("trailer\n<< /Size ");
  pdf.append(std::to_string(3 + page_count * 2));
  pdf.append(" /Root 1 0 R >>\nstartxref\n500\n%%EOF\n");
  return pdf;
}

#ifndef MAHO_STANDALONE_TEST

TEST(MahoArtifactPdfProcessorTest, ParsesValidPdfVersionAndPageCount) {
  std::string pdf = BuildSyntheticPdf("1.7", 3);
  auto result = MahoArtifactPdfProcessor::Process(pdf);
  ASSERT_TRUE(result.has_value()) << result.error();

  EXPECT_EQ(result->version, "1.7");
  EXPECT_EQ(result->mime_type, "application/pdf");
  EXPECT_EQ(result->size_bytes, pdf.size());
  ASSERT_TRUE(result->page_count.has_value());
  EXPECT_EQ(*result->page_count, 3u);
  EXPECT_EQ(result->data, pdf);
}

TEST(MahoArtifactPdfProcessorTest, ValidateForPreviewReturnsOriginalBytes) {
  std::string pdf = BuildSyntheticPdf("1.4", 1);
  auto preview_res = MahoArtifactPdfProcessor::ValidateForPreview(pdf);
  ASSERT_TRUE(preview_res.has_value()) << preview_res.error();
  EXPECT_EQ(*preview_res, pdf);
}

TEST(MahoArtifactPdfProcessorTest, MissingHeaderReturnsTypedError) {
  std::string invalid_pdf = "This is not a PDF file at all.\n%%EOF\n";
  auto result = MahoArtifactPdfProcessor::Process(invalid_pdf);
  EXPECT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("missing %PDF- magic signature"),
            std::string::npos);
}

TEST(MahoArtifactPdfProcessorTest, TruncatedPdfMissingEofReturnsTypedError) {
  std::string truncated_pdf =
      "%PDF-1.5\n1 0 obj\n<< /Type /Catalog >>\nendobj\n";
  auto result = MahoArtifactPdfProcessor::Process(truncated_pdf);
  EXPECT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("missing %%EOF trailer marker"),
            std::string::npos);
}

TEST(MahoArtifactPdfProcessorTest, EmptyDataReturnsTypedError) {
  auto result = MahoArtifactPdfProcessor::Process("");
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), "Input PDF data is empty");
}

#endif  // !MAHO_STANDALONE_TEST

}  // namespace
}  // namespace maho::ai

#ifdef MAHO_STANDALONE_TEST
int main() {
  std::cout << "[RUN] MahoArtifactPdfProcessor standalone tests..."
            << std::endl;

  using namespace maho::ai;

  // Test 1: Valid PDF
  {
    std::string pdf = BuildSyntheticPdf("1.7", 3);
    auto result = MahoArtifactPdfProcessor::Process(pdf);
    assert(result.has_value());
    assert(result->version == "1.7");
    assert(result->mime_type == "application/pdf");
    assert(result->size_bytes == pdf.size());
    assert(result->page_count.has_value() && *result->page_count == 3u);
    assert(result->data == pdf);
  }

  // Test 2: ValidateForPreview
  {
    std::string pdf = BuildSyntheticPdf("1.4", 1);
    auto preview_res = MahoArtifactPdfProcessor::ValidateForPreview(pdf);
    assert(preview_res.has_value());
    assert(*preview_res == pdf);
  }

  // Test 3: Missing Header Error
  {
    std::string invalid_pdf = "This is not a PDF file at all.\n%%EOF\n";
    auto result = MahoArtifactPdfProcessor::Process(invalid_pdf);
    assert(!result.has_value());
    assert(result.error().find("missing %PDF- magic signature") !=
           std::string::npos);
  }

  // Test 4: Truncated EOF Error
  {
    std::string truncated_pdf =
        "%PDF-1.5\n1 0 obj\n<< /Type /Catalog >>\nendobj\n";
    auto result = MahoArtifactPdfProcessor::Process(truncated_pdf);
    assert(!result.has_value());
    assert(result.error().find("missing %%EOF trailer marker") !=
           std::string::npos);
  }

  // Test 5: Empty Data Error
  {
    auto result = MahoArtifactPdfProcessor::Process("");
    assert(!result.has_value());
    assert(result.error() == "Input PDF data is empty");
  }

  std::cout << "[PASS] All MahoArtifactPdfProcessor standalone tests passed!"
            << std::endl;
  return 0;
}
#endif
