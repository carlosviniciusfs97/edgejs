#ifndef EDGE_INTERNAL_BINDING_BINDING_MODULE_WRAP_H_
#define EDGE_INTERNAL_BINDING_BINDING_MODULE_WRAP_H_

#include "node_api.h"
#include "unofficial_napi.h"

namespace internal_binding {

// Resolves Edge's JavaScript ModuleWrap object to the provider-owned module
// resource retained by its native instance. The handle remains owned by the
// ModuleWrap and is valid only while that wrapper is alive.
unofficial_napi_module GetModuleWrapHandle(napi_env env, napi_value wrapper);

}  // namespace internal_binding

#endif  // EDGE_INTERNAL_BINDING_BINDING_MODULE_WRAP_H_
