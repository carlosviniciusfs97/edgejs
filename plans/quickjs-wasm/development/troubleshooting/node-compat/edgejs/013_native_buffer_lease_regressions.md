# Native buffer lease regressions

| | | Remarks |
| --- | --- | --- |
| **Status** | ✅ | Fixed and verified on 2026-08-11. |
| **Severity** | High | The supported native Node compatibility lane reports persistent failures. |

## Scope

Restore the native V8-backed Edge compatibility baseline after converting raw
BufferSource pointers to scoped leases. Keep every file under `lib/` unchanged
and preserve the provider-neutral lease contract needed by imported N-API.

The 2026-08-11 baseline is:

```text
total=1749 passed=1726 failed=23
```

The Makefile excludes 30 separately documented tests. A TAP rerun exposed
seven additional parallel-only failures; rerunning those serially reduced the
set to the same 23 persistent failures.

## Failure clusters

1. Eleven child-process assertions receive missing `stdout` or `stderr` because
   the native `napi_create_buffer` contract rejects a null data output pointer.
2. Six zero-length UDP sends fail to create their completion Buffer for the
   same reason.
3. Two default-host UDP tests time out.
4. StringDecoder and Diffie-Hellman ArrayBufferView tests fail after their
   byte-access paths moved to leases.
5. `test-domain-stack` fails at the provider event-loop checkpoint.
6. `test-zlib-convenience-methods` times out.

## Action plan

1. Reproduce every persistent failure directly and inspect its failing native
   path before editing.
2. Restore the standard N-API creation contract: creation functions receive
   required output pointers, while subsequent byte consumption still goes
   through leases.
3. Fix UDP zero-length and default-host behavior without platform branches.
4. Trace StringDecoder and crypto views to exact lease range/type errors and
   fix the shared native logic.
5. Trace checkpoint and zlib failures as lifecycle/state-machine defects; do
   not add retries, sleeps, or test exclusions.
6. Run the 23-test set serially, then `make test-only TEST_JOBS=8` and a serial
   confirmation for any parallel-only failures.

## Acceptance gates

- All selected native compatibility tests report zero failures.
- No new tests are added to `EDGE_NODE_TEST_SKIP_TESTS`.
- `git diff -- lib` remains empty.
- No new `#ifdef __wasi__` or provider-specific Edge behavior is introduced.
- The host-JavaScript lease integration suite still passes after native fixes.

## Root causes and fixes

### Required N-API creation outputs

`napi_create_buffer()` requires a non-null data output pointer even when Edge
will consume the resulting bytes through a lease. `spawn_sync` and the
zero-length UDP completion path passed null instead. Both paths now provide a
local creation output and keep all later byte access lease-based. This fixed
the child-process output failures and empty UDP sends without weakening the
ownership model.

### Float16Array byte ranges

Edge computed `Float16Array` byte lengths correctly, but the embedded V8 and
QuickJS lease providers omitted `napi_float16_array` from their element-size
tables. Exact-range acquisitions therefore compared a correct requested byte
length against a provider length that was half as large. Both provider lease
and guest-backed typed-array creation tables now define Float16 elements as
two bytes. A provider-level regression test acquires the full eight-byte range
of a four-element Float16 view.

This single provider fix restored the UDP default-host, StringDecoder,
Diffie-Hellman view, and zlib convenience tests. The zlib test now completes
in approximately 0.1 seconds instead of timing out.

### Checkpoint exceptions are guest state

The event loop treated every non-OK result from the combined provider
checkpoint and `nextTick` drain as provider infrastructure failure. A normal
JavaScript throw from the tick callback is reported as
`napi_pending_exception`; returning early prevented Edge's existing uncaught
exception and domain dispatcher from handling it. Checkpoint callers now
reserve the infrastructure error path for statuses other than `napi_ok` and
`napi_pending_exception`, then pass pending guest exceptions through the
existing dispatcher. No domain-specific behavior was added.

LLDB confirmed the failing tick drain returned `napi_pending_exception` (10)
before this change.

## Verification

```text
make test-only TEST_JOBS=8
All tests passed.
1749 passed, 0 failed

N-API V8 provider suite:     69 passed, 0 failed
N-API QuickJS provider suite: 71 passed, 0 failed
```

The existing 30 native compatibility exclusions are unchanged. No file under
`lib/` changed, and no new platform conditional was introduced.
