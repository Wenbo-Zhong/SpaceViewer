#include "tests/selftest.hpp"

#include <cstring>

int runRefactorTests();

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--self-test") == 0) return runSelfTest();
  if (argc > 1 && std::strcmp(argv[1], "--refactor-test") == 0) return runRefactorTests();
  int self = runSelfTest();
  int refactor = runRefactorTests();
  return self || refactor ? 1 : 0;
}
