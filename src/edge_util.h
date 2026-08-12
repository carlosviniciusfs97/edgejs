#ifndef EDGE_UTIL_H_
#define EDGE_UTIL_H_

#include <cstddef>
#include <cstdint>

#include "node_api.h"

napi_value EdgeCreateSharedTypedArray(napi_env env,
                                      napi_typedarray_type type,
                                      size_t length,
                                      void** data_out);
napi_value EdgeCreateSharedInt32Array(napi_env env,
                                      size_t length,
                                      int32_t** data_out);
napi_value EdgeCreatePrivateSymbolsObject(napi_env env);
napi_value EdgeCreatePerIsolateSymbolsObject(napi_env env);
napi_value EdgeInstallUtilBinding(napi_env env);
napi_value EdgeGetTypesBinding(napi_env env);

#endif  // EDGE_UTIL_H_
