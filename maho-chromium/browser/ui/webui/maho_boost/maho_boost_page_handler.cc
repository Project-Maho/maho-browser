// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_boost/maho_boost_page_handler.h"

#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/font_list_async.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/net/maho_boost_injection_handler.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller_bridge.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace {

constexpr size_t kMaxBoostIdBytes = 256;
constexpr size_t kMaxBoostNameBytes = 256;
constexpr size_t kMaxFontFamilyBytes = 256;
constexpr size_t kMaxBoostImportBytes = 512 * 1024;
constexpr size_t kMaxCustomCssBytes = 256 * 1024;
constexpr size_t kMaxZapSelectors = 256;
constexpr size_t kMaxZapSelectorBytes = 2 * 1024;

bool IsOversizedBoostUpdate(const maho_boost::mojom::BoostUpdate& changes) {
  return (changes.name.has_value() &&
          changes.name->size() > kMaxBoostNameBytes) ||
         (changes.custom_css.has_value() &&
          changes.custom_css->size() > kMaxCustomCssBytes) ||
         (changes.typography &&
          changes.typography->set_font_family &&
          changes.typography->font_family.size() > kMaxFontFamilyBytes) ||
         (changes.zap_selectors.has_value() &&
          (changes.zap_selectors->size() > kMaxZapSelectors ||
           std::ranges::any_of(
               *changes.zap_selectors, [](const std::string& selector) {
                 return selector.size() > kMaxZapSelectorBytes;
               })));
}

class ScopedFfiSerialization {
 public:
  ScopedFfiSerialization() : scope_(maho_ffi_serialization_scope_enter()) {}
  ~ScopedFfiSerialization() { maho_ffi_serialization_scope_exit(scope_); }

  bool acquired() const { return scope_ != nullptr; }

 private:
  void* scope_;
};

maho_boost::mojom::CaseMode ParseCaseMode(const std::string& s) {
  // Accept both lowercase (from Rust serde rename_all = "camelCase") and
  // PascalCase (legacy) for robustness.
  if (s == "upper" || s == "Upper")
    return maho_boost::mojom::CaseMode::kUpper;
  if (s == "lower" || s == "Lower")
    return maho_boost::mojom::CaseMode::kLower;
  if (s == "capitalize" || s == "Capitalize")
    return maho_boost::mojom::CaseMode::kCapitalize;
  return maho_boost::mojom::CaseMode::kNone;
}

// Must match Rust serde rename_all = "camelCase" for CaseMode variants.
const char* CaseModeToString(maho_boost::mojom::CaseMode cm) {
  switch (cm) {
    case maho_boost::mojom::CaseMode::kUpper:      return "upper";
    case maho_boost::mojom::CaseMode::kLower:      return "lower";
    case maho_boost::mojom::CaseMode::kCapitalize: return "capitalize";
    case maho_boost::mojom::CaseMode::kNone:       return "none";
  }
  return "none";
}

maho_boost::mojom::SizeMode ParseSizeMode(const std::string& s) {
  if (s == "k90")  return maho_boost::mojom::SizeMode::k90;
  if (s == "k110") return maho_boost::mojom::SizeMode::k110;
  if (s == "k125") return maho_boost::mojom::SizeMode::k125;
  if (s == "k150") return maho_boost::mojom::SizeMode::k150;
  return maho_boost::mojom::SizeMode::k100;
}

const char* SizeModeToString(maho_boost::mojom::SizeMode sm) {
  switch (sm) {
    case maho_boost::mojom::SizeMode::k90:  return "k90";
    case maho_boost::mojom::SizeMode::k100: return "k100";
    case maho_boost::mojom::SizeMode::k110: return "k110";
    case maho_boost::mojom::SizeMode::k125: return "k125";
    case maho_boost::mojom::SizeMode::k150: return "k150";
  }
  return "k100";
}

maho_boost::mojom::PointPtr ParsePoint(const base::DictValue& d,
                                       float default_x,
                                       float default_y) {
  auto p = maho_boost::mojom::Point::New();
  p->x = static_cast<float>(d.FindDouble("x").value_or(default_x));
  p->y = static_cast<float>(d.FindDouble("y").value_or(default_y));
  return p;
}

maho_boost::mojom::ColorBoostPtr ParseColorBoost(const base::DictValue& d) {
  auto c = maho_boost::mojom::ColorBoost::New();

  if (const base::DictValue* dp = d.FindDict("dotPos")) {
    c->dot_pos = ParsePoint(*dp, 0.76f, 0.66f);
  } else {
    c->dot_pos = maho_boost::mojom::Point::New();
    c->dot_pos->x = 0.76f;
    c->dot_pos->y = 0.66f;
  }
  c->dot_distance =
      static_cast<float>(d.FindDouble("dotDistance").value_or(0.0));
  c->dot_angle_deg =
      static_cast<float>(d.FindDouble("dotAngleDeg").value_or(0.0));

  if (const base::DictValue* sp = d.FindDict("secondaryDotPos")) {
    c->secondary_dot_pos = ParsePoint(*sp, 0.5f, 0.81f);
  } else {
    c->secondary_dot_pos = maho_boost::mojom::Point::New();
    c->secondary_dot_pos->x = 0.5f;
    c->secondary_dot_pos->y = 0.81f;
  }
  c->secondary_dot_angle_deg_delta =
      static_cast<float>(d.FindDouble("secondaryDotAngleDegDelta").value_or(55.0));

  c->magic_theme = d.FindBool("magicTheme").value_or(false);
  c->color_boost_enabled = d.FindBool("colorBoostEnabled").value_or(false);
  c->smart_invert = d.FindBool("smartInvert").value_or(false);
  c->contrast    = static_cast<float>(d.FindDouble("contrast").value_or(0.75));
  c->brightness  = static_cast<float>(d.FindDouble("brightness").value_or(0.5));
  c->saturation  = static_cast<float>(d.FindDouble("saturation").value_or(0.5));
  return c;
}

maho_boost::mojom::TypographyBoostPtr ParseTypographyBoost(
    const base::DictValue& d) {
  auto t = maho_boost::mojom::TypographyBoost::New();
  if (const std::string* ff = d.FindString("fontFamily")) {
    t->font_family = *ff;
  }
  if (const std::string* cm = d.FindString("caseMode")) {
    t->case_mode = ParseCaseMode(*cm);
  } else {
    t->case_mode = maho_boost::mojom::CaseMode::kNone;
  }
  if (const std::string* sm = d.FindString("sizeMode")) {
    t->size_mode = ParseSizeMode(*sm);
  } else {
    t->size_mode = maho_boost::mojom::SizeMode::k100;
  }
  return t;
}

maho_boost::mojom::BoostInfoPtr ParseBoostFromDict(
    const base::DictValue& dict) {
  const std::string* id = dict.FindString("id");
  if (!id) {
    return nullptr;
  }
  auto info = maho_boost::mojom::BoostInfo::New();
  info->id = *id;
  info->name   = dict.FindString("name")   ? *dict.FindString("name")   : "";
  info->domain = dict.FindString("domain") ? *dict.FindString("domain") : "";
  info->custom_css =
      dict.FindString("customCss") ? *dict.FindString("customCss") : "";
  info->change_was_made = dict.FindBool("changeWasMade").value_or(false);

  if (const base::DictValue* cd = dict.FindDict("color")) {
    info->color = ParseColorBoost(*cd);
  } else {
    info->color = maho_boost::mojom::ColorBoost::New();
    info->color->dot_pos = maho_boost::mojom::Point::New();
    info->color->dot_pos->x = 0.76f;
    info->color->dot_pos->y = 0.66f;
    info->color->secondary_dot_pos = maho_boost::mojom::Point::New();
    info->color->secondary_dot_pos->x = 0.5f;
    info->color->secondary_dot_pos->y = 0.81f;
  }

  if (const base::DictValue* td = dict.FindDict("typography")) {
    info->typography = ParseTypographyBoost(*td);
  } else {
    info->typography = maho_boost::mojom::TypographyBoost::New();
  }

  info->zap_selectors.clear();
  if (const base::ListValue* zl = dict.FindList("zapSelectors")) {
    for (const auto& v : *zl) {
      if (const std::string* s = v.GetIfString()) {
        info->zap_selectors.push_back(*s);
      }
    }
  }

  return info;
}

maho_boost::mojom::BoostInfoPtr ParseSingleBoostJson(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return nullptr;
  }
  return ParseBoostFromDict(parsed->GetDict());
}

bool BoostBelongsToDomain(const std::string& domain,
                          const std::string& boost_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  char* json_str = maho_core_boost_get(core, boost_id.c_str());
  if (!json_str) {
    return false;
  }
  std::string json(json_str);
  maho_string_free(json_str);
  auto boost = ParseSingleBoostJson(json);
  return boost && boost->domain == domain;
}

std::string SerializeBoostUpdate(
    const maho_boost::mojom::BoostUpdatePtr& changes) {
  base::DictValue root;

  if (changes->name.has_value()) {
    root.Set("name", *changes->name);
  }

  if (changes->color) {
    const auto& cu = *changes->color;
    base::DictValue cd;
    if (cu.color_boost_enabled.has_value()) {
      cd.Set("colorBoostEnabled", *cu.color_boost_enabled);
    }
    if (cu.dot_angle_deg.has_value()) {
      cd.Set("dotAngleDeg", static_cast<double>(*cu.dot_angle_deg));
    }
    if (cu.secondary_dot_angle_deg_delta.has_value()) {
      cd.Set("secondaryDotAngleDegDelta",
             static_cast<double>(*cu.secondary_dot_angle_deg_delta));
    }
    if (cu.brightness.has_value()) {
      cd.Set("brightness", static_cast<double>(*cu.brightness));
    }
    if (cu.saturation.has_value()) {
      cd.Set("saturation", static_cast<double>(*cu.saturation));
    }
    if (cu.contrast.has_value()) {
      cd.Set("contrast", static_cast<double>(*cu.contrast));
    }
    if (cu.magic_theme.has_value()) {
      cd.Set("magicTheme", *cu.magic_theme);
    }
    if (cu.smart_invert.has_value()) {
      cd.Set("smartInvert", *cu.smart_invert);
    }
    if (cu.dot_pos) {
      base::DictValue dp;
      dp.Set("x", static_cast<double>(cu.dot_pos->x));
      dp.Set("y", static_cast<double>(cu.dot_pos->y));
      cd.Set("dotPos", base::Value(std::move(dp)));
    }
    if (cu.dot_distance.has_value()) {
      cd.Set("dotDistance", static_cast<double>(*cu.dot_distance));
    }
    if (cu.secondary_dot_pos) {
      base::DictValue sp;
      sp.Set("x", static_cast<double>(cu.secondary_dot_pos->x));
      sp.Set("y", static_cast<double>(cu.secondary_dot_pos->y));
      cd.Set("secondaryDotPos", base::Value(std::move(sp)));
    }
    root.Set("color", base::Value(std::move(cd)));
  }

  if (changes->typography) {
    const auto& tu = *changes->typography;
    base::DictValue td;

    if (tu.set_font_family) {
      td.Set("fontFamily", tu.font_family);
    }

    if (tu.case_mode.has_value()) {
      td.Set("caseMode", CaseModeToString(*tu.case_mode));
    }

    if (tu.set_size_mode) {
      td.Set("sizeMode", SizeModeToString(tu.size_mode));
    }

    root.Set("typography", base::Value(std::move(td)));
  }

  if (changes->zap_selectors.has_value()) {
    base::ListValue zl;
    for (const auto& s : *changes->zap_selectors) {
      zl.Append(s);
    }
    root.Set("zapSelectors", base::Value(std::move(zl)));
  }

  if (changes->custom_css.has_value()) {
    root.Set("customCss", *changes->custom_css);
  }

  std::string out;
  base::JSONWriter::Write(base::Value(std::move(root)), &out);
  return out;
}

std::vector<maho_boost::mojom::BoostInfoPtr> ListBoostsOnCoreSequence(
    const std::string& domain) {
  std::vector<maho_boost::mojom::BoostInfoPtr> result;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return result;
  }
  char* json_str = maho_core_boost_list_for_domain(core, domain.c_str());
  if (!json_str) {
    return result;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return result;
  }
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    auto info = ParseBoostFromDict(*dict);
    if (info) {
      result.push_back(std::move(info));
    }
  }
  return result;
}

maho_boost::mojom::BoostInfoPtr GetBoostOnCoreSequence(
    const std::string& domain,
    const std::string& boost_id) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return nullptr;
  }
  char* json_str = maho_core_boost_get(core, boost_id.c_str());
  if (!json_str) {
    return nullptr;
  }
  std::string json(json_str);
  maho_string_free(json_str);
  return ParseSingleBoostJson(json);
}

maho_boost::mojom::BoostInfoPtr CreateTempBoostOnCoreSequence(
    const std::string& domain) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return nullptr;
  }
  std::string json = maho::core::CreateTempBoost(core, domain.c_str());
  if (json.empty()) {
    return nullptr;
  }
  return ParseSingleBoostJson(json);
}

maho_boost::mojom::BoostInfoPtr CommitBoostOnCoreSequence(
    const std::string& domain,
    const std::string& boost_id) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return nullptr;
  }
  std::string json = maho::core::CommitBoost(core, boost_id.c_str());
  if (json.empty()) {
    return nullptr;
  }
  return ParseSingleBoostJson(json);
}

std::string DiscardBoostOnCoreSequence(const std::string& domain,
                                       const std::string& boost_id) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return std::string();
  }
  return maho::core::DiscardBoost(core, boost_id.c_str());
}

maho_boost::mojom::BoostInfoPtr ShuffleBoostOnCoreSequence(
    const std::string& domain,
    const std::string& boost_id) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return nullptr;
  }
  std::string json = maho::core::ShuffleBoost(core, boost_id.c_str());
  if (json.empty()) {
    return nullptr;
  }
  return ParseSingleBoostJson(json);
}

maho_boost::mojom::BoostInfoPtr ResetBoostOnCoreSequence(
    const std::string& domain,
    const std::string& boost_id) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return nullptr;
  }
  std::string json = maho::core::ResetBoost(core, boost_id.c_str());
  if (json.empty()) {
    return nullptr;
  }
  return ParseSingleBoostJson(json);
}

std::string ExportBoostOnCoreSequence(const std::string& domain,
                                      const std::string& boost_id) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return std::string();
  }
  return maho::core::ExportBoost(core, boost_id.c_str());
}

maho_boost::mojom::BoostInfoPtr ImportBoostOnCoreSequence(
    const std::string& domain,
    const std::string& json) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return nullptr;
  }
  std::string result = maho::core::ImportBoost(core, domain.c_str(), json.c_str());
  if (result.empty()) {
    return nullptr;
  }
  return ParseSingleBoostJson(result);
}

maho_boost::mojom::BoostInfoPtr AppendZapSelectorOnCoreSequence(
    const std::string& domain,
    const std::string& boost_id,
    const std::string& selector) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return nullptr;
  }
  std::string json =
      maho::core::AppendZapSelector(core, boost_id.c_str(), selector.c_str());
  if (json.empty()) {
    return nullptr;
  }
  return ParseSingleBoostJson(json);
}

maho_boost::mojom::BoostInfoPtr RemoveZapSelectorOnCoreSequence(
    const std::string& domain,
    const std::string& boost_id,
    const std::string& selector) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return nullptr;
  }
  std::string json =
      maho::core::RemoveZapSelector(core, boost_id.c_str(), selector.c_str());
  if (json.empty()) {
    return nullptr;
  }
  return ParseSingleBoostJson(json);
}

maho_boost::mojom::BoostInfoPtr UpdateBoostOnCoreSequence(
    const std::string& domain,
    const std::string& boost_id,
    const std::string& changes_json) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    LOG(ERROR) << "[MahoBoost] UpdateBoostOnCoreSequence: core is null";
    return nullptr;
  }
  VLOG(1) << "[MahoBoost] UpdateBoostOnCoreSequence: id=" << boost_id
          << " json=" << changes_json;
  char* json_str =
      maho_core_boost_update(core, boost_id.c_str(), changes_json.c_str());
  if (!json_str) {
    LOG(ERROR) << "[MahoBoost] UpdateBoostOnCoreSequence: FFI returned null for id="
               << boost_id << " json=" << changes_json;
    return nullptr;
  }
  std::string json(json_str);
  maho_string_free(json_str);
  VLOG(1) << "[MahoBoost] UpdateBoostOnCoreSequence: success, result length="
          << json.size();
  return ParseSingleBoostJson(json);
}

bool DeleteBoostOnCoreSequence(const std::string& domain,
                               const std::string& boost_id) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return false;
  }
  return maho_core_boost_delete(core, boost_id.c_str());
}

maho_boost::mojom::BoostInfoPtr GetActiveBoostOnCoreSequence(
    const std::string& domain) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return nullptr;
  }
  char* json_str = maho_core_boost_get_active(core, domain.c_str());
  if (!json_str) {
    return nullptr;
  }
  std::string json(json_str);
  maho_string_free(json_str);
  return ParseSingleBoostJson(json);
}

std::pair<std::string, bool> SetActiveBoostOnCoreSequence(
    const std::string& domain,
    const std::optional<std::string>& boost_id) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      (boost_id.has_value() &&
       !BoostBelongsToDomain(domain, *boost_id))) {
    return {{}, false};
  }
  const bool success = maho_core_boost_set_active(
      core, domain.c_str(), boost_id ? boost_id->c_str() : nullptr);
  return {{}, success};
}

std::string ComposeCssOnCoreSequence(const std::string& domain,
                                     const std::string& boost_id) {
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return std::string();
  }
  char* css_str = maho_core_boost_compose_css(core, boost_id.c_str());
  if (!css_str) {
    return std::string();
  }
  std::string css(css_str);
  maho_string_free(css_str);
  return css;
}

std::vector<std::string> GetZapSelectorsOnCoreSequence(
    const std::string& domain,
    const std::string& boost_id) {
  std::vector<std::string> zap_selectors;
  ScopedFfiSerialization serialization;
  MahoCore* core = maho::GetCore();
  if (!serialization.acquired() || !core ||
      !BoostBelongsToDomain(domain, boost_id)) {
    return zap_selectors;
  }
  char* json_str = maho_core_boost_get(core, boost_id.c_str());
  if (!json_str) {
    return zap_selectors;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  maho_string_free(json_str);
  if (parsed && parsed->is_dict()) {
    const base::ListValue* zl = parsed->GetDict().FindList("zapSelectors");
    if (zl) {
      for (const auto& v : *zl) {
        if (const std::string* s = v.GetIfString()) {
          zap_selectors.push_back(*s);
        }
      }
    }
  }
  return zap_selectors;
}

}  // namespace

MahoBoostPageHandler::MahoBoostPageHandler(
    mojo::PendingReceiver<maho_boost::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_boost::mojom::PageObserver> page,
    const std::string& domain,
    base::WeakPtr<maho::MahoBoostWindowController> controller,
    BrowserWindowInterface* browser_window,
    content::WebContents* host_web_contents,
    content::WebContents* target_web_contents)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      domain_(domain),
      controller_(std::move(controller)),
      context_token_(
          browser_window,
          (browser_window && target_web_contents &&
           browser_window->GetTabStripModel()->GetIndexOfWebContents(target_web_contents) != TabStripModel::kNoTab)
              ? target_web_contents
              : nullptr),
      host_web_contents_(host_web_contents ? host_web_contents->GetWeakPtr()
                                           : nullptr),
      target_web_contents_(target_web_contents
                               ? target_web_contents->GetWeakPtr()
                               : nullptr) {
  receiver_.set_disconnect_handler(base::BindOnce(
      &MahoBoostPageHandler::OnPipeDisconnected,
      weak_factory_.GetWeakPtr()));
  page_.set_disconnect_handler(base::BindOnce(
      &MahoBoostPageHandler::OnPipeDisconnected,
      weak_factory_.GetWeakPtr()));
}

MahoBoostPageHandler::~MahoBoostPageHandler() {
  CleanupTargetEditMode();
  maho::RequestBoostWindowClose(controller_);
}

void MahoBoostPageHandler::CleanupTargetEditMode() {
  if (content::WebContents* target = target_web_contents_.get()) {
    if (auto* handler = MahoBoostInjectionHandler::FromWebContents(target)) {
      handler->ExitCurrentEditMode();
    }
  }
}

void MahoBoostPageHandler::OnPipeDisconnected() {
  CleanupTargetEditMode();
  maho::RequestBoostWindowClose(controller_);
}

// static
bool MahoBoostPageHandler::IsBoostAuthorizedForDomainForTesting(
    const std::string& domain,
    const std::string& boost_id) {
  return BoostBelongsToDomain(domain, boost_id);
}

#define WEBUI_REVALIDATE_OR_RETURN(default_val)                              \
  if (!context_token_.Revalidate(                                            \
          MahoPrivateCapability::kProcessGlobalWebUI)) {                     \
    receiver_.reset();                                                       \
    page_.reset();                                                           \
    return;                                                                  \
  }

#define WEBUI_REVALIDATE_OR_RETURN_VOID()                                    \
  if (!context_token_.Revalidate(                                            \
          MahoPrivateCapability::kProcessGlobalWebUI)) {                     \
    receiver_.reset();                                                       \
    page_.reset();                                                           \
    return;                                                                  \
  }

void MahoBoostPageHandler::FireEditorKilled() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context_token_.Revalidate(MahoPrivateCapability::kProcessGlobalWebUI)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  page_->OnEditorKilled();
}

void MahoBoostPageHandler::GetDomain(GetDomainCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN("");
  std::move(callback).Run(domain_);
}

void MahoBoostPageHandler::ListBoosts(ListBoostsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(std::vector<maho_boost::mojom::BoostInfoPtr>());
  maho::PostCoreTask<std::vector<maho_boost::mojom::BoostInfoPtr>>(
      FROM_HERE,
      base::BindOnce(&ListBoostsOnCoreSequence, domain_),
      std::move(callback));
}

void MahoBoostPageHandler::GetBoost(const std::string& boost_id,
                                    GetBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run(nullptr);
    return;
  }
  maho::PostCoreTask<maho_boost::mojom::BoostInfoPtr>(
      FROM_HERE,
      base::BindOnce(&GetBoostOnCoreSequence, domain_, boost_id),
      std::move(callback));
}

void MahoBoostPageHandler::GetActiveBoost(GetActiveBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  maho::PostCoreTask<maho_boost::mojom::BoostInfoPtr>(
      FROM_HERE,
      base::BindOnce(&GetActiveBoostOnCoreSequence, domain_),
      std::move(callback));
}

void MahoBoostPageHandler::CreateTempBoost(CreateTempBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  maho::PostCoreTask<maho_boost::mojom::BoostInfoPtr>(
      FROM_HERE,
      base::BindOnce(&CreateTempBoostOnCoreSequence, domain_),
      base::BindOnce(&MahoBoostPageHandler::OnCreateTempBoostDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBoostPageHandler::CommitBoost(const std::string& boost_id,
                                       CommitBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run();
    return;
  }
  maho::PostCoreTask<maho_boost::mojom::BoostInfoPtr>(
      FROM_HERE,
      base::BindOnce(&CommitBoostOnCoreSequence, domain_, boost_id),
      base::BindOnce(
          [](CommitBoostCallback cb,
             maho_boost::mojom::BoostInfoPtr) { std::move(cb).Run(); },
          std::move(callback)));
}

void MahoBoostPageHandler::DiscardBoost(const std::string& boost_id,
                                        DiscardBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run();
    return;
  }
  maho::PostCoreTask<std::string>(
      FROM_HERE,
      base::BindOnce(&DiscardBoostOnCoreSequence, domain_, boost_id),
      base::BindOnce(&MahoBoostPageHandler::OnDiscardBoostDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBoostPageHandler::UpdateBoost(
    const std::string& boost_id,
    maho_boost::mojom::BoostUpdatePtr changes,
    UpdateBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  if (boost_id.size() > kMaxBoostIdBytes ||
      IsOversizedBoostUpdate(*changes)) {
    std::move(callback).Run(nullptr);
    return;
  }
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run(nullptr);
    return;
  }
  std::string changes_json = SerializeBoostUpdate(changes);
  maho::PostCoreTask<maho_boost::mojom::BoostInfoPtr>(
      FROM_HERE,
      base::BindOnce(&UpdateBoostOnCoreSequence, domain_, boost_id,
                     std::move(changes_json)),
      base::BindOnce(&MahoBoostPageHandler::OnUpdateBoostDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBoostPageHandler::DeleteBoost(const std::string& boost_id,
                                       DeleteBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(false);
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run(false);
    return;
  }
  maho::PostCoreTask<bool>(
      FROM_HERE,
      base::BindOnce(&DeleteBoostOnCoreSequence, domain_, boost_id),
      base::BindOnce(&MahoBoostPageHandler::OnDeleteBoostDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBoostPageHandler::SetActiveBoost(
    const std::optional<std::string>& boost_id,
    SetActiveBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (boost_id.has_value() && !BoostBelongsToDomain(domain_, *boost_id)) {
    std::move(callback).Run();
    return;
  }
  maho::PostCoreTask<std::pair<std::string, bool>>(
      FROM_HERE,
      base::BindOnce(&SetActiveBoostOnCoreSequence, domain_, boost_id),
      base::BindOnce(&MahoBoostPageHandler::OnSetActiveBoostDone,
                     weak_factory_.GetWeakPtr(), boost_id,
                     std::move(callback)));
}

void MahoBoostPageHandler::ComposeCss(const std::string& boost_id,
                                      ComposeCssCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN("");
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run("");
    return;
  }
  maho::PostCoreTask<std::string>(
      FROM_HERE,
      base::BindOnce(&ComposeCssOnCoreSequence, domain_, boost_id),
      base::BindOnce(
          [](ComposeCssCallback callback, std::string css) {
            std::move(callback).Run(std::move(css));
          },
          std::move(callback)));
}

void MahoBoostPageHandler::ShuffleBoost(const std::string& boost_id,
                                        ShuffleBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run(nullptr);
    return;
  }
  maho::PostCoreTask<maho_boost::mojom::BoostInfoPtr>(
      FROM_HERE,
      base::BindOnce(&ShuffleBoostOnCoreSequence, domain_, boost_id),
      base::BindOnce(&MahoBoostPageHandler::OnShuffleBoostDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBoostPageHandler::ResetBoost(const std::string& boost_id,
                                      ResetBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run(nullptr);
    return;
  }
  maho::PostCoreTask<maho_boost::mojom::BoostInfoPtr>(
      FROM_HERE,
      base::BindOnce(&ResetBoostOnCoreSequence, domain_, boost_id),
      base::BindOnce(&MahoBoostPageHandler::OnResetBoostDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBoostPageHandler::ExportBoost(const std::string& boost_id,
                                       ExportBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN("");
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run("");
    return;
  }
  maho::PostCoreTask<std::string>(
      FROM_HERE,
      base::BindOnce(&ExportBoostOnCoreSequence, domain_, boost_id),
      base::BindOnce(
          [](ExportBoostCallback callback, std::string json) {
            std::move(callback).Run(std::move(json));
          },
          std::move(callback)));
}

void MahoBoostPageHandler::ImportBoost(const std::string& json,
                                       ImportBoostCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  if (json.size() > kMaxBoostImportBytes) {
    std::move(callback).Run(nullptr);
    return;
  }
  maho::PostCoreTask<maho_boost::mojom::BoostInfoPtr>(
      FROM_HERE,
      base::BindOnce(&ImportBoostOnCoreSequence, domain_, json),
      base::BindOnce(&MahoBoostPageHandler::OnImportBoostDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBoostPageHandler::CloseDialog(CloseDialogCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  maho::CloseBoostWindow(controller_);
  std::move(callback).Run();
}

void MahoBoostPageHandler::HostCloseFinished(
    HostCloseFinishedCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  maho::CompleteBoostHostClose(controller_);
  std::move(callback).Run();
}

void MahoBoostPageHandler::SetHostCloseState(
    const std::optional<std::string>& boost_id,
    bool dirty,
    SetHostCloseStateCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (boost_id && !BoostBelongsToDomain(domain_, *boost_id)) {
    std::move(callback).Run();
    return;
  }
  maho::SetBoostHostCloseState(controller_, boost_id, dirty);
  std::move(callback).Run();
}

void MahoBoostPageHandler::RequestModeResize(
    maho_boost::mojom::WindowMode mode,
    RequestModeResizeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  maho::SetBoostWindowMode(controller_, mode);
  std::move(callback).Run();
}

void MahoBoostPageHandler::EnterZapMode(const std::string& boost_id,
                                        EnterZapModeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run();
    return;
  }
  content::WebContents* active_wc = GetTargetTab();
  if (!active_wc) {
    std::move(callback).Run();
    return;
  }

  current_zap_boost_id_ = boost_id;
  awaiting_zap_start_notification_ = true;

  MahoBoostInjectionHandler::CreateForWebContents(active_wc);
  auto* handler = MahoBoostInjectionHandler::FromWebContents(active_wc);
  if (!handler) {
    std::move(callback).Run();
    return;
  }

  maho::PostCoreTask<std::vector<std::string>>(
      FROM_HERE,
      base::BindOnce(&GetZapSelectorsOnCoreSequence, domain_, boost_id),
      base::BindOnce(&MahoBoostPageHandler::OnEnterZapModeDone,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     active_wc->GetWeakPtr()));
}

void MahoBoostPageHandler::OnEnterZapModeDone(
    EnterZapModeCallback callback,
    base::WeakPtr<content::WebContents> captured_wc,
    std::vector<std::string> zap_selectors) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  content::WebContents* active_wc = captured_wc.get();
  if (!active_wc) {
    std::move(callback).Run();
    return;
  }
  auto* handler = MahoBoostInjectionHandler::FromWebContents(active_wc);
  if (!handler) {
    std::move(callback).Run();
    return;
  }

  handler->EnterZapMode(
      zap_selectors,
      base::BindRepeating(&MahoBoostPageHandler::OnContentScriptEvent,
                          weak_factory_.GetWeakPtr()));

  if (Browser* browser = static_cast<Browser*>(
          GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
              active_wc))) {
    browser->GetWindow()->Activate();
  }
  active_wc->Focus();

  page_->OnZapStateUpdate(true, !zap_selectors.empty());
  std::move(callback).Run();
}

void MahoBoostPageHandler::ExitZapMode(ExitZapModeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  content::WebContents* active_wc = GetTargetTab();
  if (active_wc) {
    auto* handler = MahoBoostInjectionHandler::FromWebContents(active_wc);
    if (handler) {
      handler->ExitZapMode();
    }
  }
  current_zap_boost_id_.clear();
  awaiting_zap_start_notification_ = false;
  page_->OnZapStateUpdate(false, false);
  std::move(callback).Run();
}

void MahoBoostPageHandler::EnterPickerMode(const std::string& boost_id,
                                           EnterPickerModeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run();
    return;
  }
  content::WebContents* active_wc = GetTargetTab();
  if (!active_wc) {
    std::move(callback).Run();
    return;
  }

  current_picker_boost_id_ = boost_id;

  MahoBoostInjectionHandler::CreateForWebContents(active_wc);
  auto* handler = MahoBoostInjectionHandler::FromWebContents(active_wc);
  if (!handler) {
    std::move(callback).Run();
    return;
  }

  handler->EnterPickerMode(
      base::BindRepeating(&MahoBoostPageHandler::OnContentScriptEvent,
                          weak_factory_.GetWeakPtr()));

  page_->OnPickerStateUpdate(true);
  std::move(callback).Run();
}

void MahoBoostPageHandler::ExitPickerMode(ExitPickerModeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  content::WebContents* active_wc = GetTargetTab();
  if (active_wc) {
    auto* handler = MahoBoostInjectionHandler::FromWebContents(active_wc);
    if (handler) {
      handler->ExitPickerMode();
    }
  }
  current_picker_boost_id_.clear();
  page_->OnPickerStateUpdate(false);
  std::move(callback).Run();
}

void MahoBoostPageHandler::AppendZapSelector(
    const std::string& boost_id,
    const std::string& selector,
    AppendZapSelectorCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (boost_id.size() > kMaxBoostIdBytes ||
      selector.size() > kMaxZapSelectorBytes) {
    std::move(callback).Run();
    return;
  }
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run();
    return;
  }
  maho::PostCoreTask<maho_boost::mojom::BoostInfoPtr>(
      FROM_HERE,
      base::BindOnce(&AppendZapSelectorOnCoreSequence, domain_, boost_id,
                     selector),
      base::BindOnce(&MahoBoostPageHandler::OnAppendZapDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBoostPageHandler::RemoveZapSelector(
    const std::string& boost_id,
    const std::string& selector,
    RemoveZapSelectorCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (!BoostBelongsToDomain(domain_, boost_id)) {
    std::move(callback).Run();
    return;
  }
  maho::PostCoreTask<maho_boost::mojom::BoostInfoPtr>(
      FROM_HERE,
      base::BindOnce(&RemoveZapSelectorOnCoreSequence, domain_, boost_id,
                     selector),
      base::BindOnce(&MahoBoostPageHandler::OnRemoveZapDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBoostPageHandler::OpenInspector(OpenInspectorCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  content::WebContents* active_wc = GetTargetTab();
  if (active_wc) {
    DevToolsWindow::OpenDevToolsWindow(
        active_wc,
        DevToolsToggleAction::Inspect(),
        DevToolsOpenedByAction::kContextMenuInspect);
  }
  std::move(callback).Run();
}

void MahoBoostPageHandler::GetSystemFonts(GetSystemFontsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(std::vector<std::string>());
  content::GetFontListAsync(
      base::BindOnce(&MahoBoostPageHandler::OnGetFontListDone,
                     weak_factory_.GetWeakPtr(),
                     std::move(callback)));
}

void MahoBoostPageHandler::OnCreateTempBoostDone(
    CreateTempBoostCallback callback,
    maho_boost::mojom::BoostInfoPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  if (result) {
    maho::SetBoostTemporaryId(controller_, result->id);
  }
  page_->OnBoostsChanged();
  std::move(callback).Run(std::move(result));
}

void MahoBoostPageHandler::OnUpdateBoostDone(
    UpdateBoostCallback callback,
    maho_boost::mojom::BoostInfoPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  if (!result) {
    std::move(callback).Run(nullptr);
    return;
  }
  if (result) {
    VLOG(1) << "[MahoBoost] OnUpdateBoostDone: reinject domain="
            << result->domain;
    MahoBoostInjectionHandler::ReinjectForDomain(result->domain);
  } else {
    LOG(ERROR) << "[MahoBoost] OnUpdateBoostDone: result is null, "
               << "skipping reinject";
  }
  page_->OnBoostsChanged();
  std::move(callback).Run(std::move(result));
}

void MahoBoostPageHandler::OnDeleteBoostDone(DeleteBoostCallback callback,
                                             bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(false);
  if (success) {
    MahoBoostInjectionHandler::ReinjectForDomain(domain_);
  }
  page_->OnBoostsChanged();
  std::move(callback).Run(success);
}

void MahoBoostPageHandler::OnSetActiveBoostDone(
    std::optional<std::string> boost_id,
    SetActiveBoostCallback callback,
    std::pair<std::string, bool> result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (!result.second) {
    std::move(callback).Run();
    return;
  }
  if (result.first.empty()) {
    MahoBoostInjectionHandler::ReinjectForDomain(domain_);
  } else {
    MahoBoostInjectionHandler::OnCoreUpdatesJson(std::move(result.first));
  }
  page_->OnActiveChanged(boost_id);
  std::move(callback).Run();
}

void MahoBoostPageHandler::OnDiscardBoostDone(DiscardBoostCallback callback,
                                               std::string prev_active_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  MahoBoostInjectionHandler::ReinjectForDomain(domain_);
  page_->OnBoostsChanged();
  std::move(callback).Run();
}

void MahoBoostPageHandler::OnShuffleBoostDone(
    ShuffleBoostCallback callback,
    maho_boost::mojom::BoostInfoPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  if (result) {
    MahoBoostInjectionHandler::ReinjectForDomain(result->domain);
  }
  page_->OnBoostsChanged();
  std::move(callback).Run(std::move(result));
}

void MahoBoostPageHandler::OnResetBoostDone(
    ResetBoostCallback callback,
    maho_boost::mojom::BoostInfoPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  if (result) {
    MahoBoostInjectionHandler::ReinjectForDomain(result->domain);
    page_->OnZapStateUpdate(!current_zap_boost_id_.empty(),
                            !result->zap_selectors.empty());
  }
  page_->OnZapListUpdate();
  page_->OnBoostsChanged();
  std::move(callback).Run(std::move(result));
}

void MahoBoostPageHandler::OnImportBoostDone(
    ImportBoostCallback callback,
    maho_boost::mojom::BoostInfoPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(nullptr);
  page_->OnBoostsChanged();
  std::move(callback).Run(std::move(result));
}

void MahoBoostPageHandler::OnAppendZapDone(
    AppendZapSelectorCallback callback,
    maho_boost::mojom::BoostInfoPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (result) {
    MahoBoostInjectionHandler::ReinjectForDomain(result->domain);
    page_->OnZapStateUpdate(!current_zap_boost_id_.empty(),
                            !result->zap_selectors.empty());
  }
  page_->OnZapListUpdate();
  page_->OnBoostsChanged();
  std::move(callback).Run();
}

void MahoBoostPageHandler::OnRemoveZapDone(
    RemoveZapSelectorCallback callback,
    maho_boost::mojom::BoostInfoPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN_VOID();
  if (result) {
    MahoBoostInjectionHandler::ReinjectForDomain(result->domain);
    page_->OnZapStateUpdate(!current_zap_boost_id_.empty(),
                            !result->zap_selectors.empty());
  }
  page_->OnZapListUpdate();
  page_->OnBoostsChanged();
  std::move(callback).Run();
}

void MahoBoostPageHandler::OnGetFontListDone(GetSystemFontsCallback callback,
                                               base::ListValue fonts) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WEBUI_REVALIDATE_OR_RETURN(std::vector<std::string>());
  std::vector<std::string> family_names;
  family_names.reserve(fonts.size());
  for (const base::Value& entry : fonts) {
    if (entry.is_list() && !entry.GetList().empty()) {
      const base::Value& name = entry.GetList()[0];
      if (name.is_string()) {
        family_names.push_back(name.GetString());
      }
    }
  }
  std::move(callback).Run(std::move(family_names));
}

void MahoBoostPageHandler::OnContentScriptEvent(
    const std::string& type,
    const std::string& selector,
    const std::string& msg) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!context_token_.Revalidate(MahoPrivateCapability::kProcessGlobalWebUI)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  if (type == "zap_click" && !current_zap_boost_id_.empty()) {
    AppendZapSelector(current_zap_boost_id_, selector,
                      base::BindOnce([](){ }));
  } else if (type == "unzap_click" && !current_zap_boost_id_.empty()) {
    RemoveZapSelector(current_zap_boost_id_, selector,
                      base::BindOnce([](){ }));
  } else if (type == "picker_selected" &&
             !current_picker_boost_id_.empty()) {
    current_picker_boost_id_.clear();
    page_->OnPickerStateUpdate(false);
    page_->OnPickerSelectorPicked(selector);
  } else if (type == "notify") {
    if (selector == "zap-state-update") {
      if (awaiting_zap_start_notification_) {
        awaiting_zap_start_notification_ = false;
      } else {
        current_zap_boost_id_.clear();
      }
      page_->OnZapStateUpdate(!current_zap_boost_id_.empty(), false);
    } else if (selector == "zap-list-update") {
      page_->OnZapListUpdate();
    } else if (selector == "selector-picker-state-update") {
      bool is_on = (msg == "onenable");
      page_->OnPickerStateUpdate(is_on);
      if (!is_on) {
        current_picker_boost_id_.clear();
      }
    }
  }
}

content::WebContents* MahoBoostPageHandler::GetTargetTab() {
  return target_web_contents_.get();
}
