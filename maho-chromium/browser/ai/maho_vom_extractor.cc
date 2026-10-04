// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/ai/maho_vom_extractor.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace maho::ai {

VomElementInput::VomElementInput() = default;
VomElementInput::VomElementInput(const VomElementInput&) = default;
VomElementInput& VomElementInput::operator=(const VomElementInput&) = default;
VomElementInput::~VomElementInput() = default;

VomPage::VomPage() = default;
VomPage::VomPage(const VomPage&) = default;
VomPage& VomPage::operator=(const VomPage&) = default;
VomPage::~VomPage() = default;

namespace {

constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t Fnv1a64(const std::string& input) {
  uint64_t hash = kFnvOffsetBasis;
  for (char c : input) {
    hash ^= static_cast<uint8_t>(c);
    hash *= kFnvPrime;
  }
  return hash;
}

std::string Hex64(uint64_t value) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[i] = kDigits[value & 0xF];
    value >>= 4;
  }
  return out;
}

std::string EscapeName(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

std::string BoundsSuffix(const gfx::Rect& bounds) {
  return " [x=" + std::to_string(bounds.x()) + ",y=" + std::to_string(bounds.y()) +
         ",w=" + std::to_string(bounds.width()) +
         ",h=" + std::to_string(bounds.height()) + "]";
}

std::string ElementLine(const VomElementInput& element,
                        const std::string& tag_prefix) {
  std::string line;
  if (!tag_prefix.empty()) {
    line += tag_prefix + " " + element.role + " \"" + EscapeName(element.name) +
            "\"" + BoundsSuffix(element.bounds_css);
  } else {
    line += "text \"" + EscapeName(element.name) + "\"" +
            BoundsSuffix(element.bounds_css);
  }
  if (!element.hover_hint.empty()) {
    line += " [hover first: " + element.hover_hint + "]";
  }
  line += "\n";
  return line;
}

std::string HeaderLine(const std::string& page_title,
                       size_t element_count,
                       size_t ref_count) {
  return "VOM " + page_title + " (" + std::to_string(element_count) +
         " elements, " + std::to_string(ref_count) + " refs)\n";
}

bool CoveredByLaterBackdrop(const std::vector<VomElementInput>& elements,
                            const VomElementInput& candidate) {
  for (const VomElementInput& other : elements) {
    if (other.draws_backdrop && other.paint_order > candidate.paint_order) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::optional<size_t> MahoVomExtractor::ResumeIndexFromCursor(
    const std::string& cursor) {
  size_t colon = cursor.rfind(':');
  if (colon == std::string::npos || colon + 1 >= cursor.size()) {
    return std::nullopt;
  }
  const std::string index_part = cursor.substr(colon + 1);
  if (index_part.empty() ||
      !std::all_of(index_part.begin(), index_part.end(),
                   [](unsigned char c) { return std::isdigit(c); })) {
    return std::nullopt;
  }
  size_t value = 0;
  size_t digits = 0;
  for (char c : index_part) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + static_cast<size_t>(c - '0');
    if (++digits > 20) {
      return std::nullopt;
    }
  }
  return value;
}

VomPage MahoVomExtractor::Extract(const std::vector<VomElementInput>& elements,
                                  const std::string& page_title,
                                  const std::string& cursor,
                                  uint32_t max_tokens) {
  VomPage page;

  size_t resume_index = 0;
  if (!cursor.empty()) {
    resume_index = ResumeIndexFromCursor(cursor).value_or(0);
  }

  std::vector<const VomElementInput*> work;
  work.reserve(elements.size());
  for (size_t i = 0; i < elements.size(); ++i) {
    if (i < resume_index) {
      continue;
    }
    const VomElementInput& element = elements[i];
    if (element.is_hidden || element.is_occluded ||
        CoveredByLaterBackdrop(elements, element)) {
      continue;
    }
    work.push_back(&element);
  }

  auto tag_by_paint_order = [](std::vector<const VomElementInput*>& items) {
    std::vector<const VomElementInput*> actionable;
    for (const VomElementInput* element : items) {
      if (element->is_actionable) {
        actionable.push_back(element);
      }
    }
    std::stable_sort(actionable.begin(), actionable.end(),
                     [](const VomElementInput* a, const VomElementInput* b) {
                       return a->paint_order < b->paint_order;
                     });
    return actionable;
  };

  // Pass 1: tag everything, serialize, then cut trailing whole elements until
  // the approximate token budget (chars / 4) fits.
  std::vector<const VomElementInput*> actionable = tag_by_paint_order(work);
  size_t kept = work.size();
  if (!work.empty()) {
    std::string body;
    for (const VomElementInput* element : work) {
      const auto it = std::find(actionable.begin(), actionable.end(), element);
      const std::string tag_prefix =
          it == actionable.end()
              ? std::string()
              : "@e" + std::to_string(1 + (it - actionable.begin()));
      body += ElementLine(*element, tag_prefix);
    }
    const std::string header =
        HeaderLine(page_title, work.size(), actionable.size());
    const std::string full = header + body;
    if (full.size() / 4 > static_cast<size_t>(max_tokens)) {
      while (kept > 0) {
        std::string kept_body;
        for (size_t i = 0; i < kept; ++i) {
          const auto it =
              std::find(actionable.begin(), actionable.end(), work[i]);
          const std::string tag_prefix =
              it == actionable.end()
                  ? std::string()
                  : "@e" + std::to_string(1 + (it - actionable.begin()));
          kept_body += ElementLine(*work[i], tag_prefix);
        }
        size_t kept_refs = 0;
        for (size_t i = 0; i < kept; ++i) {
          if (std::find(actionable.begin(), actionable.end(), work[i]) !=
              actionable.end()) {
            ++kept_refs;
          }
        }
        const std::string candidate =
            HeaderLine(page_title, kept, kept_refs) + kept_body;
        if (candidate.size() / 4 <= static_cast<size_t>(max_tokens)) {
          break;
        }
        --kept;
      }
    }
  }

  // Pass 2: re-tag the kept subset so tags are contiguous @e1..@eK per page,
  // then serialize the final text and refs. Re-numbering only shrinks tag
  // digits, so the budget still holds.
  std::vector<const VomElementInput*> kept_items(work.begin(),
                                                 work.begin() + kept);
  std::vector<const VomElementInput*> kept_actionable =
      tag_by_paint_order(kept_items);

  std::string body;
  for (const VomElementInput* element : kept_items) {
    const auto it =
        std::find(kept_actionable.begin(), kept_actionable.end(), element);
    std::string tag_prefix;
    if (it != kept_actionable.end()) {
      const size_t tag_index = 1 + (it - kept_actionable.begin());
      tag_prefix = "@e" + std::to_string(tag_index);
      page.refs.push_back({element->backend_node_id, element->bounds_css,
                           tag_prefix});
    }
    body += ElementLine(*element, tag_prefix);
  }
  std::sort(page.refs.begin(), page.refs.end(),
            [](const VomRef& a, const VomRef& b) { return a.tag < b.tag; });
  page.text =
      HeaderLine(page_title, kept_items.size(), kept_actionable.size()) + body;

  if (kept < work.size()) {
    const size_t first_dropped = work[kept] - elements.data();
    std::string fingerprint;
    for (const VomElementInput& element : elements) {
      fingerprint += element.role;
      fingerprint.push_back('\x1f');
      fingerprint += element.name;
      fingerprint.push_back('\x1f');
      fingerprint += std::to_string(element.bounds_css.x()) + "," +
                     std::to_string(element.bounds_css.y()) + "," +
                     std::to_string(element.bounds_css.width()) + "," +
                     std::to_string(element.bounds_css.height()) + ";";
    }
    page.next_cursor = Hex64(Fnv1a64(fingerprint)) + ":" +
                       std::to_string(first_dropped);
  }
  return page;
}

}  // namespace maho::ai
