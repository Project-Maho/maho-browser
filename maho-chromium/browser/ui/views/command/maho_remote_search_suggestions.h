// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_REMOTE_SEARCH_SUGGESTIONS_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_REMOTE_SEARCH_SUGGESTIONS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "url/gurl.h"

class Profile;
class AutocompleteResult;

namespace maho {

// Renderer-neutral output from the remote-search provider. Match filtering and
// mapping are deliberately added in Task 2; this type keeps that policy out of
// the command palette model and its rendering code.
struct RemoteSearchSuggestion {
  RemoteSearchSuggestion(std::string normalized_search_terms,
                         std::string display_title,
                         std::optional<std::string> subtitle,
                         GURL destination_url);
  RemoteSearchSuggestion(const RemoteSearchSuggestion&);
  RemoteSearchSuggestion(RemoteSearchSuggestion&&);
  RemoteSearchSuggestion& operator=(const RemoteSearchSuggestion&);
  RemoteSearchSuggestion& operator=(RemoteSearchSuggestion&&);
  ~RemoteSearchSuggestion();

  std::string normalized_search_terms;
  std::string display_title;
  std::optional<std::string> subtitle;
  GURL destination_url;
};

class MahoRemoteSearchSuggestionSource {
 public:
  using SnapshotCallback =
      base::RepeatingCallback<void(std::vector<RemoteSearchSuggestion>)>;

  virtual ~MahoRemoteSearchSuggestionSource() = default;
  virtual void Start(const std::string& query, SnapshotCallback callback) = 0;
  virtual void Cancel() = 0;
};

class MahoRemoteSearchSuggestions final
    : public MahoRemoteSearchSuggestionSource {
 public:
  class ControllerForTesting {
   public:
    using ResultChangedCallback = base::RepeatingClosure;

    virtual ~ControllerForTesting() = default;
    virtual void SetResultChangedCallback(ResultChangedCallback callback) = 0;
    virtual void Start(const std::string& query) = 0;
    virtual void Stop() = 0;
    virtual std::vector<RemoteSearchSuggestion> GetSuggestions() const;
  };

  explicit MahoRemoteSearchSuggestions(Profile* profile);
  MahoRemoteSearchSuggestions(const MahoRemoteSearchSuggestions&) = delete;
  MahoRemoteSearchSuggestions& operator=(const MahoRemoteSearchSuggestions&) =
      delete;
  ~MahoRemoteSearchSuggestions() override;

  void Start(const std::string& query, SnapshotCallback callback) override;
  void Cancel() override;

  // Narrow config seam for offline deterministic unit coverage. It does not
  // expose the controller to consumers such as MahoCommandModel.
  static int ProviderTypesForTesting();
  static std::unique_ptr<MahoRemoteSearchSuggestions> CreateForTesting(
      std::unique_ptr<ControllerForTesting> controller);

 private:
  explicit MahoRemoteSearchSuggestions(
      std::unique_ptr<ControllerForTesting> controller);

  class Impl;
  std::unique_ptr<Impl> impl_;
};

std::unique_ptr<MahoRemoteSearchSuggestionSource>
CreateMahoRemoteSearchSuggestionSource(Profile* profile);

// Pure mapping seam for deterministic synthetic AutocompleteResult tests.
std::vector<RemoteSearchSuggestion> MapRemoteSearchSuggestionsForTesting(
    const AutocompleteResult& result);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_REMOTE_SEARCH_SUGGESTIONS_H_
