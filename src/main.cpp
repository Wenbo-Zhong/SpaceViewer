#include "selftest.hpp"

#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <shellapi.h>
#include <cstdio>

int spaceviewer_main(int argc, char** argv);

static bool hasSelfTest() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool found = false;
  if (argv) {
    for (int i = 1; i < argc; ++i) {
      if (wcscmp(argv[i], L"--self-test") == 0) found = true;
    }
    LocalFree(argv);
  }
  return found;
}

static void attachConsole() {
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
#if defined(_MSC_VER)
  FILE* out = nullptr;
  FILE* err = nullptr;
  freopen_s(&out, "CONOUT$", "w", stdout);
  freopen_s(&err, "CONOUT$", "w", stderr);
#else
  std::freopen("CONOUT$", "w", stdout);
  std::freopen("CONOUT$", "w", stderr);
#endif
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  if (hasSelfTest()) {
    attachConsole();
    return runSelfTest();
  }
  return spaceviewer_main(0, nullptr);
}
#else
int spaceviewer_main(int argc, char** argv);

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--self-test") == 0) return runSelfTest();
  }
  return spaceviewer_main(argc, argv);
}
#endif
