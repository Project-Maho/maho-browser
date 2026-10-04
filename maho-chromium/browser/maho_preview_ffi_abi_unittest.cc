#include "testing/gtest/include/gtest/gtest.h"
#include "maho/third_party/maho/maho_ffi.h"

TEST(MahoPreviewFfiAbiTest, LegacyAndPrivacySymbolsLinkAndInvoke) {
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);

  const char* tab_id = "test-tab-123";
  uint8_t dummy_data[] = {1, 2, 3};
  maho_core_update_tab_preview(core, tab_id, dummy_data, sizeof(dummy_data));

  bool result = maho_core_update_tab_preview_with_privacy(
      core, tab_id, /*is_private=*/false, dummy_data, sizeof(dummy_data));
  EXPECT_FALSE(result);

  maho_core_free(core);
}
