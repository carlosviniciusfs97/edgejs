#ifndef EDGE_NAPI_EMBEDDER_HOOKS_H_
#define EDGE_NAPI_EMBEDDER_HOOKS_H_

#include <cstdint>

#include "unofficial_napi.h"

uint64_t EdgeGetTotalMemory();
void EdgeInitializeNapiEnvCreateOptions(
    unofficial_napi_env_create_options* options);

#endif  // EDGE_NAPI_EMBEDDER_HOOKS_H_
