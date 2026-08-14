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
    DeleteRefIfAny(env, &tick_entry_ref);
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
  napi_ref tick_entry_ref = nullptr;
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
  (void)unofficial_napi_process_microtasks(env);
  return internal_binding::Undefined(env);
}

static napi_value TaskQueueSetTickCallback(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1] = {nullptr};
  if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) return nullptr;

  auto& st = GetTaskQueueState(env);

  napi_valuetype t = napi_undefined;
  if (napi_typeof(env, argv[0], &t) == napi_ok && t == napi_function) {
    // Capture the callback and pristine Reflect.apply during Node bootstrap.
    // The process receiver is retained separately: V8 uses it for the native
    // process.processTicksAndRejections stack label even though the closure
    // does not otherwise depend on its dynamic `this` value.
    static constexpr char kTickEntryFactorySource[] =
        "((apply) => (recv, callback) => "
        "function processTicksAndRejections() {"
        "  return apply(callback, recv, []);"
        "})(Reflect.apply)";
    napi_value source = nullptr;
    napi_value factory = nullptr;
    napi_value global = nullptr;
    napi_value process = nullptr;
    napi_value entry = nullptr;
    if (napi_create_string_utf8(
            env, kTickEntryFactorySource, NAPI_AUTO_LENGTH, &source) != napi_ok ||
        source == nullptr ||
        napi_run_script(env, source, &factory) != napi_ok ||
        factory == nullptr ||
        napi_get_global(env, &global) != napi_ok ||
        global == nullptr) {
      return nullptr;
    }
    if (napi_get_named_property(env, global, "process", &process) != napi_ok ||
        process == nullptr) {
      process = global;
    }
    napi_value factory_argv[2] = {process, argv[0]};
    if (napi_call_function(env, global, factory, 2, factory_argv, &entry) != napi_ok ||
        entry == nullptr) {
      return nullptr;
    }
    napi_ref new_entry_ref = nullptr;
    if (napi_create_reference(env, entry, 1, &new_entry_ref) != napi_ok ||
        new_entry_ref == nullptr) {
      return nullptr;
    }
    napi_ref new_receiver_ref = nullptr;
    if (napi_create_reference(env, process, 1, &new_receiver_ref) != napi_ok ||
        new_receiver_ref == nullptr) {
      DeleteRefIfAny(env, &new_entry_ref);
      return nullptr;
    }
    DeleteRefIfAny(env, &st.tick_entry_ref);
    DeleteRefIfAny(env, &st.tick_receiver_ref);
    st.tick_entry_ref = new_entry_ref;
    st.tick_receiver_ref = new_receiver_ref;
  } else {
    DeleteRefIfAny(env, &st.tick_entry_ref);
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
  if (state == nullptr || state->tick_entry_ref == nullptr) {
    return napi_ok;
  }
  if (state->tick_receiver_ref == nullptr) {
    return napi_generic_failure;
  }

  edge::HandleScope scope(env);

  napi_value tick_entry = nullptr;
  napi_status status = napi_get_reference_value(env, state->tick_entry_ref, &tick_entry);
  if (status != napi_ok || tick_entry == nullptr) {
    return status == napi_ok ? napi_generic_failure : status;
  }

  napi_value tick_receiver = nullptr;
  status = napi_get_reference_value(
      env, state->tick_receiver_ref, &tick_receiver);
  if (status != napi_ok || tick_receiver == nullptr) {
    return status == napi_ok ? napi_generic_failure : status;
  }

  // Both the closure and its receiver were captured at setup time, so dispatch
  // is independent of mutable user globals.
  napi_value ignored = nullptr;
  status = napi_call_function(
      env, tick_receiver, tick_entry, 0, nullptr, &ignored);
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
