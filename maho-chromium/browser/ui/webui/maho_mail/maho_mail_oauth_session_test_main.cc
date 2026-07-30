// Copyright 2026 Maho Browser. All rights reserved.

#include "base/functional/bind.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "content/public/test/unittest_test_suite.h"
#include "content/test/content_test_suite.h"

int main(int argc, char** argv) {
  content::UnitTestTestSuite test_suite(
      new content::ContentTestSuite(argc, argv),
      base::BindRepeating(content::UnitTestTestSuite::CreateTestContentClients),
      /*child_mojo_config=*/std::nullopt);
  return base::LaunchUnitTests(argc, argv,
                               base::BindOnce(&content::UnitTestTestSuite::Run,
                                              base::Unretained(&test_suite)));
}
