// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/boost/maho_boost_visual_capture_test_support.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"

namespace maho::boost::test {
namespace {

constexpr char kRuntimeManifestName[] = "capture-manifest.json";
constexpr char kVisualCaptureDirectoryRelativePath[] =
    ".omo/evidence/boost-ui-functional-coverage/visual-matrix";
constexpr char kVisualContractRelativePath[] =
    "maho-chromium/browser/ui/views/boost/"
    "maho_boost_visual_capture_manifest.json";

std::string TimeAsUnixMilliseconds(base::Time time) {
  return base::NumberToString(time.InMillisecondsSinceUnixEpoch());
}

std::optional<base::Time> FileMtime(const base::FilePath& path) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::File::Info info;
  if (!base::GetFileInfo(path, &info) || info.is_directory) {
    return std::nullopt;
  }
  return info.last_modified;
}

}  // namespace

VisualCaptureSpec::VisualCaptureSpec() = default;
VisualCaptureSpec::VisualCaptureSpec(const VisualCaptureSpec&) = default;
VisualCaptureSpec& VisualCaptureSpec::operator=(const VisualCaptureSpec&) = default;
VisualCaptureSpec::~VisualCaptureSpec() = default;

MahoBoostVisualCaptureTestBase::MahoBoostVisualCaptureTestBase() = default;
MahoBoostVisualCaptureTestBase::~MahoBoostVisualCaptureTestBase() = default;

const std::vector<VisualCaptureSpec>&
MahoBoostVisualCaptureTestBase::visual_capture_matrix() {
  CHECK(LoadVisualCaptureContract());
  return visual_capture_matrix_;
}

const std::vector<base::FilePath>&
MahoBoostVisualCaptureTestBase::visual_source_paths() {
  CHECK(LoadVisualCaptureContract());
  return visual_source_paths_;
}

base::FilePath MahoBoostVisualCaptureTestBase::workspace_root() const {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath search_root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT,
                               &search_root));

  for (base::FilePath candidate = search_root; !candidate.empty();) {
    if (base::PathExists(candidate.AppendASCII(kVisualContractRelativePath))) {
      return candidate;
    }

    const base::FilePath parent = candidate.DirName();
    if (parent == candidate) {
      break;
    }
    candidate = parent;
  }

  CHECK(false) << "Could not find visual capture workspace root from "
               << search_root;
  return base::FilePath();
}

base::FilePath MahoBoostVisualCaptureTestBase::visual_contract_path() const {
  return workspace_root().AppendASCII(kVisualContractRelativePath);
}

base::FilePath MahoBoostVisualCaptureTestBase::visual_capture_directory() const {
  return workspace_root().AppendASCII(kVisualCaptureDirectoryRelativePath);
}

bool MahoBoostVisualCaptureTestBase::LoadVisualCaptureContract() {
  if (visual_capture_contract_loaded_) {
    return true;
  }

  base::ScopedAllowBlockingForTesting allow_blocking;
  std::string contents;
  if (!base::ReadFileToString(visual_contract_path(), &contents)) {
    ADD_FAILURE() << "Could not read visual capture contract: "
                  << visual_contract_path();
    return false;
  }
  std::optional<base::DictValue> contract =
      base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
  if (!contract) {
    ADD_FAILURE() << "Visual capture contract is not valid JSON";
    return false;
  }

  const base::ListValue* sources = contract->FindList("visual_sources");
  const base::ListValue* states = contract->FindList("states");
  if (!sources || !states || sources->empty() || states->empty()) {
    ADD_FAILURE() << "Visual capture contract has no sources or states";
    return false;
  }

  for (const base::Value& source_value : *sources) {
    const base::DictValue* source = source_value.GetIfDict();
    const std::string* path = source ? source->FindString("path") : nullptr;
    if (!path) {
      ADD_FAILURE() << "Visual source entry has no path";
      return false;
    }
    visual_source_paths_.push_back(workspace_root().AppendASCII(*path));
  }

  for (const base::Value& state_value : *states) {
    const base::DictValue* state = state_value.GetIfDict();
    if (!state) {
      ADD_FAILURE() << "Visual state entry is not an object";
      return false;
    }
    const std::string* id = state->FindString("id");
    const std::string* owner_test = state->FindString("owner_test");
    const std::string* surface = state->FindString("surface");
    const std::string* filename = state->FindString("filename");
    const std::string* frame = state->FindString("frame");
    const std::optional<int> width = state->FindInt("width");
    const std::optional<int> height = state->FindInt("height");
    if (!id || !owner_test || !surface || !filename || !frame || !width ||
        !height) {
      ADD_FAILURE() << "Visual state entry is missing a required field";
      return false;
    }
    std::optional<std::string> reference;
    if (const std::string* value = state->FindString("reference")) {
      reference = *value;
    }
    VisualCaptureSpec spec;
    spec.id = *id;
    spec.owner_test = *owner_test;
    spec.surface = *surface;
    spec.size = gfx::Size(*width, *height);
    spec.filename = *filename;
    spec.reference = std::move(reference);
    spec.frame = *frame;
    visual_capture_matrix_.push_back(std::move(spec));
  }

  visual_capture_contract_loaded_ = true;
  return true;
}

std::optional<base::Time>
MahoBoostVisualCaptureTestBase::LatestVisualSourceMtime() const {
  std::optional<base::Time> latest;
  for (const base::FilePath& source : visual_source_paths_) {
    const std::optional<base::Time> mtime = FileMtime(source);
    if (!mtime) {
      ADD_FAILURE() << "Visual source is missing or unreadable: " << source;
      return std::nullopt;
    }
    if (!latest || *mtime > *latest) {
      latest = *mtime;
    }
  }
  return latest;
}

bool MahoBoostVisualCaptureTestBase::CaptureVisualState(
    content::WebContents* editor_web_contents,
    std::string_view state_id) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  if (!LoadVisualCaptureContract() || !editor_web_contents) {
    ADD_FAILURE() << "Visual capture requires the loaded contract and editor";
    return false;
  }
  const auto state = std::ranges::find(
      visual_capture_matrix_, state_id, &VisualCaptureSpec::id);
  if (state == visual_capture_matrix_.end()) {
    ADD_FAILURE() << "Unknown visual capture state: " << state_id;
    return false;
  }

  content::RenderWidgetHostView* view =
      editor_web_contents->GetRenderWidgetHostView();
  if (!view || view->GetViewBounds().size() != state->size) {
    ADD_FAILURE() << "Visual state " << state_id
                  << " is not at its exact WebUI content size "
                  << state->size.ToString();
    return false;
  }
  if (!base::CreateDirectory(visual_capture_directory())) {
    ADD_FAILURE() << "Could not create visual capture directory";
    return false;
  }

  const std::optional<base::Time> latest_source = LatestVisualSourceMtime();
  if (!latest_source) {
    return false;
  }
  const base::FilePath output =
      visual_capture_directory().AppendASCII(state->filename);
  if (!base::DeleteFile(output) && base::PathExists(output)) {
    ADD_FAILURE() << "Could not remove stale visual capture: " << output;
    return false;
  }
  if (!CaptureEditorScreenshot(editor_web_contents, state->size, output)) {
    return false;
  }

  const VisualCaptureValidationResult validation =
      ValidateVisualCaptureArtifact(output, state->size, *latest_source);
  if (!validation.ok()) {
    ADD_FAILURE() << "Visual capture rejected for " << state_id << ": "
                  << validation.detail;
    return false;
  }
  return RefreshVisualCaptureManifest();
}

bool MahoBoostVisualCaptureTestBase::RefreshVisualCaptureManifest() {
  base::ScopedAllowBlockingForTesting allow_blocking;
  if (!LoadVisualCaptureContract()) {
    return false;
  }
  const std::optional<base::Time> latest_source = LatestVisualSourceMtime();
  if (!latest_source || !base::CreateDirectory(visual_capture_directory())) {
    return false;
  }

  base::ListValue source_entries;
  for (const base::FilePath& source : visual_source_paths_) {
    base::File::Info info;
    CHECK(base::GetFileInfo(source, &info));
    source_entries.Append(
        base::DictValue()
            .Set("path", source.AsUTF8Unsafe())
            .Set("mtime_unix_ms", TimeAsUnixMilliseconds(info.last_modified)));
  }

  bool complete = true;
  base::ListValue state_entries;
  for (const VisualCaptureSpec& state : visual_capture_matrix_) {
    const base::FilePath path =
        visual_capture_directory().AppendASCII(state.filename);
    const VisualCaptureValidationResult validation =
        ValidateVisualCaptureArtifact(path, state.size, *latest_source);
    complete = complete && validation.ok();
    base::DictValue entry =
        base::DictValue()
            .Set("id", state.id)
            .Set("owner_test", state.owner_test)
            .Set("surface", state.surface)
            .Set("width", state.size.width())
            .Set("height", state.size.height())
            .Set("filename", state.filename)
            .Set("frame", state.frame)
            .Set("valid", validation.ok())
            .Set("validation", validation.detail);
    if (const std::optional<base::Time> mtime = FileMtime(path)) {
      entry.Set("capture_mtime_unix_ms", TimeAsUnixMilliseconds(*mtime));
    }
    if (state.reference) {
      entry.Set("reference", *state.reference);
    }
    state_entries.Append(std::move(entry));
  }

  base::DictValue manifest =
      base::DictValue()
          .Set("schema_version", 1)
          .Set("owner", "Todo 16")
          .Set("execution_owner", "Todo 17")
          .Set("complete", complete)
          .Set("latest_visual_source_mtime_unix_ms",
               TimeAsUnixMilliseconds(*latest_source))
          .Set("visual_sources", std::move(source_entries))
          .Set("states", std::move(state_entries));
  const std::optional<std::string> json = base::WriteJsonWithOptions(
      manifest, base::OPTIONS_PRETTY_PRINT);
  if (!json ||
      !base::WriteFile(
          visual_capture_directory().AppendASCII(kRuntimeManifestName), *json)) {
    ADD_FAILURE() << "Could not write visual capture runtime manifest";
    return false;
  }
  return true;
}

}  // namespace maho::boost::test
