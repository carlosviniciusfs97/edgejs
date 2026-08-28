#include "edge_napi_embedder_hooks.h"

#include <cstdint>
#include "uv.h"

uint64_t EdgeGetTotalMemory() {
  return uv_get_total_memory();
}

void EdgeInitializeNapiEnvCreateOptions(
    unofficial_napi_env_create_options* options) {
  if (options == nullptr) return;
  *options = unofficial_napi_env_create_options{};
  options->size = sizeof(*options);
  options->version = UNOFFICIAL_NAPI_ENV_CREATE_OPTIONS_VERSION;
  options->total_memory = EdgeGetTotalMemory();
  options->constrained_memory = uv_get_constrained_memory();
}
