// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/maho_import_session_completion.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoImportSessionCompletionTest, SuccessReleasesThenHydratesThenForwards) {
  ImportTerminalState state = ImportTerminalState::kRunning;

  EXPECT_EQ(AdvanceImportProgress(3, 0, &state),
            (std::vector{ImportProgressAction::kReleaseSession,
                         ImportProgressAction::kHydrateSpaces,
                         ImportProgressAction::kForwardProgress}));
  EXPECT_EQ(state, ImportTerminalState::kCompleted);
  EXPECT_TRUE(AdvanceImportProgress(3, 0, &state).empty());
}

TEST(MahoImportSessionCompletionTest, CancellationOnlyReleasesSession) {
  ImportTerminalState state = ImportTerminalState::kCancelled;

  EXPECT_EQ(AdvanceImportProgress(3, 0, &state),
            (std::vector{ImportProgressAction::kReleaseSession}));
  EXPECT_EQ(state, ImportTerminalState::kCancelled);
  EXPECT_TRUE(AdvanceImportProgress(1, 0, &state).empty());
}

TEST(MahoImportSessionCompletionTest, ErrorReleasesThenForwardsWithoutHydration) {
  ImportTerminalState state = ImportTerminalState::kRunning;

  EXPECT_EQ(AdvanceImportProgress(4, 0, &state),
            (std::vector{ImportProgressAction::kReleaseSession,
                         ImportProgressAction::kForwardProgress}));
  EXPECT_EQ(state, ImportTerminalState::kFailed);
  EXPECT_TRUE(AdvanceImportProgress(3, 0, &state).empty());
}

TEST(MahoImportSessionCompletionTest, TypedErrorIsForwardedAndNotTerminal) {
  ImportTerminalState state = ImportTerminalState::kRunning;

  // Bookmarks (bit 2) failed; the import continues.
  EXPECT_EQ(AdvanceImportProgress(4, 2, &state),
            (std::vector{ImportProgressAction::kForwardProgress}));
  EXPECT_EQ(state, ImportTerminalState::kRunning);

  // The orchestrator's AllComplete still ends the session normally.
  EXPECT_EQ(AdvanceImportProgress(3, 0, &state),
            (std::vector{ImportProgressAction::kReleaseSession,
                         ImportProgressAction::kHydrateSpaces,
                         ImportProgressAction::kForwardProgress}));
  EXPECT_EQ(state, ImportTerminalState::kCompleted);
}

}  // namespace
}  // namespace maho
