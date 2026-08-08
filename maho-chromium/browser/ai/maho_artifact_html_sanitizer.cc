// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_artifact_html_sanitizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace maho {
namespace {

constexpr size_t kPayloadLimit =
    kMaxSanitizedArtifactHtmlBytes -
    (sizeof(kSanitizedArtifactTruncationMarker) - 1);

bool IsAsciiWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool IsNameCharacter(char c) {
  const unsigned char value = static_cast<unsigned char>(c);
  return std::isalnum(value) || c == '-' || c == '_' || c == ':';
}

char AsciiLower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c + ('a' - 'A');
  }
  return c;
}

std::string NormalizeName(std::string_view name) {
  std::string normalized;
  normalized.reserve(name.size());
  for (char c : name) {
    if (c != '\0') {
      normalized.push_back(AsciiLower(c));
    }
  }
  return normalized;
}

std::string TrimAndRemoveAsciiControls(std::string_view value) {
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end &&
         (IsAsciiWhitespace(value[begin]) || value[begin] == '\0')) {
    ++begin;
  }
  while (end > begin &&
         (IsAsciiWhitespace(value[end - 1]) || value[end - 1] == '\0')) {
    --end;
  }

  std::string normalized;
  normalized.reserve(end - begin);
  for (size_t i = begin; i < end; ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if (c > 0x20 && c != 0x7f) {
      normalized.push_back(AsciiLower(value[i]));
    }
  }
  return normalized;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

bool IsAllowedImageUrl(std::string_view value) {
  const std::string normalized = TrimAndRemoveAsciiControls(value);
  return StartsWith(normalized, "data:") || StartsWith(normalized, "blob:");
}

bool IsAllowedFragmentUrl(std::string_view value) {
  size_t position = 0;
  while (position < value.size() &&
         (IsAsciiWhitespace(value[position]) || value[position] == '\0')) {
    ++position;
  }
  return position < value.size() && value[position] == '#';
}

bool IsAllowedTag(std::string_view tag) {
  static constexpr std::array<std::string_view, 34> kAllowedTags = {
      "a",     "b",  "blockquote", "br", "code",  "dd",     "del",
      "div",   "dl", "dt",         "em", "h1",    "h2",     "h3",
      "h4",    "h5", "h6",         "hr", "i",     "img",    "li",
      "ol",    "p",  "pre",        "s",  "span",  "strong", "table",
      "tbody", "td", "tfoot",      "th", "thead", "tr",
  };
  return std::find(kAllowedTags.begin(), kAllowedTags.end(), tag) !=
         kAllowedTags.end();
}

bool IsVoidTag(std::string_view tag) {
  return tag == "br" || tag == "hr" || tag == "img";
}

bool IsSubtreeStripTag(std::string_view tag) {
  return tag == "script" || tag == "svg" || tag == "math";
}

bool IsGlobalAttribute(std::string_view attribute) {
  static constexpr std::array<std::string_view, 6> kGlobalAttributes = {
      "class", "dir", "id", "lang", "role", "title"};
  return std::find(kGlobalAttributes.begin(), kGlobalAttributes.end(),
                   attribute) != kGlobalAttributes.end() ||
         StartsWith(attribute, "aria-");
}

bool IsAllowedAttribute(std::string_view tag, std::string_view attribute) {
  if (IsGlobalAttribute(attribute)) {
    return true;
  }
  if (tag == "img") {
    return attribute == "alt" || attribute == "height" || attribute == "src" ||
           attribute == "width";
  }
  if (tag == "td" || tag == "th") {
    return attribute == "colspan" || attribute == "rowspan";
  }
  if (tag == "ol") {
    return attribute == "reversed" || attribute == "start";
  }
  return (tag == "a" && attribute == "href") ||
         (tag == "li" && attribute == "value");
}

struct Attribute {
  std::string name;
  std::string_view value;
};

struct TagToken {
  std::string name;
  std::vector<Attribute> attributes;
  bool is_end = false;
  bool self_closing = false;
};

// Parses one complete tag. The caller has already established that input[pos]
// is '<'. On success, |next| points just after '>'.
bool ParseTag(std::string_view input, size_t position, TagToken *token,
              size_t *next) {
  size_t cursor = position + 1;
  while (cursor < input.size() && input[cursor] == '\0') {
    ++cursor;
  }
  if (cursor < input.size() && input[cursor] == '/') {
    token->is_end = true;
    ++cursor;
  }
  while (cursor < input.size() && input[cursor] == '\0') {
    ++cursor;
  }

  const size_t name_begin = cursor;
  std::string raw_name;
  while (cursor < input.size() &&
         (IsNameCharacter(input[cursor]) || input[cursor] == '\0')) {
    raw_name.push_back(input[cursor]);
    ++cursor;
  }
  if (cursor == name_begin || (token->name = NormalizeName(raw_name)).empty()) {
    return false;
  }

  while (cursor < input.size()) {
    while (cursor < input.size() &&
           (IsAsciiWhitespace(input[cursor]) || input[cursor] == '\0')) {
      ++cursor;
    }
    if (cursor >= input.size()) {
      return false;
    }
    if (input[cursor] == '>') {
      *next = cursor + 1;
      return true;
    }
    if (input[cursor] == '/') {
      token->self_closing = true;
      ++cursor;
      continue;
    }

    std::string raw_attribute_name;
    while (cursor < input.size() &&
           (IsNameCharacter(input[cursor]) || input[cursor] == '\0')) {
      raw_attribute_name.push_back(input[cursor]);
      ++cursor;
    }
    if (raw_attribute_name.empty()) {
      // A malformed byte is not allowed to make the scanner lose sight of a
      // later closing quote or '>'. Advance deterministically by one byte.
      ++cursor;
      continue;
    }

    Attribute attribute;
    attribute.name = NormalizeName(raw_attribute_name);
    while (cursor < input.size() &&
           (IsAsciiWhitespace(input[cursor]) || input[cursor] == '\0')) {
      ++cursor;
    }
    if (cursor < input.size() && input[cursor] == '=') {
      ++cursor;
      while (cursor < input.size() &&
             (IsAsciiWhitespace(input[cursor]) || input[cursor] == '\0')) {
        ++cursor;
      }
      if (cursor >= input.size()) {
        return false;
      }
      if (input[cursor] == '\'' || input[cursor] == '"') {
        const char quote = input[cursor++];
        const size_t value_begin = cursor;
        while (cursor < input.size() && input[cursor] != quote) {
          ++cursor;
        }
        if (cursor >= input.size()) {
          return false;
        }
        attribute.value = input.substr(value_begin, cursor - value_begin);
        ++cursor;
      } else {
        const size_t value_begin = cursor;
        while (cursor < input.size() && !IsAsciiWhitespace(input[cursor]) &&
               input[cursor] != '>' && input[cursor] != '\0') {
          ++cursor;
        }
        attribute.value = input.substr(value_begin, cursor - value_begin);
      }
    }
    token->attributes.push_back(std::move(attribute));
  }
  return false;
}

class BoundedOutput {
public:
  bool truncated() const { return truncated_; }
  size_t size() const { return output_.size(); }

  void RollBackTo(size_t size) { output_.resize(size); }

  void AppendAtom(std::string_view atom) {
    if (truncated_) {
      return;
    }
    if (output_.size() + atom.size() > kPayloadLimit) {
      truncated_ = true;
      return;
    }
    output_.append(atom);
  }

  void AppendEscaped(std::string_view value) {
    for (size_t i = 0; i < value.size() && !truncated_;) {
      const unsigned char c = static_cast<unsigned char>(value[i]);
      if (c == 0) {
        ++i;
      } else if (c == '&') {
        AppendAtom("&amp;");
        ++i;
      } else if (c == '<') {
        AppendAtom("&lt;");
        ++i;
      } else if (c == '>') {
        AppendAtom("&gt;");
        ++i;
      } else if (c == '"') {
        AppendAtom("&quot;");
        ++i;
      } else if (c == '\'') {
        AppendAtom("&#39;");
        ++i;
      } else {
        size_t length = 1;
        if ((c & 0xe0) == 0xc0) {
          length = 2;
        } else if ((c & 0xf0) == 0xe0) {
          length = 3;
        } else if ((c & 0xf8) == 0xf0) {
          length = 4;
        }
        if (i + length > value.size()) {
          length = 1;
        } else {
          for (size_t continuation = 1; continuation < length; ++continuation) {
            if ((static_cast<unsigned char>(value[i + continuation]) & 0xc0) !=
                0x80) {
              length = 1;
              break;
            }
          }
        }
        AppendAtom(value.substr(i, length));
        i += length;
      }
    }
  }

  std::string Finish() {
    if (truncated_) {
      output_.append(kSanitizedArtifactTruncationMarker);
    }
    return output_;
  }

private:
  std::string output_;
  bool truncated_ = false;
};

void EmitTag(const TagToken &token, BoundedOutput *output) {
  const size_t tag_begin = output->size();
  if (token.is_end) {
    if (!IsVoidTag(token.name)) {
      output->AppendAtom("</");
      output->AppendAtom(token.name);
      output->AppendAtom(">");
    }
    if (output->truncated()) {
      output->RollBackTo(tag_begin);
    }
    return;
  }

  output->AppendAtom("<");
  output->AppendAtom(token.name);
  std::set<std::string> emitted_attributes;
  for (const Attribute &attribute : token.attributes) {
    if (attribute.name.empty() ||
        !IsAllowedAttribute(token.name, attribute.name) ||
        !emitted_attributes.insert(attribute.name).second) {
      continue;
    }
    if (attribute.name == "src" && !IsAllowedImageUrl(attribute.value)) {
      continue;
    }
    if (attribute.name == "href" && !IsAllowedFragmentUrl(attribute.value)) {
      continue;
    }
    output->AppendAtom(" ");
    output->AppendAtom(attribute.name);
    output->AppendAtom("=\"");
    output->AppendEscaped(attribute.value);
    output->AppendAtom("\"");
  }
  output->AppendAtom(">");
  if (output->truncated()) {
    output->RollBackTo(tag_begin);
  }
}

} // namespace

std::string SanitizeArtifactHtml(std::string_view input) {
  BoundedOutput output;
  std::vector<std::string> stripped_subtrees;

  for (size_t position = 0; position < input.size() && !output.truncated();) {
    if (input[position] != '<') {
      const size_t text_end = input.find('<', position);
      if (stripped_subtrees.empty()) {
        output.AppendEscaped(
            input.substr(position, text_end == std::string_view::npos
                                       ? input.size() - position
                                       : text_end - position));
      }
      position = text_end == std::string_view::npos ? input.size() : text_end;
      continue;
    }

    if (input.substr(position, 4) == "<!--") {
      const size_t comment_end = input.find("-->", position + 4);
      position = comment_end == std::string_view::npos ? input.size()
                                                       : comment_end + 3;
      continue;
    }
    if (input.substr(position, 2) == "<!" ||
        input.substr(position, 2) == "<?") {
      const size_t declaration_end = input.find('>', position + 2);
      position = declaration_end == std::string_view::npos
                     ? input.size()
                     : declaration_end + 1;
      continue;
    }

    TagToken token;
    size_t next = position;
    if (!ParseTag(input, position, &token, &next)) {
      if (stripped_subtrees.empty()) {
        output.AppendAtom("&lt;");
      }
      ++position;
      continue;
    }
    position = next;

    if (!stripped_subtrees.empty()) {
      if (!token.is_end && IsSubtreeStripTag(token.name) &&
          !token.self_closing) {
        stripped_subtrees.push_back(token.name);
      } else if (token.is_end && token.name == stripped_subtrees.back()) {
        stripped_subtrees.pop_back();
      }
      continue;
    }

    if (IsSubtreeStripTag(token.name)) {
      if (!token.is_end && !token.self_closing) {
        stripped_subtrees.push_back(token.name);
      }
      continue;
    }
    if (!IsAllowedTag(token.name)) {
      continue;
    }
    EmitTag(token, &output);
  }

  return output.Finish();
}

} // namespace maho
