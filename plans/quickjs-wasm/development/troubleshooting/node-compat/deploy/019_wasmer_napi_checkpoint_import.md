# Wasmer N-API checkpoint import alignment

| | | Remarks |
| --- | --- | --- |
| **Status** | 🟢 | Resolved by aligning the host bridge and guest import contract. |
| **Severity** | High | The V8-imports WASIX artifact cannot instantiate, so no compatibility test can start. |

## Baseline

The freshly rebuilt Edge WASIX artifact validates as engine-free with 110
standard N-API imports and 91 extension imports. Running the CI-equivalent V8
WASIX target with the local Wasmer binary fails before the first test:

```text
Error while importing "napi_extension_wasmer_v0".
"unofficial_napi_event_loop_checkpoint": unknown import.
Expected Function(FunctionType { params: [I32, I32, I32], results: [I32] })
```

Edge and its embedded N-API headers use the unified provider event-loop
checkpoint API. The Wasmer checkout at
`/Users/syrusakbary/Development/wasmer` pins an older `lib/napi` revision that
does not register that import. This is a host/guest ABI version mismatch, not a
Node behavior failure.

## Root cause and fix

Wasmer pinned N-API revision `b941407`, while Edge was built against revision
`e30b029`. The latter owns the unified event-loop checkpoint import. Updating
both consumers to the same N-API revision restored the missing host function.

The first aligned build then reported a duplicate `env.memory` import. WASIX
had already prepared the module's memory and table, but the N-API hook created
a second pair instead of extending the existing import object. N-API now
merges into the imports prepared by WASIX and reuses its memory and table. This
keeps the module instance under one memory/table owner rather than masking the
collision in Edge.

## Verification

- N-API WASIX library tests: `39 passed`.
- The local Wasmer release CLI rebuilt successfully with `--locked`.
- The pseudo-TTY compatibility test passes without the duplicate-import
  warning.
- The full V8 WASIX lane completes with `1676 passed, 0 failed`.
- The V8 WASIX locale lane passes.

## Acceptance gates

- The Edge WASIX artifact instantiates with the local Wasmer SDK binary.
- The unified checkpoint is provided by the N-API host bridge; Edge does not
  acquire a fallback import or platform conditional.
- The CI-equivalent V8 WASIX and locale suites pass.
- No file under `lib/` changes.
