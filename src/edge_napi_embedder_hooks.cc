#include "edge_napi_embedder_hooks.h"

#include <cstdint>
#include <mutex>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

#include "unofficial_napi.h"

uint64_t EdgeGetTotalMemory() {
#if defined(__wasi__) || defined(__wasm32__)
  // The JS Wasmer runtime currently caps shared WebAssembly memories at 1 GiB.
  // WASIX does not expose host physical-memory discovery, so report that
  // effective process budget instead of zero or an arbitrary stub value.
  return uint64_t{1024} * 1024 * 1024;
#else
#if defined(_SC_PHYS_PAGES)
  const long pages = sysconf(_SC_PHYS_PAGES);
#if defined(_SC_PAGE_SIZE)
  const long page_size = sysconf(_SC_PAGE_SIZE);
#elif defined(_SC_PAGESIZE)
  const long page_size = sysconf(_SC_PAGESIZE);
#else
  const long page_size = 0;
#endif
  if (pages > 0 && page_size > 0) {
    return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
  }
#endif
  return 0;
#endif
}

namespace {

napi_status GetEmbedderMemoryInfo(void* /*target*/,
                                  unofficial_napi_embedder_memory_info* info_out) {
  if (info_out == nullptr) return napi_invalid_arg;
  info_out->total_memory = EdgeGetTotalMemory();
  info_out->constrained_memory = 0;
  return napi_ok;
}

}  // namespace

void EdgeInstallNapiEmbedderHooks() {
  static std::once_flag once;
  std::call_once(once, []() {
    unofficial_napi_embedder_hooks hooks{};
    hooks.memory_info_callback = GetEmbedderMemoryInfo;
    (void)unofficial_napi_set_embedder_hooks(&hooks);
  });
}
