#ifndef EDGE_NAPI_EMBEDDER_HOOKS_H_
#define EDGE_NAPI_EMBEDDER_HOOKS_H_

#include <cstdint>

uint64_t EdgeGetTotalMemory();
void EdgeInstallNapiEmbedderHooks();

#endif  // EDGE_NAPI_EMBEDDER_HOOKS_H_
