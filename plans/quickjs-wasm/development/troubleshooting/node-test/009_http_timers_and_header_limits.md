# Node Test: HTTP timers and header limits

| | | Remarks |
| --- | --- | --- |
| **Status** | 🟢 | Resolved by provider-neutral event-loop checkpoint admission. |
| **Severity** | Medium | Affects HTTP edge-case tests around timers, warnings, and CLI option propagation. |

Affected tests:

- `parallel/test-http-keep-alive-timeout-race-condition`
- `client-proxy/test-http-proxy-request-invalid-char-in-url`
- `parallel/test-http-timeout-client-warning`
- `parallel/test-set-http-max-http-headers`

## What Was The Issue

The log shows a mix of HTTP timing and validation failures:

- `test-http-timeout-client-warning` emits a `TimeoutOverflowWarning`, but the
  warning object does not satisfy the test assertion.
- `test-set-http-max-http-headers` reports `ERR_INVALID_ARG_VALUE` through the
  test runner instead of completing its child-process checks.
- The keep-alive timeout race and invalid-character proxy request tests do not
  complete normally in the log.

The `test-set-http-max-http-headers` failure was not a parser or CLI option
propagation bug. Edge always used a nonblocking libuv turn and then relied on
the provider checkpoint to make progress. Embedded V8 and QuickJS implemented
the host-task checkpoint with a one-millisecond sleep even though no host event
loop was admitted. Socket callbacks therefore progressed through polling, and
the many short child/socket phases in this test accumulated roughly 21 seconds
of latency.

## Resolution

The common checkpoint result now distinguishes two facts:

- the provider still owns runnable work;
- the provider actually admitted a host-task turn.

The host-JavaScript provider sets the admission bit after its JSPI suspension.
Embedded providers perform their JavaScript checkpoint synchronously and leave
the bit clear. When there is no immediate Edge/provider work and no host turn
was admitted, Edge waits on the existing libuv event source with
`uv_run(UV_RUN_ONCE)`. The wait is selected from runtime state, not from
`__wasi__` or a JavaScript-engine conditional.

The HTTP header-limit test was removed from the WASIX slow-test list. The full
maintained native and serial WASIX compatibility selections pass for both V8
and QuickJS, including all four tests listed above.

Verification:

```sh
build-edge-quickjs-cli/edge test/parallel/test-http-timeout-client-warning.js
build-edge-quickjs-cli/edge --test-reporter=test/common/test-error-reporter.js --test-reporter-destination=stdout test/parallel/test-set-http-max-http-headers.js
build-edge-quickjs-cli/edge test/parallel/test-http-keep-alive-timeout-race-condition.js
build-edge-quickjs-cli/edge test/client-proxy/test-http-proxy-request-invalid-char-in-url.mjs
```

Full acceptance on 2026-08-12:

- native V8: 1,749/1,749;
- native QuickJS: 1,741/1,741;
- serial WASIX V8: 1,676/1,676;
- serial WASIX QuickJS: 1,675/1,675;
- wasmer-sh browser smoke and Next.js install/dev/preview: passed.
