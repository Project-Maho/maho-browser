#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace maho::welcome {
namespace {

TEST(MahoWelcomeCompletionPolicyTest,
     RequiresPasswordSetupOnlyBeforeFirstCompletion) {
  EXPECT_TRUE(RequiresPasswordSetup(false));
  EXPECT_FALSE(RequiresPasswordSetup(true));
}

}  // namespace
}  // namespace maho::welcome
