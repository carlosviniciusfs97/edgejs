#include "edge_task_queue.h"

#include "edge_environment.h"
#include "edge_handle_scope.h"
#include "edge_util.h"
#include "internal_binding/helpers.h"
#include "unofficial_napi.h"

namespace {

void DeleteRefIfAny(napi_env env, napi_ref* ref_slot);

struct TaskQueueBindingState {
  explicit TaskQueueBindingState(napi_env env_in) : env(env_in) {}
  ~TaskQueueBindingState() {
    DeleteRefIfAny(env, &binding_ref);
    DeleteRefIfAny(env, &tick_callback_ref);
    DeleteRefIfAny(env, &tick_receiver_ref);
    DeleteRefIfAny(env, &promise_reject_callback_ref);
    tick_info_fields = nullptr;
    if (auto* environment = EdgeEnvironmentGet(env); environment != nullptr) {
      environment->tick_info()->fields = nullptr;
      DeleteRefIfAny(env, &environment->tick_info()->ref);
    }
  }

  napi_env env = nullptr;
  napi_ref binding_ref = nullptr;
  napi_ref tick_callback_ref = nullptr;
  napi_ref tick_receiver_ref = nullptr;
  napi_ref promise_reject_callback_ref = nullptr;
  int32_t* tick_info_fields = nullptr;
};

void DeleteRefIfAny(napi_env env, napi_ref* ref_slot) {
  if (env == nullptr || ref_slot == nullptr || *ref_slot == nullptr) return;
  napi_delete_reference(env, *ref_slot);
  *ref_slot = nullptr;
}

TaskQueueBindingState& GetTaskQueueState(napi_env env) {
  return EdgeEnvironmentGetOrCreateSlotData<TaskQueueBindingState>(
      env, kEdgeEnvironmentSlotTaskQueueBindingState);
}

napi_value FailTickCallbackSetup(napi_env env) {
  bool pending = false;
  if (napi_is_exception_pending(env, &pending) != napi_ok || !pending) {
    (void)napi_throw_error(
        env, "ERR_INTERNAL_ASSERTION", "Failed to install tick callback");
  }
  return nullptr;
}

static napi_value TaskQueueEnqueueMicrotask(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1] = {nullptr};
  if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) return nullptr;

  if (unofficial_napi_enqueue_microtask(env, argv[0]) == napi_ok) {
    return internal_binding::Undefined(env);
  }

  napi_value global = nullptr;
  if (napi_get_global(env, &global) != napi_ok || global == nullptr) return nullptr;

  napi_value queue_microtask = nullptr;
  if (napi_get_named_property(env, global, "queueMicrotask", &queue_microtask) == napi_ok &&
      queue_microtask != nullptr) {
    napi_valuetype t = napi_undefined;
    if (napi_typeof(env, queue_microtask, &t) == napi_ok && t == napi_function) {
      napi_value ignored = nullptr;
      napi_call_function(env, global, queue_microtask, 1, argv, &ignored);
    }
  }

  return internal_binding::Undefined(env);
}

static napi_value TaskQueueRunMicrotasks(napi_env env, napi_callback_info /*info*/) {
  uint32_t checkpoint_state = unofficial_napi_event_loop_checkpoint_state_none;
  (void)unofficial_napi_event_loop_checkpoint(
      env,
      unofficial_napi_event_loop_checkpoint_microtasks,
      false,
      &checkpoint_state);
  return internal_binding::Undefined(env);
}

static napi_value TaskQueueSetTickCallback(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1] = {nullptr};
  if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) return nullptr;

  auto& st = GetTaskQueueState(env);

  napi_valuetype t = napi_undefined;
  if (napi_typeof(env, argv[0], &t) == napi_ok && t == napi_function) {
    // The C++ checkpoint is the trampoline. Retain the original JavaScript
    // callback and receiver directly so dispatch adds no synthetic JS frame
    // and performs no runtime source compilation.
    napi_value global = nullptr;
    napi_value process = nullptr;
    if (napi_get_global(env, &global) != napi_ok || global == nullptr) {
      return FailTickCallbackSetup(env);
    }
    if (napi_get_named_property(env, global, "process", &process) != napi_ok ||
        process == nullptr) {
      bool pending = false;
      if (napi_is_exception_pending(env, &pending) == napi_ok && pending) {
        return nullptr;
      }
      process = global;
    }
    napi_ref new_callback_ref = nullptr;
    if (napi_create_reference(env, argv[0], 1, &new_callback_ref) != napi_ok ||
        new_callback_ref == nullptr) {
      return FailTickCallbackSetup(env);
    }
    napi_ref new_receiver_ref = nullptr;
    if (napi_create_reference(env, process, 1, &new_receiver_ref) != napi_ok ||
        new_receiver_ref == nullptr) {
      DeleteRefIfAny(env, &new_callback_ref);
      return FailTickCallbackSetup(env);
    }
    DeleteRefIfAny(env, &st.tick_callback_ref);
    DeleteRefIfAny(env, &st.tick_receiver_ref);
    st.tick_callback_ref = new_callback_ref;
    st.tick_receiver_ref = new_receiver_ref;
  } else {
    DeleteRefIfAny(env, &st.tick_callback_ref);
    DeleteRefIfAny(env, &st.tick_receiver_ref);
  }

  return internal_binding::Undefined(env);
}

static napi_value TaskQueueSetPromiseRejectCallback(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1] = {nullptr};
  if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) return nullptr;

  // Install V8's promise-reject callback on every provider. On the embedded
  // providers (bundled-v8 / quickjs) this links directly; on the imports
  // provider it resolves to the N-API bridge import, which tells the host to
  // wire PromiseRejectCallback and dispatch rejections back into the guest.
  // Without this the imports lane never emits 'unhandledRejection' (and the
  // captureRejections / promise-unhandled diagnostics never fire).
  (void)unofficial_napi_set_promise_reject_callback(env, argv[0]);

  auto& st = GetTaskQueueState(env);
  DeleteRefIfAny(env, &st.promise_reject_callback_ref);

  napi_valuetype t = napi_undefined;
  if (napi_typeof(env, argv[0], &t) == napi_ok && t == napi_function) {
    napi_create_reference(env, argv[0], 1, &st.promise_reject_callback_ref);
  }

  return internal_binding::Undefined(env);
}

}  // namespace

napi_value EdgeGetOrCreateTaskQueueBinding(napi_env env) {
  if (env == nullptr) return nullptr;

  auto& st = GetTaskQueueState(env);
  if (st.binding_ref != nullptr) {
    napi_value existing = nullptr;
    if (napi_get_reference_value(env, st.binding_ref, &existing) == napi_ok && existing != nullptr) {
      return existing;
    }
  }

  napi_value binding = nullptr;
  if (napi_create_object(env, &binding) != napi_ok || binding == nullptr) return nullptr;

  auto define_method = [&](const char* name, napi_callback cb) -> bool {
    napi_value fn = nullptr;
    if (napi_create_function(env, name, NAPI_AUTO_LENGTH, cb, nullptr, &fn) != napi_ok || fn == nullptr) {
      return false;
    }
    return napi_set_named_property(env, binding, name, fn) == napi_ok;
  };

  if (!define_method("enqueueMicrotask", TaskQueueEnqueueMicrotask) ||
      !define_method("setTickCallback", TaskQueueSetTickCallback) ||
      !define_method("runMicrotasks", TaskQueueRunMicrotasks) ||
      !define_method("setPromiseRejectCallback", TaskQueueSetPromiseRejectCallback)) {
    return nullptr;
  }

  int32_t* fields = nullptr;
  napi_value tick_info = EdgeCreateSharedInt32Array(env, 2, &fields);
  if (tick_info == nullptr || fields == nullptr) return nullptr;
  fields[0] = 0;
  fields[1] = 0;
  st.tick_info_fields = fields;
  if (auto* environment = EdgeEnvironmentGet(env); environment != nullptr) {
    environment->tick_info()->fields = fields;
  }

  if (napi_set_named_property(env, binding, "tickInfo", tick_info) != napi_ok) return nullptr;
  if (auto* environment = EdgeEnvironmentGet(env); environment != nullptr) {
    DeleteRefIfAny(env, &environment->tick_info()->ref);
    napi_create_reference(env, tick_info, 1, &environment->tick_info()->ref);
  }

  napi_value promise_events = nullptr;
  if (napi_create_object(env, &promise_events) != napi_ok || promise_events == nullptr) return nullptr;
  auto set_event_const = [&](const char* name, int32_t value) -> bool {
    napi_value v = nullptr;
    return napi_create_int32(env, value, &v) == napi_ok && v != nullptr &&
           napi_set_named_property(env, promise_events, name, v) == napi_ok;
  };
  if (!set_event_const("kPromiseRejectWithNoHandler", 0) ||
      !set_event_const("kPromiseHandlerAddedAfterReject", 1) ||
      !set_event_const("kPromiseResolveAfterResolved", 2) ||
      !set_event_const("kPromiseRejectAfterResolved", 3)) {
    return nullptr;
  }
  if (napi_set_named_property(env, binding, "promiseRejectEvents", promise_events) != napi_ok) return nullptr;

  DeleteRefIfAny(env, &st.binding_ref);
  if (napi_create_reference(env, binding, 1, &st.binding_ref) != napi_ok || st.binding_ref == nullptr) {
    return nullptr;
  }

  return binding;
}

napi_status EdgeRunTaskQueueTickCallback(napi_env env, bool* called) {
  if (called != nullptr) {
    *called = false;
  }
  if (env == nullptr) {
    return napi_invalid_arg;
  }

  auto* state = EdgeEnvironmentGetSlotData<TaskQueueBindingState>(
      env, kEdgeEnvironmentSlotTaskQueueBindingState);
  if (state == nullptr || state->tick_callback_ref == nullptr) {
    return napi_ok;
  }
  if (state->tick_receiver_ref == nullptr) {
    return napi_generic_failure;
  }

  edge::HandleScope scope(env);

  napi_value tick_callback = nullptr;
  napi_status status =
      napi_get_reference_value(env, state->tick_callback_ref, &tick_callback);
  if (status != napi_ok || tick_callback == nullptr) {
    return status == napi_ok ? napi_generic_failure : status;
  }

  napi_value tick_receiver = nullptr;
  status = napi_get_reference_value(
      env, state->tick_receiver_ref, &tick_receiver);
  if (status != napi_ok || tick_receiver == nullptr) {
    return status == napi_ok ? napi_generic_failure : status;
  }

  // Both callback and receiver were captured at setup time, so dispatch is
  // independent of mutable user globals and introduces no JavaScript wrapper.
  napi_value ignored = nullptr;
  status = napi_call_function(
      env, tick_receiver, tick_callback, 0, nullptr, &ignored);
  if (status == napi_ok && called != nullptr) {
    *called = true;
  }
  return status;
}

bool EdgeGetTaskQueueFlags(napi_env env, bool* has_tick_scheduled, bool* has_rejection_to_warn) {
  if (has_tick_scheduled != nullptr) {
    *has_tick_scheduled = false;
  }
  if (has_rejection_to_warn != nullptr) {
    *has_rejection_to_warn = false;
  }
  if (env == nullptr) {
    return false;
  }

  int32_t* tick_info_fields = nullptr;
  if (auto* environment = EdgeEnvironmentGet(env); environment != nullptr) {
    tick_info_fields = environment->tick_info()->fields;
  }
  auto* state = EdgeEnvironmentGetSlotData<TaskQueueBindingState>(
      env, kEdgeEnvironmentSlotTaskQueueBindingState);
  if (tick_info_fields == nullptr && (state == nullptr || state->tick_info_fields == nullptr)) {
    return false;
  }
  if (tick_info_fields == nullptr) tick_info_fields = state->tick_info_fields;

  if (has_tick_scheduled != nullptr) {
    *has_tick_scheduled = tick_info_fields[0] != 0;
  }
  if (has_rejection_to_warn != nullptr) {
    *has_rejection_to_warn = tick_info_fields[1] != 0;
  }
  return true;
}
