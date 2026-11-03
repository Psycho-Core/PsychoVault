#define NOMINMAX
#include "../platform/process.hpp"
#include <fstream>
#include <iostream>
#include <windows.h>
static LRESULT CALLBACK sandbox_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  return DefWindowProcW(h, m, w, l);
}
int main() {
  using namespace pv;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = sandbox_wndproc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"PsychoVaultSandboxTest";
  RegisterClassExW(&wc);
  HWND hidden = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                                nullptr, nullptr, wc.hInstance, nullptr);
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  std::string dir;
  int n = WideCharToMultiByte(CP_UTF8, 0, temp, -1, nullptr, 0, nullptr, nullptr);
  dir.resize(n ? n - 1 : 0);
  if (n)
    WideCharToMultiByte(CP_UTF8, 0, temp, -1, dir.data(), n, nullptr, nullptr);
  std::string sentinel = dir + "psychovault-sandbox-sentinel.txt";
  {
    std::ofstream f(sentinel);
    f << "private profile sentinel\n";
  }
  if (hidden) {
    ShowWindow(hidden, SW_HIDE);
    UpdateWindow(hidden);
  }
  Process p;
  std::string error;
  if (!p.start(executable_dir() + "\\pv-renderer.exe", "--sandbox-probe \"" + sentinel + "\"", true,
               error)) {
    std::cerr << "sandbox start failed: " << error << "\n";
    return 1;
  }
  WaitForSingleObject(p.process, 3000);
  DWORD exit_code = 0;
  GetExitCodeProcess(p.process, &exit_code);
  std::string output;
  for (int i = 0; i < 100 && !output.empty() == false; ++i) {
    DWORD available = 0;
    if (!PeekNamedPipe(p.read, nullptr, 0, nullptr, &available, nullptr))
      break;
    if (available) {
      char buf[4096];
      DWORD got = 0;
      ReadFile(p.read, buf, std::min<DWORD>(available, sizeof(buf) - 1), &got, nullptr);
      buf[got] = 0;
      output += buf;
      if (output.find('\n') != std::string::npos)
        break;
    }
    Sleep(20);
  }
  p.terminate();
  DeleteFileA(sentinel.c_str());
  if (hidden)
    DestroyWindow(hidden);
  std::cout << "exitCode=" << exit_code << " outputBytes=" << output.size() << "\n"
            << output << "\n";
  bool ok = output.find("\"appContainer\":1") != std::string::npos &&
            output.find("\"fileDenied\":1") != std::string::npos;
  std::cout << (ok ? "SANDBOX PASS: AppContainer token active and profile file read denied; direct "
                     "network probe not run\n"
                   : "SANDBOX FAIL: expected AppContainer token and file denial\n");
  return ok ? 0 : 1;
}
