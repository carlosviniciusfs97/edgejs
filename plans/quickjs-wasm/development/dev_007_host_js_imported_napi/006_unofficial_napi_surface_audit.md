# Unofficial N-API Surface Audit

| | | Remarks |
| --- | --- | --- |
| **Status** | ▶️ | Implementation active on `codex/napi-surface-reduction` from the current Edge and N-API `main` branches. |
| **Baseline** | 106 functions | Exported from `napi/include/unofficial_napi.h` on 2026-08-11. |
| **Goal** | Provider-neutral capabilities | Keep only semantics unavailable through standard Node-API, with explicit ownership and atomic state transitions. |

## Implementation status

The first implementation milestone reduced the exported header from 106 to 90
operations. Phase 1, all seven mechanical Phase 2 folds, and the first Phase 3
environment-configuration work, the process-memory snapshot fold, and atomic
source-map configuration, error-metadata snapshot, and message-resource
ownership are complete, reducing
the current surface to 71
operations:

- environment creation is one operation with nullable options;
- environment release is one indivisible operation with a nullable loop;
- structured clone has one optional-transfer-list form;
- Edge implements non-index property filtering with standard Node-API; and
- the worker stack limit is supplied only at environment creation; and
- heap-space statistics use one capacity-bounded bulk snapshot instead of a
  count operation followed by indexed provider calls; and
- profiler and heap-snapshot JSON is returned as an environment-owned
  `napi_value`; Edge copies it into Edge-owned `std::string` storage before a
  worker result crosses threads, so the provider-wide `free_buffer` allocator
  API is gone; and
- six independently mutable environment setters are replaced by one versioned,
  immutable, exactly-once `unofficial_napi_attach_env` transition. The hook
  table carries lifecycle, context-token, foreground scheduling, fatal-error,
  and OOM callbacks together; and
- process-global `set_embedder_hooks` and `set_flags_from_string` are gone.
  Engine flags, physical/constrained memory, resource limits, stack limit, and
  guest-heap ownership are versioned immutable environment-creation inputs;
  and
- ArrayBuffer memory is part of the atomic heap-statistics snapshot, so Edge no
  longer crosses the provider boundary through a second process-memory getter;
  and
- source-map enablement and its optional error-source callback are configured
  together, preventing providers from observing a partially updated pair; and
- current source positions, stderr formatting, and thrown-at text are returned
  from one provider observation, while the same operation can atomically
  consume preserved formatting state; and
- cross-environment values are represented by a typed opaque message. Edge
  holds queued messages through move-only ownership; `message_take` consumes
  the resource on success or failure, and `message_drop` is used only when an
  unconsumed queue entry is abandoned; and
- bytecode is a typed opaque resource opened through one versioned
  transaction. The provider validates an optional cache, reports rejection,
  and compiles the source fallback atomically. A tagged source descriptor
  prevents text and bytecode from being confused, while Edge owns artifacts
  with one move-only RAII wrapper; and
- module status, evaluation error, top-level-await presence, and async-graph
  presence are returned by one module-state snapshot. Providers observe the
  record once, and Edge applies Node's pre-instantiation visibility rule from
  the returned status rather than asking providers through a separate call;
  and
- module resources use the opaque `unofficial_napi_module` type end to end.
  Edge, provider implementations, and the Wasmer bridge can no longer
  accidentally pass an unrelated `void*` resource into a module transition;
  and
- dynamic-import and import-meta callbacks are installed through one
  versioned module-hooks descriptor. Updating either JavaScript-facing hook
  sends the provider a complete, atomic view of both callbacks.

The profiler/snapshot migration is covered by a native V8 provider contract,
native and wasm32 Wasmer guest-bridge compilation, and Edge's worker CPU
profile, heap profile, heap snapshot, and heapdump failure tests.

The attachment migration is covered by provider-neutral exactly-once contract
tests, complete V8 and QuickJS provider suites, and the Wasmer guest/host-JS
bridge. V8 binds platform, lifecycle, and fatal state atomically; QuickJS owns
and invokes the attached lifecycle callbacks exactly once; the Wasmer bridge
accepts one wasm32 hook descriptor and rejects duplicate attachment. Edge
builds the complete table before crossing the provider boundary and does not
select attachment behavior with `#ifdef __wasi__`.

Creation configuration is covered by provider-neutral descriptor validation,
complete V8 and QuickJS suites, native and wasm32 compilation against the SDK
Wasmer branch, and Edge's native and WASIX compatibility gates. The obsolete
N-API-side Edge memory-hook adapter and its unused shutdown-pump callback path
were removed; WASIX now reports its actual runtime memory ceiling through its
libuv platform implementation.

## Decision

The unofficial surface is too large, but the function count is a symptom rather
than the root problem. It currently mixes five different layers:

1. N-API environment construction and embedder lifecycle;
2. JavaScript-engine intrinsics unavailable through standard Node-API;
3. Edge and operating-system responsibilities such as the SIGINT watchdog;
4. opaque resources with real ownership, such as buffers, bytecode, modules,
   profiles, and cross-environment messages;
5. convenience getters which expose one field at a time from the same engine
   snapshot.

The target should not be one generic `unofficial_napi_call(opcode, ...)` API.
That would reduce exported symbol count while weakening types, ownership, and
reviewability. The target is a small set of strong capability families with
typed descriptors, typed opaque handles, and one operation for each atomic
semantic transition.

The first pass can remove 11 exports which have no Edge native consumer. A
second, mechanical pass can remove or fold another 7 without changing
semantics. The larger reductions should then follow their ownership boundary,
not be attempted as one ABI rewrite.

The implementation audit initially appeared to show that
`module_wrap_set_module_source_object` and
`module_wrap_get_module_source_object` were Edge-owned metadata. A complete
consumer trace disproved that: V8's source-phase resolver consumes the stored
object from the linked module record, and QuickJS retains the same provider
state. These operations therefore remain provider capabilities. The
host-JavaScript implementation must be completed instead of moving the value
into Edge.

## Rules for deciding whether an extension belongs

An unofficial operation is justified only if all of the following are true:

- Edge requires the behavior for Node compatibility.
- Standard Node-API cannot express the same behavior without observable
  JavaScript calls, mutable globals, loss of identity, or loss of ownership.
- The behavior belongs to the JavaScript provider rather than Edge, libuv,
  WASIX, or the Wasmer scheduler.
- Its inputs and outputs state their lifetime. A raw `void*` is not an adequate
  public type for bytecode, modules, messages, profiles, or buffer leases.
- Multiple outputs which must describe one instant are returned by one
  snapshot operation.

Conversely, an operation should be removed when it is unused, duplicates
standard Node-API, represents an Edge/OS responsibility, is an internal helper
exported accidentally, or is just a narrower form of another operation.

## Phase 1: remove unused and accidental exports

The following 11 functions have no consumer under `edgejs/src`. Generated
Wasmer bridge entries do not make them product requirements; those bridge
entries exist because the functions were present in the header.

| Function | Disposition | Reason |
| --- | --- | --- |
| `unofficial_napi_contextify_compile_function_for_cjs_loader` | Remove | Edge already builds the CJS-loader behavior from the generic compile-function and bytecode APIs. |
| `unofficial_napi_contextify_dispose_context` | Remove from the ABI | Production Edge never calls it; provider context records already follow their owning environment/value lifetime. Keep any necessary cleanup as provider-internal code. |
| `unofficial_napi_contextify_start_sigint_watchdog` | Remove | The watchdog is implemented by Edge/libuv in `binding_watchdog.cc`; it is not a JavaScript-engine primitive. |
| `unofficial_napi_contextify_stop_sigint_watchdog` | Remove | Same ownership error as the start operation. |
| `unofficial_napi_contextify_watchdog_has_pending_sigint` | Remove | Same ownership error as the start operation. |
| `unofficial_napi_destroy_env_instance` | Make private | It is called only by the V8 provider's own release implementation. Partial environment destruction must not be an exported state transition. |
| `unofficial_napi_get_caller_location` | Remove | Edge uses the call-site snapshot operation instead. |
| `unofficial_napi_get_current_stack_trace` | Remove | This differs from `get_call_sites` only by an internal skip count and has no Edge caller. |
| `unofficial_napi_get_edge_environment` | Remove | Edge owns its environment registry and never reads this provider field. |
| `unofficial_napi_module_wrap_import_module_dynamically` | Remove | Dynamic import enters through the registered provider callback; Edge never calls this reverse convenience wrapper. |
| `unofficial_napi_set_pending_exception` | Remove | It has no Edge caller and its intended operation is standard `napi_throw`. |

Removal means deleting the declaration, provider implementation or exported
wrapper, guest import mapping, bridge thunk, and obsolete tests together. It
does not mean leaving a compatibility stub which continues to enlarge the
Wasm import table.

## Phase 2: mechanical folds and standard Node-API replacements

These changes preserve the current architecture and can land independently.

| Current surface | Replacement | Net reduction |
| --- | --- | ---: |
| `create_env` + `create_env_with_options` | One `create_env` whose options pointer may be null. | 1 |
| `release_env` + `release_env_with_loop` | One release operation with a nullable shutdown-pump/loop field. Environment destruction remains provider-internal and indivisible. | 1 |
| `structured_clone` + `structured_clone_with_transfer` | One structured-clone operation with a nullable/undefined transfer list. | 1 |
| `get_own_non_index_properties` | `napi_get_all_property_names(..., napi_key_own_only, ..., napi_key_keep_numbers)` followed by filtering numeric keys in Edge. This preserves engine index classification and key ordering without a private provider call. | 1 |
| `set_stack_limit` after worker creation | Put the initial stack limit only in environment creation options. Edge already supplies it there before repeating the setter. | 1 |
| `get_heap_space_count` + indexed `get_heap_space_statistics` | One bulk heap-space snapshot using caller-provided capacity and returning the required count. | 1 |
| `free_buffer` | Profiling and snapshot operations return a `napi_value` string/byte value in the target environment; Edge copies it into its own cross-thread task before leaving that environment. | 1 |

`arraybuffer_view_has_buffer` should **not** be replaced mechanically with a
JavaScript `.buffer` access. V8's `ArrayBufferView::HasBuffer()` is an intrinsic
query and a JavaScript property access may materialize a buffer or invoke
observable behavior. It remains until a standard Node-API operation with the
same semantics exists.

## Phase 3: one environment attachment contract

Environment setup is the largest collection of weak setter-style APIs. The
provider currently accepts global hooks, an Edge pointer, three lifecycle hook
setters, a foreground-task setter, fatal callbacks, and a post-creation stack
limit. This permits partially configured environments and requires rollback
code after every setter.

Replace the setup sequence with two concepts:

```cpp
struct unofficial_napi_env_options {
  uint32_t size;
  uint32_t version;
  int32_t module_api_version;
  // Resource limits, initial stack limit, guest heap, engine flags, and
  // process-wide memory information needed before engine creation.
};

struct unofficial_napi_env_hooks {
  uint32_t size;
  uint32_t version;
  void* data;
  // Cleanup, destroy, context assign/unassign, foreground task enqueue,
  // fatal error, and OOM callbacks.
};

napi_status unofficial_napi_create_env(
    const unofficial_napi_env_options*, napi_env*, unofficial_napi_env_owner*);
napi_status unofficial_napi_attach_env(
    napi_env, const unofficial_napi_env_hooks*);
napi_status unofficial_napi_release_env(
    unofficial_napi_env_owner, void* shutdown_pump);
```

The exact names are provisional; the invariants are not:

- configuration required before engine creation is supplied at creation;
- Edge allocates all runtime state, then attaches one complete immutable hook
  table;
- imported host-JavaScript environments use the same attachment operation but
  have no `env_owner` to release;
- callback `data` points at one embedder-owned attachment state object;
  callbacks that need the Edge environment recover it from `napi_env`, removing
  both `set_edge_environment` and the unused getter;
- `destroy_env_instance` is not public, so callers cannot split destruction
  from isolate/provider release;
- global `set_embedder_hooks` and `set_flags_from_string` disappear. Their
  pre-creation inputs move into environment/runtime creation options;
- the host-JavaScript provider may report an unsupported capability, but Edge
  never selects a provider using `#ifdef __wasi__`.

The near-heap-limit callback remains a separate dynamic registration because
workers install and remove it during their lifetime. Promise hooks,
prepare-stack-trace, source-map callbacks, and rejection callbacks are also
runtime JavaScript state, not immutable embedder attachment hooks.

### Phase 3 implementation status

Phase 3 is complete. These six public operations are gone:

- `unofficial_napi_set_edge_environment`;
- `unofficial_napi_set_env_cleanup_callback`;
- `unofficial_napi_set_env_destroy_callback`;
- `unofficial_napi_set_context_token_callbacks`;
- `unofficial_napi_set_enqueue_foreground_task_callback`; and
- `unofficial_napi_set_fatal_error_callbacks`.

They are represented by the single `unofficial_napi_attach_env` operation, a
net reduction of five exports. The two process-global configuration setters
are also gone. Their required values now travel in the versioned
`unofficial_napi_env_create_options` descriptor and are consumed synchronously
before provider environment creation. No replacement setter or provider-kind
branch was introduced.

## Phase 4: strengthen opaque resources

### Buffer memory

Keep these primitives:

- `acquire_buffer_lease`;
- `release_buffer_lease`;
- guest-backed/shared control-block creation;
- provider-owned uninitialized ArrayBuffer creation.

Acquire and release are two real ownership transitions and should not be
collapsed. The release operation publishes writable bytes and drops the
retained JavaScript value. A C++ `EdgeBufferLease` wrapper makes the pair safe
for Edge; removing release from the ABI would merely hide an implicit leak or
copy-back point.

The shared typed-array operation is also distinct from a lease. It creates a
small control block which JavaScript and native code must observe
simultaneously. It should eventually use a typed opaque name such as
`create_shared_control_block`, but it must not be implemented as a transient
copy or a view over the entire WebAssembly memory.

### Cross-environment messages

The former names `serialize_value`, `deserialize_value`, and
`release_serialized_value` described the V8 implementation, not the contract.
The host-JavaScript provider correctly transports a host value through the
Wasmer cross-worker registry, whose underlying transport performs structured
clone/postMessage semantics. It does not need a second serializer.

They are now represented by an opaque message resource:

```cpp
unofficial_napi_message_create(source_env, value, &message);
unofficial_napi_message_take(destination_env, message, &value);  // consumes
unofficial_napi_message_drop(message);                           // if unconsumed
```

Three transitions remain necessary even though the names are clearer: a
queued message may be consumed in another environment or dropped because its
port closes. `take` consumes the handle on both success and failure so the
common success path cannot forget a separate release. Edge owns the handle
with a move-only RAII wrapper while it is queued.

The native Wasmer bridge stores message resources in a process-wide opaque
handle table rather than reusing scope-bound `napi_value` IDs. This is required
because a message can outlive its source handle scope and be taken by a
different environment. The host-JavaScript bridge uses the Wasmer message
registry with the same create/take/drop ownership contract.

Within one environment, keep only the single structured-clone operation with
an optional transfer list. It is not a substitute for the message handle,
because a `napi_value` cannot cross native isolates/environments.

### Bytecode

The bytecode API is conceptually correct—compiled state is an opaque
provider-owned artifact—but its inputs are weak:

- `void*` does not identify the resource type;
- `unofficial_napi_js_source` relies on exactly one of two nullable fields
  being set but has no discriminator;
- compile and deserialize duplicate the same source/filename/shape/options
  validation and require Edge to implement the fallback transaction.

Use a typed `unofficial_napi_bytecode` handle, a tagged source descriptor, and
one open operation with optional cache bytes:

```cpp
unofficial_napi_bytecode_open(env, &descriptor, &result);
unofficial_napi_bytecode_serialize(env, bytecode, &bytes);
unofficial_napi_bytecode_release(env, bytecode);
```

The result reports `cache_rejected` and `can_parse_as_module` where applicable.
The provider atomically validates cached bytes and compiles the fallback text,
so V8, QuickJS, and host JavaScript cannot drift in fallback policy. Serialize
and release remain genuine independent operations.

This migration is complete. The former compile and deserialize entry points
are represented by one `unofficial_napi_bytecode_open` operation, a net
reduction of one export. V8 and QuickJS share the same fallback contract; the
host-JavaScript provider retains source as its opaque artifact and rejects
cache bytes because the browser engine exposes no portable compiled-code
serialization. Empty supplied caches are rejected while still returning a
freshly compiled artifact, so Edge never implements a second fallback branch.

### Module wrap

Keep an opaque provider-owned module resource, but type it instead of exposing
`void*`. Consolidate only operations which are one semantic snapshot:

- replace source-text and synthetic creation with one tagged creation
  descriptor;
- replace the two module callback setters with one module-hooks table;
- return immutable module requests and top-level-await status as part of the
  creation result, then let Edge retain the request metadata and materialize
  the JavaScript result with the required identity semantics;
- replace `get_status`, `get_error`, and `has_async_graph` with one
  module-state snapshot;
- make `check_unsettled_top_level_await` take the opaque module handle instead
  of a JavaScript wrapper value;
- keep link, instantiate, async evaluate, sync evaluate, namespace,
  set-export, cached-data, and required-facade operations distinct because
  they perform different state transitions or return values with different
  lifetimes;
- keep source-object storage on the provider-owned module record because the
  engine consumes it while resolving source-phase imports.

Merging sync and async evaluate behind a boolean would not make the contract
stronger: one returns a synchronous value/error contract and the other returns
promise-driven evaluation.

The module-state migration is complete. It replaces four field-at-a-time
operations (`get_status`, `get_error`, `has_top_level_await`, and
`has_async_graph`) with one provider-neutral snapshot, a net reduction of
three exports. The module handle is also now typed across Edge, V8, QuickJS,
and the Wasmer bridge without changing its pointer-sized ABI. Consolidating
creation and hooks, and moving immutable request metadata into the creation
result remain separate module-resource improvements.

The module-hooks migration is complete. Two independently mutable callback
setters are now one versioned configuration operation, a net reduction of one
export. Edge retains the callbacks needed by its public binding methods and
sends both values whenever either public setter changes, so no provider can
observe a half-updated pair.

### Profilers

CPU and heap profiling should return typed opaque session handles rather than
numeric IDs and booleans. Stopping consumes a session. Heap snapshots are
one-shot and need no session. All result JSON should be returned as an
environment-owned `napi_value` and copied by Edge before a worker task crosses
threads. This removes the extension-wide `free_buffer` allocator convention.

## Phase 5: consolidate atomic snapshots and configuration

### Error metadata

The former error API read source positions, stderr formatting, thrown-at text,
and preserved formatting through four calls which could observe or consume
different provider state. They now use one metadata snapshot:

```cpp
struct unofficial_napi_error_metadata {
  napi_value source_line;
  napi_value script_resource_name;
  napi_value stderr_line;
  napi_value thrown_at;
  int32_t line_number;
  int32_t start_column;
  int32_t end_column;
  bool was_preserved;
};

unofficial_napi_get_error_metadata(env, error, mode, &metadata);
```

The mode selects current versus consume-preserved state.
`preserve_error_source_message` remains the explicit capture transition.
Source-map enablement and the source-map error callback use one atomic
configuration operation. All current formatting fields now come from the same
provider message, and consuming preserved formatting is one indivisible state
transition.

### Memory statistics

ArrayBuffer memory is now part of the heap-statistics snapshot and
`get_process_memory_info` has been removed. Edge derives `process.memoryUsage()`
and diagnostic-report heap values from that one snapshot. Heap-space statistics
are already returned in bulk rather than count-plus-index iteration. Heap-code
statistics remain separate because callers do not otherwise need to pay for it.

### Module state

The module-state snapshot described above should be immutable for the duration
of the call. This is stronger than four getters and cheaper across the Wasm
boundary, while avoiding a generic bag of unrelated module operations.

## Operations which should remain distinct

The following pairs or families may look mergeable but represent different
semantics and should remain explicit:

| Operations | Why they remain distinct |
| --- | --- |
| buffer lease acquire/release | Begin and end native pointer ownership; release may publish writes. |
| event-loop checkpoint / enqueue microtask / request interrupt | Drain/admit host work, enqueue JavaScript work, and schedule engine-thread native work are different directions. |
| terminate/cancel termination | Explicit worker termination and recovery during unwind are separate state transitions. |
| promise reject callback / promise hooks / mark handled / promise details | Registration, lifecycle observation, rejection bookkeeping, and introspection are not interchangeable. |
| message create/take/drop | Creation, cross-environment consumption, and cancellation each own the payload at a different point. |
| bytecode open/serialize/release | Live compiled state, persistent bytes, and destruction have different lifetimes. |
| module link/instantiate/evaluate | These are required module lifecycle transitions, not field accessors. |
| continuation data get/set | Provider continuation state is an engine intrinsic; replacing it with mutable JavaScript globals would be observably wrong. |
| low-memory notification / GC request | Production memory-pressure notification and test/explicit full-GC request have different intent. |

Engine introspection operations such as proxy details, preview entries,
constructor name, promise details, call sites, hash seed, and private-symbol
behavior should remain until a standard Node-API primitive with equivalent
non-observable semantics exists. Calling user-visible JavaScript properties is
not an equivalent replacement.

## Proposed migration order

1. Define one typed ABI inventory and generate declarations, bridge
   registration, and import-conformance checks from it. Provider
   implementations remain handwritten; this is not an opcode dispatcher.
2. Remove the 11 unused/accidental exports and their bridge plumbing.
3. Land the seven mechanical folds/replacements, one commit per capability.
4. Introduce the environment options/hooks structs with `size` and `version`,
   migrate native V8 and host-JavaScript providers, then remove the old
   setters.
5. Rename serialized payloads to typed message handles and make `take`
   consuming; migrate Edge queues to move-only ownership.
6. Introduce the tagged bytecode source/open transaction and typed module
   handles.
7. Consolidate error, module-state, and heap-space snapshots.
8. Convert profiler results to environment-owned values and remove
   `free_buffer`.
9. Rebuild Edge and assert both the exact extension import allowlist and the
   absence of embedded QuickJS/V8 symbols.
10. Run native V8, N-API provider tests, the WASIX Node compatibility
    selection, SDK host-JS tests, and browser wasmer-sh/Next.js acceptance
    before deleting compatibility shims.

Each phase must keep `edgejs/lib` unchanged and must use the same Edge logic for
native and WASIX. A provider may implement an operation differently, but Edge
must not choose an implementation with `#ifdef __wasi__`.

## Expected outcome

The completed removal, mechanical, attachment, configuration, heap-space,
memory-snapshot, source-map configuration, error-metadata snapshot,
message-resource ownership, bytecode transaction, module-state snapshot,
module-hooks configuration, and profiling-result phases reduce 106 exported
functions to 71. The remaining
module-resource changes can reduce it further, but their success criterion is
not a specific number. The success criterion is that every remaining extension
is either:

- one provider-owned engine capability unavailable in standard Node-API; or
- one explicit transition on a typed opaque resource whose ownership cannot be
  represented by `napi_value` alone.

That boundary is small enough to audit, implement consistently in V8 and host
JavaScript, and keep stable as Edge compatibility grows.
