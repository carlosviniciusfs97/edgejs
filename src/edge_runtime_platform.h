#ifndef EDGE_RUNTIME_PLATFORM_H_
#define EDGE_RUNTIME_PLATFORM_H_

#include "node_api.h"
#include "unofficial_napi.h"

using EdgeRuntimePlatformTaskCallback = void (*)(napi_env env, void* data);
using EdgeRuntimePlatformTaskCleanup = void (*)(napi_env env, void* data);

enum EdgeRuntimePlatformTaskFlags : int {
  kEdgeRuntimePlatformTaskNone = 0,
  kEdgeRuntimePlatformTaskRefed = 1 << 0,
};

// Queue a native immediate/platform task for the current env. Tasks run on the
// owning thread before JS immediates, mirroring Node's native immediate queue.
// Immediate-task APIs are owning-thread-only; cross-thread engine work must use
// the foreground task enqueue hook instead.
napi_status EdgeRuntimePlatformEnqueueTask(napi_env env,
                                         EdgeRuntimePlatformTaskCallback callback,
                                         void* data,
                                         EdgeRuntimePlatformTaskCleanup cleanup,
                                         int flags);

// Drain queued native immediate tasks. Returns the number of tasks run.
size_t EdgeRuntimePlatformDrainImmediateTasks(napi_env env, bool only_refed = false);

bool EdgeRuntimePlatformHasImmediateTasks(napi_env env);
bool EdgeRuntimePlatformHasRefedImmediateTasks(napi_env env);

// Prepare Edge's foreground queue and add its immutable callback and target to
// the environment hook table. The caller commits the complete table with the
// provider in one attachment transition.
napi_status EdgeRuntimePlatformPrepareEnvHooks(
    napi_env env,
    unofficial_napi_env_hooks* hooks);

napi_status EdgeRuntimePlatformEnqueueForegroundTask(napi_env env,
                                                    EdgeRuntimePlatformTaskCallback callback,
                                                    void* data,
                                                    EdgeRuntimePlatformTaskCleanup cleanup,
                                                    uint64_t delay_millis = 0);

napi_status EdgeRuntimePlatformAddRef(napi_env env);
napi_status EdgeRuntimePlatformReleaseRef(napi_env env);

// Drain Edge-owned foreground tasks that were posted by the engine adapter.
napi_status EdgeRuntimePlatformDrainTasks(napi_env env);
void EdgeRunRuntimePlatformEnvCleanup(napi_env env);

#endif  // EDGE_RUNTIME_PLATFORM_H_
