// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/maho_space_ffi_client.h"

#include <algorithm>
#include <cmath>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"  // nogncheck
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {

namespace {

std::string ValidateColorHex(const std::string& hex) {
  if (hex.empty()) {
    return "#3b82f6"; // Default blue
  }
  std::string clean = hex;
  if (clean[0] == '#') clean = clean.substr(1);
  if (clean.size() != 3 && clean.size() != 6 && clean.size() != 8) {
    return "#3b82f6";
  }
  for (char c : clean) {
    if (!base::IsHexDigit(c)) {
      return "#3b82f6";
    }
  }
  return hex[0] == '#' ? hex : "#" + hex;
}

std::string HexToMahoColorJson(const std::string& hex) {
  std::string clean = ValidateColorHex(hex);
  if (clean[0] == '#') clean = clean.substr(1);

  uint32_t rgb = 0;
  if (!base::HexStringToUInt(clean.substr(0, 6), &rgb)) {
    return R"({"hue":220,"saturation":0.6,"brightness":0.85,"grain":0.0})";
  }
  unsigned int r = (rgb >> 16) & 0xFF;
  unsigned int g = (rgb >> 8) & 0xFF;
  unsigned int b = rgb & 0xFF;

  double rd = r / 255.0, gd = g / 255.0, bd = b / 255.0;
  double max_c = std::max({rd, gd, bd});
  double min_c = std::min({rd, gd, bd});
  double delta = max_c - min_c;

  double hue = 0;
  if (delta > 0.001) {
    if (max_c == rd) hue = 60.0 * fmod((gd - bd) / delta, 6.0);
    else if (max_c == gd) hue = 60.0 * ((bd - rd) / delta + 2.0);
    else hue = 60.0 * ((rd - gd) / delta + 4.0);
  }
  if (hue < 0) hue += 360.0;

  double sat = (max_c > 0.001) ? (delta / max_c) : 0.0;
  double bri = max_c;

  base::DictValue color;
  color.Set("hue", static_cast<int>(hue));
  color.Set("saturation", sat);
  color.Set("brightness", bri);
  color.Set("grain", 0.0);

  std::string result;
  base::JSONWriter::Write(color, &result);
  return result;
}

std::string ExtractTabIdFromResponse(const std::string& response) {
  auto parsed = base::JSONReader::Read(response, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return std::string();

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    const std::string* kind = dict->FindString("kind");
    if (!kind || *kind != "tab_created") continue;
    const auto* tab = dict->FindDict("tab");
    if (!tab) continue;
    const std::string* id = tab->FindString("id");
    if (id && !id->empty()) return *id;
  }
  return std::string();
}

}  // namespace

namespace {
MahoSpaceFFIClient* g_test_instance = nullptr;
}  // namespace

// static
MahoSpaceFFIClient* MahoSpaceFFIClient::GetInstance() {
  if (g_test_instance) {
    return g_test_instance;
  }
  static base::NoDestructor<MahoSpaceFFIClient> instance;
  return instance.get();
}

// static
void MahoSpaceFFIClient::SetInstanceForTesting(MahoSpaceFFIClient* instance) {
  g_test_instance = instance;
}

MahoSpaceFFIClient::MahoSpaceFFIClient() = default;

MahoSpaceFFIClient::~MahoSpaceFFIClient() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

std::vector<int> MahoSpaceFFIClient::GetRecordedFolderDepths() const {
  std::vector<int> depths;
  depths.reserve(folder_depths_.size());
  for (const auto& kv : folder_depths_) {
    depths.push_back(kv.second);
  }
  return depths;
}

std::string MahoSpaceFFIClient::CreateSpace(const std::string& name,
                                            const std::string& theme_color_hex,
                                            const std::string& icon) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core) return "";

  std::string color_json = HexToMahoColorJson(theme_color_hex);
  // The 4th argument is the owning profile id, not an icon ("" = default
  // profile, as the Rust import destination uses). The icon goes through the
  // core's update_space_config seam below, which canonicalizes it.
  char* svm = maho_core_create_space(core, name.c_str(), color_json.c_str(),
                                     /*profile_id=*/"");
  if (!svm) return "";

  std::string svm_json(svm);
  maho_string_free(svm);
  SidebarCacheInvalidation invalidation;
  invalidation.fragments = SidebarCoreFragment::kFooter;
  InvalidateSidebarCoreCache(invalidation);

  auto svm_parsed = base::JSONReader::Read(svm_json, base::JSON_PARSE_RFC);
  if (!svm_parsed || !svm_parsed->is_dict()) return "";
  const std::string* space_id = svm_parsed->GetDict().FindString("id");
  if (!space_id) {
    return "";
  }
  if (!icon.empty()) {
    SetSpaceIcon(*space_id, icon);
  }
  return *space_id;
}

std::string MahoSpaceFFIClient::CreateTab(const std::string& space_id,
                                          const std::string& url,
                                          const std::string& title,
                                          const std::string& parent_folder_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::GetCore()) return "";

  base::DictValue event;
  event.Set("kind", "create_tab");
  event.Set("space_id", space_id);
  event.Set("url", url);
  if (!title.empty()) {
    event.Set("title", title);
  }
  if (parent_folder_id.empty()) {
    event.Set("parent_id", base::Value());
  } else {
    event.Set("parent_id", parent_folder_id);
  }

  std::string response = DispatchEvent(event);
  return ExtractTabIdFromResponse(response);
}

void MahoSpaceFFIClient::PinTab(const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::GetCore()) return;
  base::DictValue event;
  event.Set("kind", "pin_tab");
  event.Set("tab_id", tab_id);
  DispatchEvent(event);
}

void MahoSpaceFFIClient::FavoriteTab(const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::GetCore()) return;
  base::DictValue event;
  event.Set("kind", "favorite_tab");
  event.Set("tab_id", tab_id);
  DispatchEvent(event);
}

std::string MahoSpaceFFIClient::CreateFolder(const std::string& space_id,
                                             const std::string& name,
                                             const std::string& parent_folder_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::GetCore()) return "";

  std::string target_parent_id = parent_folder_id;
  if (!target_parent_id.empty()) {
    int parent_depth = 1;
    auto it = folder_depths_.find(target_parent_id);
    if (it != folder_depths_.end()) {
      parent_depth = it->second;
    }
    if (parent_depth >= 5) {
      return target_parent_id; // Flatten to depth-5 parent
    }
  }

  base::DictValue event;
  event.Set("kind", "create_folder");
  event.Set("space_id", space_id);
  event.Set("name", name);
  if (target_parent_id.empty()) {
    event.Set("parent_id", base::Value());
  } else {
    event.Set("parent_id", target_parent_id);
  }

  std::string response = DispatchEvent(event);
  auto parsed = base::JSONReader::Read(response, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return std::string();
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    const std::string* kind = dict->FindString("kind");
    if (!kind || *kind != "folder_created") continue;
    const auto* folder = dict->FindDict("folder");
    if (!folder) continue;
    const std::string* id = folder->FindString("id");
    if (id && !id->empty()) {
      int new_depth = target_parent_id.empty() ? 1 : folder_depths_[target_parent_id] + 1;
      folder_depths_[*id] = new_depth;
      return *id;
    }
  }
  return std::string();
}

void MahoSpaceFFIClient::MoveTabToFolder(const std::string& tab_id,
                                         const std::string& folder_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::GetCore()) return;
  base::DictValue event;
  event.Set("kind", "move_tab_to_folder");
  event.Set("tab_id", tab_id);
  event.Set("folder_id", folder_id);
  DispatchEvent(event);
}

void MahoSpaceFFIClient::UpdateSpaceConfig(const std::string& space_id,
                                           const std::string& changes_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core) return;

  std::optional<base::Value> changes_value =
      base::JSONReader::Read(changes_json, base::JSON_PARSE_RFC);
  if (!changes_value || !changes_value->is_dict()) return;

  base::DictValue changes = std::move(changes_value->GetDict());
  changes.Set("spaceId", space_id);

  base::DictValue event;
  event.Set("kind", "update_space_config");
  event.Set("changes", std::move(changes));

  std::string event_json;
  base::JSONWriter::Write(event, &event_json);
  char* result = maho_core_handle_event(core, event_json.c_str());
  if (result) {
    std::string updates(result);
    maho_string_free(result);
    InvalidateSidebarCoreCacheForUpdatesJson(updates);
  }
}

void MahoSpaceFFIClient::ActivateSpace(const std::string& space_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_activate_space(core, space_id.c_str());
  SidebarCacheInvalidation invalidation;
  invalidation.fragments = SidebarCoreFragment::kFooter;
  InvalidateSidebarCoreCache(invalidation);
}

void MahoSpaceFFIClient::SetSpaceIcon(const std::string& space_id,
                                      const std::string& icon) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::GetCore()) return;
  base::DictValue changes;
  changes.Set("spaceId", space_id);
  changes.Set("icon", icon);
  base::DictValue event;
  event.Set("kind", "update_space_config");
  event.Set("changes", std::move(changes));
  DispatchEvent(event);
}

void MahoSpaceFFIClient::MoveTabToSpace(const std::string& tab_id,
                                         const std::string& target_space_id,
                                         const std::string& section) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::GetCore()) return;
  base::DictValue event;
  event.Set("kind", "move_tab_to_space");
  event.Set("tab_id", tab_id);
  event.Set("target_space_id", target_space_id);
  if (!section.empty()) {
    event.Set("section", section);
  }
  DispatchEvent(event);
}

std::string MahoSpaceFFIClient::DispatchEvent(const base::DictValue& event) {
  std::string json;
  base::JSONWriter::Write(event, &json);
  MahoCore* core = maho::GetCore();
  if (!core) return std::string();
  char* result = maho_core_handle_event(core, json.c_str());
  if (!result) return std::string();
  std::string out(result);
  maho_string_free(result);
  InvalidateSidebarCoreCacheForUpdatesJson(out);
  return out;
}

}  // namespace maho
