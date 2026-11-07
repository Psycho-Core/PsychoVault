// PsychoVault original source. Windows renderer-child entry point. The child
// owns an engine Host. Every fetch is requested from the broker over the process
// pipe; the AppContainer receives no network capability.
#ifdef _WIN32
#define NOMINMAX
#include "Psycho_renderer_main.hpp"
#include "Psycho_renderer_worker.hpp"
#include "../broker/Psycho_broker.hpp"
#include "../process/Psycho_process.hpp"
#include <windows.h>
#include <utility>

namespace pv::rt {
int run_renderer_child() {
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!input || input == INVALID_HANDLE_VALUE ||
      !output || output == INVALID_HANDLE_VALUE)
    return 2;
  Schemas schemas = Schemas::stock();
  const intptr_t read_handle = reinterpret_cast<intptr_t>(input);
  const intptr_t write_handle = reinterpret_cast<intptr_t>(output);
  auto read = [read_handle](std::string &bytes, Error &error) {
    return process_pipe_read(read_handle, bytes, error);
  };
  auto write = [write_handle](const std::string &bytes, Error &error) {
    return process_pipe_write(write_handle, bytes, error);
  };
  ProcessTransport transport(capabilities_for(Role::Renderer), schemas,
                             std::move(read), std::move(write), true,
                             renderer_process_limits());
  RendererWorker worker(transport, broker_fetcher(transport));
  return worker.run();
}
} // namespace pv::rt

#endif // _WIN32
