// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_remote_search_suggestions.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/profiles/profile.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/autocomplete_result.h"

namespace maho {

RemoteSearchSuggestion::RemoteSearchSuggestion(
    std::string normalized_search_terms,
    std::string display_title,
    std::optional<std::string> subtitle,
    GURL destination_url)
    : normalized_search_terms(std::move(normalized_search_terms)),
      display_title(std::move(display_title)),
      subtitle(std::move(subtitle)),
      destination_url(std::move(destination_url)) {}

RemoteSearchSuggestion::RemoteSearchSuggestion(
    const RemoteSearchSuggestion&) = default;
RemoteSearchSuggestion::RemoteSearchSuggestion(RemoteSearchSuggestion&&) =
    default;
RemoteSearchSuggestion& RemoteSearchSuggestion::operator=(
    const RemoteSearchSuggestion&) = default;
RemoteSearchSuggestion& RemoteSearchSuggestion::operator=(
    RemoteSearchSuggestion&&) = default;
RemoteSearchSuggestion::~RemoteSearchSuggestion() = default;

namespace {

AutocompleteControllerConfig MakeControllerConfig() {
  AutocompleteControllerConfig config;
  config.provider_types = AutocompleteProvider::TYPE_SEARCH;
  return config;
}

bool IsAcceptedMatchType(AutocompleteMatchType::Type type) {
  switch (type) {
    case AutocompleteMatchType::SEARCH_SUGGEST:
    case AutocompleteMatchType::SEARCH_SUGGEST_ENTITY:
    case AutocompleteMatchType::SEARCH_SUGGEST_TAIL:
    case AutocompleteMatchType::SEARCH_SUGGEST_PERSONALIZED:
    case AutocompleteMatchType::SEARCH_SUGGEST_PROFILE:
      return true;
    default:
      return false;
  }
}

std::string TrimToUtf8(std::u16string_view value) {
  return base::UTF16ToUTF8(base::TrimWhitespace(value, base::TRIM_ALL));
}

std::vector<RemoteSearchSuggestion> MapRemoteSearchSuggestions(
    const AutocompleteResult& result) {
  std::vector<RemoteSearchSuggestion> suggestions;
  std::unordered_set<std::string> normalized_terms_seen;

  for (const AutocompleteMatch& match : result) {
    if (!IsAcceptedMatchType(match.type) ||
        !match.destination_url.is_valid() ||
        !match.destination_url.SchemeIsHTTPOrHTTPS()) {
      continue;
    }

    const std::u16string_view search_terms =
        match.fill_into_edit.empty() ? std::u16string_view(match.contents)
                                     : std::u16string_view(match.fill_into_edit);
    std::string normalized_search_terms =
        base::ToLowerASCII(TrimToUtf8(search_terms));
    if (normalized_search_terms.empty() ||
        !normalized_terms_seen.insert(normalized_search_terms).second) {
      continue;
    }

    std::string display_title = TrimToUtf8(match.contents);
    if (display_title.empty()) {
      display_title = TrimToUtf8(search_terms);
    }
    std::string subtitle = TrimToUtf8(match.description);
    suggestions.emplace_back(
        std::move(normalized_search_terms), std::move(display_title),
        subtitle.empty() ? std::nullopt
                         : std::make_optional(std::move(subtitle)),
        match.destination_url);
  }

  return suggestions;
}

class AutocompleteControllerAdapter final
    : public MahoRemoteSearchSuggestions::ControllerForTesting,
      public AutocompleteController::Observer {
 public:
  explicit AutocompleteControllerAdapter(Profile* profile)
      : profile_(profile),
        controller_(std::make_unique<AutocompleteController>(
            std::make_unique<ChromeAutocompleteProviderClient>(profile),
            MakeControllerConfig())) {
    DCHECK(profile_);
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    controller_observation_.Observe(controller_.get());
  }

  AutocompleteControllerAdapter(const AutocompleteControllerAdapter&) = delete;
  AutocompleteControllerAdapter& operator=(
      const AutocompleteControllerAdapter&) = delete;

  ~AutocompleteControllerAdapter() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    result_changed_callback_.Reset();
    controller_observation_.Reset();
    controller_->Stop(AutocompleteStopReason::kClobbered);
  }

  void SetResultChangedCallback(ResultChangedCallback callback) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    result_changed_callback_ = std::move(callback);
  }

  void Start(const std::string& query) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    // AutocompleteInput measures the cursor position in UTF-16 code units, so
    // it must be derived from the converted text. The UTF-8 byte length
    // overshoots for any non-ASCII query (Korean is 3 bytes per 1 UTF-16 unit)
    // and trips the cursor_position_ <= text.length() DCHECK in
    // AutocompleteInput::Init.
    const std::u16string text = base::UTF8ToUTF16(query);
    AutocompleteInput input(text, text.length(),
                            metrics::OmniboxEventProto::OTHER,
                            ChromeAutocompleteSchemeClassifier(profile_));
    input.set_allow_exact_keyword_match(false);
    controller_->Start(input);
  }

  void Stop() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    controller_->Stop(AutocompleteStopReason::kClobbered);
  }

  std::vector<RemoteSearchSuggestion> GetSuggestions() const override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return MapRemoteSearchSuggestions(controller_->result());
  }

 private:
  void OnResultChanged(AutocompleteController* controller,
                       bool default_match_changed) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    DCHECK_EQ(controller_.get(), controller);
    if (result_changed_callback_) {
      result_changed_callback_.Run();
    }
  }

  SEQUENCE_CHECKER(sequence_checker_);
  const raw_ptr<Profile> profile_;
  std::unique_ptr<AutocompleteController> controller_;
  base::ScopedObservation<AutocompleteController,
                          AutocompleteController::Observer>
      controller_observation_{this};
  ResultChangedCallback result_changed_callback_;
};

}  // namespace

class MahoRemoteSearchSuggestions::Impl {
 public:
  explicit Impl(std::unique_ptr<ControllerForTesting> controller)
      : controller_(std::move(controller)) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    DCHECK(controller_);
    controller_->SetResultChangedCallback(base::BindRepeating(
        &Impl::OnResultChanged, weak_factory_.GetWeakPtr()));
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  ~Impl() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    snapshot_callback_.Reset();
    weak_factory_.InvalidateWeakPtrs();
    controller_->SetResultChangedCallback({});
    controller_->Stop();
  }

  void Start(const std::string& query, SnapshotCallback callback) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    snapshot_callback_ = std::move(callback);
    weak_factory_.InvalidateWeakPtrs();
    controller_->SetResultChangedCallback(base::BindRepeating(
        &Impl::OnResultChanged, weak_factory_.GetWeakPtr()));
    if (query.empty()) {
      return;
    }
    controller_->Start(query);
  }

  void Cancel() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    snapshot_callback_.Reset();
    weak_factory_.InvalidateWeakPtrs();
    controller_->SetResultChangedCallback({});
    controller_->Stop();
  }

 private:
  void OnResultChanged() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (!snapshot_callback_) {
      return;
    }

    // AutocompleteController notifies observers synchronously from Start() and
    // while streaming updates. Never run consumer code on that stack: the
    // consumer may destroy this source, which owns the controller currently
    // dispatching the observer notification.
    std::vector<RemoteSearchSuggestion> snapshot =
        controller_->GetSuggestions();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Impl::PublishSnapshot, weak_factory_.GetWeakPtr(),
                       std::move(snapshot)));
  }

  void PublishSnapshot(std::vector<RemoteSearchSuggestion> snapshot) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (!snapshot_callback_) {
      return;
    }

    SnapshotCallback callback = snapshot_callback_;
    base::WeakPtr<Impl> alive = weak_factory_.GetWeakPtr();
    callback.Run(std::move(snapshot));
    if (!alive) {
      return;
    }
  }

  SEQUENCE_CHECKER(sequence_checker_);
  std::unique_ptr<ControllerForTesting> controller_;
  SnapshotCallback snapshot_callback_;
  base::WeakPtrFactory<Impl> weak_factory_{this};
};

std::vector<RemoteSearchSuggestion>
MahoRemoteSearchSuggestions::ControllerForTesting::GetSuggestions() const {
  return {};
}

MahoRemoteSearchSuggestions::MahoRemoteSearchSuggestions(Profile* profile)
    : impl_(std::make_unique<Impl>(
          std::make_unique<AutocompleteControllerAdapter>(profile))) {}

MahoRemoteSearchSuggestions::MahoRemoteSearchSuggestions(
    std::unique_ptr<ControllerForTesting> controller)
    : impl_(std::make_unique<Impl>(std::move(controller))) {}

MahoRemoteSearchSuggestions::~MahoRemoteSearchSuggestions() = default;

void MahoRemoteSearchSuggestions::Start(const std::string& query,
                                        SnapshotCallback callback) {
  impl_->Start(query, std::move(callback));
}

void MahoRemoteSearchSuggestions::Cancel() {
  impl_->Cancel();
}

int MahoRemoteSearchSuggestions::ProviderTypesForTesting() {
  return MakeControllerConfig().provider_types;
}

std::unique_ptr<MahoRemoteSearchSuggestions>
MahoRemoteSearchSuggestions::CreateForTesting(
    std::unique_ptr<ControllerForTesting> controller) {
  return std::unique_ptr<MahoRemoteSearchSuggestions>(
      new MahoRemoteSearchSuggestions(std::move(controller)));
}

std::unique_ptr<MahoRemoteSearchSuggestionSource>
CreateMahoRemoteSearchSuggestionSource(Profile* profile) {
  return std::make_unique<MahoRemoteSearchSuggestions>(profile);
}

std::vector<RemoteSearchSuggestion> MapRemoteSearchSuggestionsForTesting(
    const AutocompleteResult& result) {
  return MapRemoteSearchSuggestions(result);
}

}  // namespace maho
