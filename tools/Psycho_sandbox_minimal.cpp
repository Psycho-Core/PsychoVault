// I use this tiny no-engine probe only to separate AppContainer startup problems
// from renderer import problems. It is not a page API.
#ifndef _WIN32
#error "Windows sandbox probe"
#endif
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <string>
static bool send_text(const std::string &text) {
  unsigned char header[4];
  for (int i = 0; i < 4; ++i)
    header[i] = (unsigned char)(text.size() >> (8 * i));
  DWORD done = 0;
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  return WriteFile(out, header, 4, &done, nullptr) && done == 4 &&
         WriteFile(out, text.data(), DWORD(text.size()), &done, nullptr) && done == text.size();
}
int main(int argc, char **argv) {
  HANDLE token = nullptr;
  DWORD app = 0, token_error = 0;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    DWORD returned = 0;
    if (!GetTokenInformation(token, TokenIsAppContainer, &app, sizeof(app), &returned))
      token_error = GetLastError();
    CloseHandle(token);
  } else
    token_error = GetLastError();
  DWORD file_error = 0;
  bool file_denied = false;
  if (argc > 2) {
    HANDLE file = CreateFileA(argv[2], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    file_error = file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    if (file != INVALID_HANDLE_VALUE)
      CloseHandle(file);
    file_denied = file == INVALID_HANDLE_VALUE &&
                  (file_error == ERROR_ACCESS_DENIED || file_error == ERROR_SHARING_VIOLATION);
  }
  WSADATA wsa{};
  int startup = WSAStartup(MAKEWORD(2, 2), &wsa);
  int tcp = 0, udp = 0;
  if (!startup) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
      tcp = WSAGetLastError();
    else
      closesocket(s);
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
      udp = WSAGetLastError();
    else
      closesocket(s);
    WSACleanup();
  }
  bool network = !startup && tcp == WSAEACCES && udp == WSAEACCES;
  std::string json = "{\"tag\":\"sandbox-minimal\",\"appContainer\":" + std::to_string(app != 0) +
                     ",\"fileDenied\":" + (file_denied ? "true" : "false") +
                     ",\"networkDenied\":" + (network ? "true" : "false") +
                     ",\"tokenError\":" + std::to_string(token_error) +
                     ",\"fileError\":" + std::to_string(file_error) +
                     ",\"startupError\":" + std::to_string(startup) +
                     ",\"tcpError\":" + std::to_string(tcp) +
                     ",\"udpError\":" + std::to_string(udp) + "}";
  return send_text(json) ? 0 : 1;
}
