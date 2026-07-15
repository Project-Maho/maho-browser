// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"

#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines.mojom.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// RAII swap of the process-global MahoCore so the handler's maho::GetCore()
// resolves to our temp-sqlite-backed core for the duration of a test.
class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(maho::GetCore()) {
    maho::SetCore(core);
  }
  ~ScopedCoreOverride() { maho::SetCore(saved_); }

 private:
  raw_ptr<MahoCore> saved_;
};

// Collects the routines returned by a ListAllRoutines() call. The handler runs
// the callback synchronously (FFI is blocking), so no RunLoop is required.
std::vector<maho_routines::mojom::RoutineInfoPtr> ListAll(
    MahoRoutinesPageHandler* handler) {
  std::vector<maho_routines::mojom::RoutineInfoPtr> out;
  handler->ListAllRoutines(base::BindLambdaForTesting(
      [&](std::vector<maho_routines::mojom::RoutineInfoPtr> routines) {
        out = std::move(routines);
      }));
  return out;
}

class MahoRoutinesPageHandlerTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath db =
        temp_dir_.GetPath().AppendASCII("maho_routines_test.sqlite");
    core_ = maho_core_new_with_storage(db.AsUTF8Unsafe().c_str());
    ASSERT_NE(core_, nullptr);
    scoped_core_ = std::make_unique<ScopedCoreOverride>(core_);
  }

  void TearDown() override {
    scoped_core_.reset();
    if (core_) {
      maho_core_free(core_);
      core_ = nullptr;
    }
  }

  // Builds a page handler bound to throwaway mojo pipes. Tests invoke the
  // handler's methods directly (not over mojo), so the pipes only need to be
  // valid for construction.
  std::unique_ptr<MahoRoutinesPageHandler> MakeHandler() {
    mojo::PendingRemote<maho_routines::mojom::Page> page_remote;
    std::ignore = page_remote.InitWithNewPipeAndPassReceiver();
    return std::make_unique<MahoRoutinesPageHandler>(
        handler_remote_.BindNewPipeAndPassReceiver(), std::move(page_remote));
  }

  content::BrowserTaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  raw_ptr<MahoCore> core_ = nullptr;
  std::unique_ptr<ScopedCoreOverride> scoped_core_;
  mojo::Remote<maho_routines::mojom::PageHandler> handler_remote_;
};

// Create a custom routine, confirm it surfaces in ListAllRoutines() with
// is_custom=true and the expected fields, then delete it and confirm it is
// gone. Mirrors the Rust-side FFI roundtrip test at the mojom-handler boundary.
TEST_F(MahoRoutinesPageHandlerTest, CreateListDeleteRoundtrip) {
  auto handler = MakeHandler();

  // Baseline: only built-in routines, none custom.
  std::vector<maho_routines::mojom::RoutineInfoPtr> baseline = ListAll(handler.get());
  const size_t baseline_count = baseline.size();
  for (const auto& info : baseline) {
    EXPECT_FALSE(info->is_custom) << "built-in routine must not be is_custom";
  }

  // Create a valid cron-scheduled custom routine.
  bool create_ok = false;
  handler->CreateRoutine(
      "My Recipe", "Summarize tabs",
      std::optional<std::string>("0 9 * * *"), std::nullopt,
      base::BindLambdaForTesting([&](bool ok) { create_ok = ok; }));
  EXPECT_TRUE(create_ok);

  // ListAll now contains exactly one custom routine with our fields.
  std::vector<maho_routines::mojom::RoutineInfoPtr> after_create =
      ListAll(handler.get());
  EXPECT_EQ(after_create.size(), baseline_count + 1);

  std::string custom_id;
  int custom_count = 0;
  for (const auto& info : after_create) {
    if (info->is_custom) {
      ++custom_count;
      custom_id = info->id;
      EXPECT_EQ(info->name, "My Recipe");
      EXPECT_EQ(info->cron, "0 9 * * *");
      ASSERT_TRUE(info->schedule.has_value());
      EXPECT_EQ(info->schedule.value(), "0 9 * * *");
    }
  }
  EXPECT_EQ(custom_count, 1);
  ASSERT_FALSE(custom_id.empty());

  // Delete it.
  bool delete_ok = false;
  handler->DeleteRoutine(
      custom_id, base::BindLambdaForTesting([&](bool ok) { delete_ok = ok; }));
  EXPECT_TRUE(delete_ok);

  // Deleting again reports failure (not found).
  bool delete_again_ok = true;
  handler->DeleteRoutine(
      custom_id,
      base::BindLambdaForTesting([&](bool ok) { delete_again_ok = ok; }));
  EXPECT_FALSE(delete_again_ok);

  // ListAll is back to baseline with no custom routines.
  std::vector<maho_routines::mojom::RoutineInfoPtr> after_delete =
      ListAll(handler.get());
  EXPECT_EQ(after_delete.size(), baseline_count);
  for (const auto& info : after_delete) {
    EXPECT_FALSE(info->is_custom);
  }
}

// An invalid cron schedule must be rejected by CreateRoutine.
TEST_F(MahoRoutinesPageHandlerTest, CreateRejectsInvalidCron) {
  auto handler = MakeHandler();

  bool ok = true;
  handler->CreateRoutine(
      "Bad", "x", std::optional<std::string>("not a cron"), std::nullopt,
      base::BindLambdaForTesting([&](bool result) { ok = result; }));
  EXPECT_FALSE(ok);

  // The rejected routine must not appear in ListAll.
  std::vector<maho_routines::mojom::RoutineInfoPtr> routines =
      ListAll(handler.get());
  for (const auto& info : routines) {
    EXPECT_FALSE(info->is_custom);
  }
}

// Empty name/prompt must be rejected.
TEST_F(MahoRoutinesPageHandlerTest, CreateRejectsEmptyNameOrPrompt) {
  auto handler = MakeHandler();

  bool ok = true;
  handler->CreateRoutine(
      "", "prompt", std::nullopt, std::nullopt,
      base::BindLambdaForTesting([&](bool result) { ok = result; }));
  EXPECT_FALSE(ok);
}

// Deleting an id that does not exist must report failure.
TEST_F(MahoRoutinesPageHandlerTest, DeleteUnknownReturnsFalse) {
  auto handler = MakeHandler();

  bool ok = true;
  handler->DeleteRoutine(
      "does-not-exist",
      base::BindLambdaForTesting([&](bool result) { ok = result; }));
  EXPECT_FALSE(ok);
}

}  // namespace
