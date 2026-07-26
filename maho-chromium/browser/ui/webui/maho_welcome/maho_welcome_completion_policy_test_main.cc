#include "base/test/test_suite.h"

int main(int argc, char** argv) {
  base::TestSuite test_suite(argc, argv);
  return test_suite.Run();
}
