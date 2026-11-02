// I use this executable only in the sandbox test harness. It is not a page API.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <string>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
int main(int argc, char **argv) {
  std::string target = argc > 1 ? argv[1] : "C:\\Windows\\win.ini";
  HANDLE file =
      CreateFileA(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  DWORD file_error = file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int network_error = sock == INVALID_SOCKET ? WSAGetLastError() : 0;
  if (sock != INVALID_SOCKET)
    closesocket(sock);
  WSACleanup();
  std::cout << "{\"fileDenied\":"
            << (file == INVALID_HANDLE_VALUE &&
                (file_error == ERROR_ACCESS_DENIED ||
                 file_error == ERROR_SHARING_VIOLATION))
            << ",\"fileError\":" << file_error << ",\"networkDenied\":"
            << (sock == INVALID_SOCKET && network_error == WSAEACCES)
            << ",\"networkError\":" << network_error << "}\n";
  return 0;
}
