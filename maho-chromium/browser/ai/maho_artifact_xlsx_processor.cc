// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/ai/maho_artifact_xlsx_processor.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace maho::ai {

MahoXlsxSheet::MahoXlsxSheet() = default;
MahoXlsxSheet::MahoXlsxSheet(const MahoXlsxSheet&) = default;
MahoXlsxSheet& MahoXlsxSheet::operator=(const MahoXlsxSheet&) = default;
MahoXlsxSheet::~MahoXlsxSheet() = default;

MahoXlsxModel::MahoXlsxModel() = default;
MahoXlsxModel::MahoXlsxModel(const MahoXlsxModel&) = default;
MahoXlsxModel& MahoXlsxModel::operator=(const MahoXlsxModel&) = default;
MahoXlsxModel::~MahoXlsxModel() = default;

namespace {

struct ZipEntry {
  std::string filename;
  uint16_t method = 0;
  uint32_t compressed_size = 0;
  uint32_t uncompressed_size = 0;
  uint32_t local_header_offset = 0;
};

std::string ToLower(std::string_view str) {
  std::string result;
  result.reserve(str.size());
  for (char c : str) {
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return result;
}

std::string EscapeJsonString(std::string_view input) {
  std::string out;
  out.reserve(input.size() + 2);
  out.push_back('"');
  for (char c : input) {
    switch (c) {
      case '"':
        out.append("\\\"");
        break;
      case '\\':
        out.append("\\\\");
        break;
      case '\b':
        out.append("\\b");
        break;
      case '\f':
        out.append("\\f");
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\r':
        out.append("\\r");
        break;
      case '\t':
        out.append("\\t");
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out.append(buf);
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

std::string UnescapeXml(std::string_view input) {
  std::string result;
  result.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '&') {
      size_t semi = input.find(';', i + 1);
      if (semi != std::string_view::npos && semi - i <= 10) {
        std::string_view entity = input.substr(i + 1, semi - i - 1);
        if (entity == "amp") {
          result.push_back('&');
          i = semi;
          continue;
        } else if (entity == "lt") {
          result.push_back('<');
          i = semi;
          continue;
        } else if (entity == "gt") {
          result.push_back('>');
          i = semi;
          continue;
        } else if (entity == "quot") {
          result.push_back('"');
          i = semi;
          continue;
        } else if (entity == "apos") {
          result.push_back('\'');
          i = semi;
          continue;
        } else if (!entity.empty() && entity[0] == '#') {
          uint32_t cp = 0;
          bool valid = false;
          if (entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
            for (size_t k = 2; k < entity.size(); ++k) {
              char c = entity[k];
              if (c >= '0' && c <= '9') {
                cp = (cp << 4) | (c - '0');
              } else if (c >= 'a' && c <= 'f') {
                cp = (cp << 4) | (c - 'a' + 10);
              } else if (c >= 'A' && c <= 'F') {
                cp = (cp << 4) | (c - 'A' + 10);
              } else {
                valid = false;
                break;
              }
              valid = true;
            }
          } else if (entity.size() > 1) {
            for (size_t k = 1; k < entity.size(); ++k) {
              char c = entity[k];
              if (c >= '0' && c <= '9') {
                cp = cp * 10 + (c - '0');
              } else {
                valid = false;
                break;
              }
              valid = true;
            }
          }
          if (valid && cp > 0 && cp <= 0x10ffff) {
            if (cp < 0x80) {
              result.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
              result.push_back(static_cast<char>(0xc0 | (cp >> 6)));
              result.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            } else if (cp < 0x10000) {
              result.push_back(static_cast<char>(0xe0 | (cp >> 12)));
              result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
              result.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            } else {
              result.push_back(static_cast<char>(0xf0 | (cp >> 18)));
              result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
              result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
              result.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
            i = semi;
            continue;
          }
        }
      }
    }
    if (input[i] != '\0') {
      result.push_back(input[i]);
    }
  }
  return result;
}

std::optional<std::string> ExtractAttribute(std::string_view tag_str, std::string_view attr_name) {
  size_t pos = 0;
  while ((pos = tag_str.find(attr_name, pos)) != std::string_view::npos) {
    if (pos > 0 && tag_str[pos - 1] != ' ' && tag_str[pos - 1] != '\t' &&
        tag_str[pos - 1] != '\n' && tag_str[pos - 1] != '<') {
      pos += attr_name.size();
      continue;
    }
    size_t eq_pos = tag_str.find('=', pos + attr_name.size());
    if (eq_pos == std::string_view::npos) return std::nullopt;
    bool all_ws = true;
    for (size_t k = pos + attr_name.size(); k < eq_pos; ++k) {
      if (tag_str[k] != ' ' && tag_str[k] != '\t' && tag_str[k] != '\n') {
        all_ws = false;
        break;
      }
    }
    if (!all_ws) {
      pos += attr_name.size();
      continue;
    }
    size_t val_start = eq_pos + 1;
    while (val_start < tag_str.size() &&
           (tag_str[val_start] == ' ' || tag_str[val_start] == '\t' || tag_str[val_start] == '\n')) {
      val_start++;
    }
    if (val_start >= tag_str.size()) return std::nullopt;
    char quote = tag_str[val_start];
    if (quote == '"' || quote == '\'') {
      size_t val_end = tag_str.find(quote, val_start + 1);
      if (val_end == std::string_view::npos) return std::nullopt;
      return UnescapeXml(tag_str.substr(val_start + 1, val_end - val_start - 1));
    } else {
      size_t val_end = val_start;
      while (val_end < tag_str.size() && tag_str[val_end] != ' ' &&
             tag_str[val_end] != '\t' && tag_str[val_end] != '\n' &&
             tag_str[val_end] != '>' && tag_str[val_end] != '/') {
        val_end++;
      }
      return UnescapeXml(tag_str.substr(val_start, val_end - val_start));
    }
  }
  return std::nullopt;
}

bool ParseZipArchive(const uint8_t* data, size_t size, std::map<std::string, ZipEntry>* entries) {
  if (size < 22) return false;
  size_t max_search = std::min(size, static_cast<size_t>(65535 + 22));
  size_t search_start = size - max_search;
  int64_t eocd_offset = -1;
  for (int64_t i = static_cast<int64_t>(size) - 22; i >= static_cast<int64_t>(search_start); --i) {
    if (data[i] == 0x50 && data[i + 1] == 0x4b && data[i + 2] == 0x05 && data[i + 3] == 0x06) {
      eocd_offset = i;
      break;
    }
  }
  if (eocd_offset < 0) return false;

  const uint8_t* p = data + eocd_offset;
  uint16_t total_entries = p[10] | (p[11] << 8);
  uint32_t cd_size = p[12] | (p[13] << 8) | (p[14] << 16) | (p[15] << 24);
  uint32_t cd_offset = p[16] | (p[17] << 8) | (p[18] << 16) | (p[19] << 24);

  if (cd_offset + cd_size > static_cast<size_t>(eocd_offset)) return false;
  if (cd_offset >= size) return false;

  size_t cur = cd_offset;
  for (uint16_t i = 0; i < total_entries; ++i) {
    if (cur + 46 > size) return false;
    const uint8_t* ch = data + cur;
    if (ch[0] != 0x50 || ch[1] != 0x4b || ch[2] != 0x01 || ch[3] != 0x02) return false;
    uint16_t method = ch[10] | (ch[11] << 8);
    uint32_t comp_sz = ch[20] | (ch[21] << 8) | (ch[22] << 16) | (ch[23] << 24);
    uint32_t uncomp_sz = ch[24] | (ch[25] << 8) | (ch[26] << 16) | (ch[27] << 24);
    uint16_t fn_len = ch[28] | (ch[29] << 8);
    uint16_t extra_len = ch[30] | (ch[31] << 8);
    uint16_t comment_len = ch[32] | (ch[33] << 8);
    uint32_t local_hdr_off = ch[42] | (ch[43] << 8) | (ch[44] << 16) | (ch[45] << 24);

    cur += 46;
    if (cur + fn_len > size) return false;
    std::string fn(reinterpret_cast<const char*>(data + cur), fn_len);
    // Normalize path separators
    for (char& c : fn) {
      if (c == '\\') c = '/';
    }
    cur += fn_len + extra_len + comment_len;

    ZipEntry entry;
    entry.filename = fn;
    entry.method = method;
    entry.compressed_size = comp_sz;
    entry.uncompressed_size = uncomp_sz;
    entry.local_header_offset = local_hdr_off;
    (*entries)[ToLower(fn)] = entry;
  }
  return true;
}

bool ExtractZipEntry(const uint8_t* data, size_t size, const ZipEntry& entry, std::string* output) {
  if (entry.uncompressed_size > MahoArtifactXlsxProcessor::kMaxExtractedXmlBytes) {
    return false;
  }
  // `local_header_offset` is a uint32_t read straight from attacker-controlled
  // zip bytes. Widen before adding: `uint32_t + 30` is evaluated in 32-bit and
  // wraps (0xFFFFFFF8 + 30 == 22), passing this guard and leaving `lh` far
  // outside the buffer.
  const size_t header_offset = static_cast<size_t>(entry.local_header_offset);
  if (header_offset + 30 > size) return false;
  const uint8_t* lh = data + header_offset;
  if (lh[0] != 0x50 || lh[1] != 0x4b || lh[2] != 0x03 || lh[3] != 0x04) return false;
  uint16_t fn_len = lh[26] | (lh[27] << 8);
  uint16_t extra_len = lh[28] | (lh[29] << 8);
  size_t data_off = header_offset + 30 + fn_len + extra_len;
  if (data_off > size || size - data_off < entry.compressed_size) return false;

  if (entry.method == 0) {  // Store (no compression)
    output->assign(reinterpret_cast<const char*>(data + data_off), entry.compressed_size);
    return true;
  } else if (entry.method == 8) {  // Deflate
    output->resize(entry.uncompressed_size);
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    strm.next_in = const_cast<Bytef*>(data + data_off);
    strm.avail_in = static_cast<uInt>(entry.compressed_size);
    strm.next_out = reinterpret_cast<Bytef*>(&(*output)[0]);
    strm.avail_out = static_cast<uInt>(entry.uncompressed_size);
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return false;
    int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    return (ret == Z_STREAM_END || ret == Z_OK);
  }
  return false;
}

std::vector<std::string> ParseSharedStringsXml(std::string_view xml) {
  std::vector<std::string> strings;
  size_t pos = 0;
  while ((pos = xml.find("<si", pos)) != std::string_view::npos) {
    size_t end_si = xml.find("</si>", pos);
    if (end_si == std::string_view::npos) break;
    std::string_view si_block = xml.substr(pos, end_si + 5 - pos);
    pos = end_si + 5;

    std::string si_text;
    size_t t_pos = 0;
    while ((t_pos = si_block.find("<t", t_pos)) != std::string_view::npos) {
      size_t tag_close = si_block.find('>', t_pos);
      if (tag_close == std::string_view::npos) break;
      size_t end_t = si_block.find("</t>", tag_close);
      if (end_t == std::string_view::npos) break;
      std::string_view t_content = si_block.substr(tag_close + 1, end_t - tag_close - 1);
      si_text.append(UnescapeXml(t_content));
      t_pos = end_t + 4;
    }
    strings.push_back(std::move(si_text));
  }
  return strings;
}

struct SheetEntry {
  std::string name;
  std::string sheet_id;
  std::string r_id;
};

std::vector<SheetEntry> ParseWorkbookXml(std::string_view xml) {
  std::vector<SheetEntry> sheets;
  size_t pos = 0;
  while ((pos = xml.find("<sheet", pos)) != std::string_view::npos) {
    size_t after_tag = pos + 6;
    if (after_tag < xml.size() && xml[after_tag] != ' ' && xml[after_tag] != '\t' &&
        xml[after_tag] != '\n' && xml[after_tag] != '>') {
      pos = after_tag;
      continue;
    }
    size_t tag_end = xml.find('>', pos);
    if (tag_end == std::string_view::npos) break;
    std::string_view sheet_tag = xml.substr(pos, tag_end + 1 - pos);
    pos = tag_end + 1;

    SheetEntry info;
    info.name = ExtractAttribute(sheet_tag, "name").value_or("Sheet");
    info.sheet_id = ExtractAttribute(sheet_tag, "sheetId").value_or("");
    info.r_id = ExtractAttribute(sheet_tag, "r:id")
                    .value_or(ExtractAttribute(sheet_tag, "id").value_or(""));
    sheets.push_back(std::move(info));
  }
  return sheets;
}

bool ParseCellCoordinate(std::string_view ref, size_t* out_col, size_t* out_row) {
  if (ref.empty()) return false;
  size_t col = 0;
  size_t i = 0;
  while (i < ref.size() && ((ref[i] >= 'A' && ref[i] <= 'Z') || (ref[i] >= 'a' && ref[i] <= 'z'))) {
    char c = ref[i];
    if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
    col = col * 26 + (c - 'A' + 1);
    ++i;
  }
  if (i == 0 || col == 0 || i >= ref.size()) return false;
  col -= 1;  // 0-based

  size_t row = 0;
  while (i < ref.size() && ref[i] >= '0' && ref[i] <= '9') {
    row = row * 10 + (ref[i] - '0');
    ++i;
  }
  if (row == 0 || i != ref.size()) return false;
  row -= 1;  // 0-based

  *out_col = col;
  *out_row = row;
  return true;
}

std::vector<std::vector<std::string>> ParseWorksheetXml(
    std::string_view xml, const std::vector<std::string>& shared_strings) {
  std::vector<std::vector<std::string>> grid;
  size_t pos = 0;
  size_t current_row_idx = 0;

  while ((pos = xml.find("<c", pos)) != std::string_view::npos) {
    if (pos + 2 < xml.size() && xml[pos + 2] != ' ' && xml[pos + 2] != '\t' &&
        xml[pos + 2] != '\n' && xml[pos + 2] != '>' && xml[pos + 2] != '/') {
      pos += 2;
      continue;
    }
    size_t tag_end = xml.find('>', pos);
    if (tag_end == std::string_view::npos) break;
    std::string_view open_tag = xml.substr(pos, tag_end + 1 - pos);

    bool is_self_closing = (open_tag.size() >= 2 && open_tag[open_tag.size() - 2] == '/');
    size_t c_end = is_self_closing ? tag_end + 1 : xml.find("</c>", tag_end);
    if (c_end == std::string_view::npos) {
      c_end = tag_end + 1;
    } else if (!is_self_closing) {
      c_end += 4;
    }
    std::string_view c_block = xml.substr(pos, c_end - pos);
    pos = c_end;

    std::optional<std::string> ref_str = ExtractAttribute(open_tag, "r");
    std::optional<std::string> type_str = ExtractAttribute(open_tag, "t");

    size_t col_idx = 0;
    size_t row_idx = current_row_idx;
    if (ref_str && ParseCellCoordinate(*ref_str, &col_idx, &row_idx)) {
      current_row_idx = row_idx;
    }

    if (row_idx >= MahoArtifactXlsxProcessor::kMaxRowsPerSheet ||
        col_idx >= MahoArtifactXlsxProcessor::kMaxColsPerSheet) {
      continue;
    }

    std::string cell_val;
    if (type_str == "s") {
      size_t v_start = c_block.find("<v>");
      if (v_start != std::string_view::npos) {
        size_t v_end = c_block.find("</v>", v_start + 3);
        if (v_end != std::string_view::npos) {
          std::string_view idx_str = c_block.substr(v_start + 3, v_end - v_start - 3);
          size_t sst_idx = 0;
          bool parse_ok = true;
          for (char c : idx_str) {
            if (c >= '0' && c <= '9') {
              sst_idx = sst_idx * 10 + (c - '0');
            } else {
              parse_ok = false;
              break;
            }
          }
          if (parse_ok && sst_idx < shared_strings.size()) {
            cell_val = shared_strings[sst_idx];
          }
        }
      }
    } else if (type_str == "inlineStr" || type_str == "is") {
      size_t t_start = c_block.find("<t");
      if (t_start != std::string_view::npos) {
        size_t t_tag_close = c_block.find('>', t_start);
        if (t_tag_close != std::string_view::npos) {
          size_t t_end = c_block.find("</t>", t_tag_close);
          if (t_end != std::string_view::npos) {
            cell_val = UnescapeXml(c_block.substr(t_tag_close + 1, t_end - t_tag_close - 1));
          }
        }
      }
    } else if (type_str == "b") {
      size_t v_start = c_block.find("<v>");
      if (v_start != std::string_view::npos) {
        size_t v_end = c_block.find("</v>", v_start + 3);
        if (v_end != std::string_view::npos) {
          std::string_view b_str = c_block.substr(v_start + 3, v_end - v_start - 3);
          cell_val = (b_str == "1" || b_str == "true") ? "TRUE" : "FALSE";
        }
      }
    } else {
      size_t v_start = c_block.find("<v>");
      if (v_start != std::string_view::npos) {
        size_t v_end = c_block.find("</v>", v_start + 3);
        if (v_end != std::string_view::npos) {
          cell_val = UnescapeXml(c_block.substr(v_start + 3, v_end - v_start - 3));
        }
      }
    }

    if (grid.size() <= row_idx) {
      grid.resize(row_idx + 1);
    }
    if (grid[row_idx].size() <= col_idx) {
      grid[row_idx].resize(col_idx + 1);
    }
    grid[row_idx][col_idx] = std::move(cell_val);
  }

  // Normalize rectangular grid width
  size_t max_cols = 0;
  for (const auto& row : grid) {
    if (row.size() > max_cols) max_cols = row.size();
  }
  for (auto& row : grid) {
    row.resize(max_cols);
  }

  return grid;
}

std::vector<std::string> ParseCsvLine(std::string_view line, char delimiter) {
  std::vector<std::string> fields;
  std::string current;
  bool in_quotes = false;
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          current.push_back('"');
          ++i;
        } else {
          in_quotes = false;
        }
      } else {
        current.push_back(c);
      }
    } else {
      if (c == '"') {
        in_quotes = true;
      } else if (c == delimiter) {
        fields.push_back(std::move(current));
        current.clear();
      } else {
        current.push_back(c);
      }
    }
  }
  fields.push_back(std::move(current));
  return fields;
}

bool IsPrintableText(std::string_view text) {
  if (text.empty()) return false;
  for (char c : text) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x20 && uc != '\r' && uc != '\n' && uc != '\t') {
      return false;
    }
  }
  return true;
}

MahoXlsxModel ParseCsvFallback(std::string_view text) {
  char delimiter = ',';
  if (text.find('\t') != std::string_view::npos && text.find(',') == std::string_view::npos) {
    delimiter = '\t';
  }

  std::vector<std::vector<std::string>> grid;
  size_t pos = 0;
  while (pos < text.size() && grid.size() < MahoArtifactXlsxProcessor::kMaxRowsPerSheet) {
    size_t line_end = text.find('\n', pos);
    std::string_view line = (line_end == std::string_view::npos)
                                ? text.substr(pos)
                                : text.substr(pos, line_end - pos);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    grid.push_back(ParseCsvLine(line, delimiter));
    if (line_end == std::string_view::npos) break;
    pos = line_end + 1;
  }

  // Normalize rectangular grid width
  size_t max_cols = 0;
  for (const auto& row : grid) {
    if (row.size() > max_cols) max_cols = row.size();
  }
  for (auto& row : grid) {
    row.resize(max_cols);
  }

  MahoXlsxSheet sheet;
  sheet.name = "Sheet1";
  sheet.cells = std::move(grid);

  MahoXlsxModel model;
  model.sheets.push_back(std::move(sheet));
  return model;
}

}  // namespace

std::string MahoXlsxModel::ToJson() const {
  std::string json;
  json.append("{\"sheets\":[");
  for (size_t s = 0; s < sheets.size(); ++s) {
    if (s > 0) json.push_back(',');
    json.append("{\"name\":");
    json.append(EscapeJsonString(sheets[s].name));
    json.append(",\"cells\":[");
    for (size_t r = 0; r < sheets[s].cells.size(); ++r) {
      if (r > 0) json.push_back(',');
      json.push_back('[');
      for (size_t c = 0; c < sheets[s].cells[r].size(); ++c) {
        if (c > 0) json.push_back(',');
        json.append(EscapeJsonString(sheets[s].cells[r][c]));
      }
      json.push_back(']');
    }
    json.append("]}");
  }
  json.append("]}");
  return json;
}

base::expected<MahoXlsxModel, std::string> MahoXlsxModel::FromJson(std::string_view json_str) {
  // Minimal structured JSON parser for MahoXlsxModel format
  if (json_str.empty()) {
    return base::unexpected(std::string("Empty JSON input"));
  }
  // Fast check and simple token walk for sheets model
  size_t sheets_pos = json_str.find("\"sheets\"");
  if (sheets_pos == std::string_view::npos) {
    return base::unexpected(std::string("Missing sheets array in JSON"));
  }

  MahoXlsxModel model;
  size_t pos = sheets_pos;
  while ((pos = json_str.find("\"name\"", pos)) != std::string_view::npos) {
    size_t colon = json_str.find(':', pos);
    if (colon == std::string_view::npos) break;
    size_t q1 = json_str.find('"', colon);
    if (q1 == std::string_view::npos) break;
    size_t q2 = json_str.find('"', q1 + 1);
    if (q2 == std::string_view::npos) break;
    std::string name(json_str.substr(q1 + 1, q2 - q1 - 1));

    size_t cells_pos = json_str.find("\"cells\"", q2);
    if (cells_pos == std::string_view::npos) break;
    size_t bracket1 = json_str.find('[', cells_pos);
    if (bracket1 == std::string_view::npos) break;

    MahoXlsxSheet sheet;
    sheet.name = std::move(name);

    size_t row_pos = bracket1 + 1;
    while ((row_pos = json_str.find('[', row_pos)) != std::string_view::npos) {
      size_t row_end = json_str.find(']', row_pos);
      if (row_end == std::string_view::npos) break;
      std::string_view row_str = json_str.substr(row_pos + 1, row_end - row_pos - 1);
      std::vector<std::string> row_cells;

      size_t str_pos = 0;
      while ((str_pos = row_str.find('"', str_pos)) != std::string_view::npos) {
        size_t str_end = str_pos + 1;
        while (str_end < row_str.size() && (row_str[str_end] != '"' || row_str[str_end - 1] == '\\')) {
          str_end++;
        }
        if (str_end >= row_str.size()) break;
        row_cells.push_back(std::string(row_str.substr(str_pos + 1, str_end - str_pos - 1)));
        str_pos = str_end + 1;
      }
      sheet.cells.push_back(std::move(row_cells));
      row_pos = row_end + 1;
      // If closing sheets array
      size_t next_bracket = json_str.find_first_of("[]", row_pos);
      if (next_bracket != std::string_view::npos && json_str[next_bracket] == ']') {
        break;
      }
    }
    model.sheets.push_back(std::move(sheet));
    pos = row_pos;
  }

  return model;
}

base::expected<MahoXlsxModel, std::string> MahoArtifactXlsxProcessor::ProcessBytes(
    const uint8_t* data, size_t size) {
  if (!data || size == 0) {
    return base::unexpected(std::string("Input XLSX data is empty"));
  }
  if (size > kMaxXlsxBytes) {
    return base::unexpected(std::string("XLSX data exceeds size limit (32 MiB)"));
  }

  std::map<std::string, ZipEntry> entries;
  if (ParseZipArchive(data, size, &entries)) {
    // Look up workbook and shared strings
    std::string wb_xml, sst_xml;
    auto wb_it = entries.find("xl/workbook.xml");
    if (wb_it == entries.end()) {
      return base::unexpected(std::string("Missing xl/workbook.xml in XLSX archive"));
    }
    if (!ExtractZipEntry(data, size, wb_it->second, &wb_xml)) {
      return base::unexpected(std::string("Failed to decompress xl/workbook.xml"));
    }

    std::vector<std::string> shared_strings;
    auto sst_it = entries.find("xl/sharedstrings.xml");
    if (sst_it != entries.end()) {
      if (ExtractZipEntry(data, size, sst_it->second, &sst_xml)) {
        shared_strings = ParseSharedStringsXml(sst_xml);
      }
    }

    std::vector<SheetEntry> sheet_entries = ParseWorkbookXml(wb_xml);
    MahoXlsxModel model;

    for (size_t i = 0; i < sheet_entries.size() && model.sheets.size() < kMaxSheets; ++i) {
      // Find matching worksheet file, e.g. xl/worksheets/sheet1.xml
      std::string sheet_filename = "xl/worksheets/sheet" + std::to_string(i + 1) + ".xml";
      auto ws_it = entries.find(sheet_filename);
      if (ws_it == entries.end()) {
        // Search any worksheet in entries
        for (const auto& [name, entry] : entries) {
          if (name.find("xl/worksheets/sheet") != std::string_view::npos) {
            ws_it = entries.find(name);
            break;
          }
        }
      }

      if (ws_it != entries.end()) {
        std::string ws_xml;
        if (ExtractZipEntry(data, size, ws_it->second, &ws_xml)) {
          MahoXlsxSheet sheet;
          sheet.name = sheet_entries[i].name;
          sheet.cells = ParseWorksheetXml(ws_xml, shared_strings);
          model.sheets.push_back(std::move(sheet));
        }
      }
    }

    if (!model.sheets.empty()) {
      return model;
    }
    return base::unexpected(std::string("No valid worksheets found in XLSX archive"));
  }

  // Check if it looks like a corrupted ZIP header
  if (size >= 4 && data[0] == 0x50 && data[1] == 0x4b) {
    return base::unexpected(std::string("Corrupted or truncated XLSX zip archive"));
  }

  // Fallback: Check if printable text / CSV
  std::string_view text_view(reinterpret_cast<const char*>(data), size);
  if (IsPrintableText(text_view)) {
    return ParseCsvFallback(text_view);
  }

  return base::unexpected(std::string("Invalid XLSX format: neither a valid spreadsheet archive nor CSV"));
}

base::expected<MahoXlsxModel, std::string> MahoArtifactXlsxProcessor::Process(
    std::string_view raw_data) {
  return ProcessBytes(reinterpret_cast<const uint8_t*>(raw_data.data()), raw_data.size());
}

base::expected<std::string, std::string> MahoArtifactXlsxProcessor::GeneratePreviewJson(
    std::string_view raw_data) {
  auto model_res = Process(raw_data);
  if (!model_res.has_value()) {
    return base::unexpected(std::string(model_res.error()));
  }
  return base::ok(model_res->ToJson());
}

}  // namespace maho::ai
