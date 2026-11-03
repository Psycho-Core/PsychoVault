// I use this executable only in the sandbox test harness. It is not a page API.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
int main(int argc, char **argv) {
  HANDLE token = nullptr;
  DWORD is_appcontainer = 0;
  DWORD token_error = 0;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    DWORD returned = 0;
    if (!GetTokenInformation(token, TokenIsAppContainer, &is_appcontainer, sizeof(is_appcontainer),
                             &returned))
      token_error = GetLastError();
    CloseHandle(token);
  } else {
    token_error = GetLastError();
  }
  std::string target = argc > 1 ? argv[1] : "C:\\Windows\\win.ini";
  HANDLE file = CreateFileA(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  DWORD file_error = file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
  bool file_denied = file == INVALID_HANDLE_VALUE &&
                     (file_error == ERROR_ACCESS_DENIED || file_error == ERROR_SHARING_VIOLATION);
  std::cout << "{\"appContainer\":" << is_appcontainer << ",\"tokenError\":" << token_error
            << ",\"fileDenied\":" << file_denied << ",\"fileError\":" << file_error
            << ",\"networkTested\":false}\n";
  return 0;
}
