// Copyright 2026 Maho Browser. All rights reserved.

#include "mojo/core/embedder/embedder.h"
#include "ui/views/views_test_suite.h"

int main(int argc, char** argv) {
  mojo::core::Init();
  return views::ViewsTestSuite(argc, argv).RunTests();
}
