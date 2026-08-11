# WASIX CI tail failures

| | | Remarks |
| --- | --- | --- |
| **Status** | 🟢 | Resolved at the platform-capability and WASIX socket boundaries. |
| **Severity** | High | Two late failures keep the otherwise-clean V8 WASIX lane red. |

## Baseline

After aligning the Wasmer and Edge N-API revisions and fixing shared-import
ownership, the CI-equivalent serial lane completed with:

```text
1675 passed, 2 failed
```

The failures were:

1. `parallel/test-http2-async-local-storage.js` received one
   `NGHTTP2_REFUSED_STREAM` after more than 1,200 preceding tests. A direct
   rerun passed immediately.
2. `known_issues/test-dgram-bind-shared-ports-after-port-0.js` timed out. A
   focused harness rerun reproduced the timeout deterministically.

The second test requires cluster to share a bound UDP descriptor with a child
process. WASI does not expose the Unix descriptor-passing mechanism needed for
that operation. The repository's `fix/wasix-skip-dgram-shared-ports` branch
already documents this boundary and excludes the corresponding negative test;
an expected-failure test still fails the harness when it hangs instead of
terminating with the expected error.

## Diagnosis

LLDB showed both failures waiting for guest or host progress, with no native
crash.

The dgram test depends on transferring a bound UDP descriptor from a cluster
parent to a child. WASI has no descriptor-passing/`SCM_RIGHTS` contract. Its
positive sibling was already excluded for that capability boundary, so the
known-issues variant belongs in the same skip group: it otherwise waits forever
for a transfer the platform cannot perform.

The HTTP/2 failure reproduced intermittently in focused WASIX runs but not in
native Edge. A fixed-size native trace showed that the client serialized both
HEADERS frames before it received GOAWAY, while the server observed only the
first stream. That ruled out Edge stream creation and task-queue visibility.

WASIX implemented stream `fd_write` by awaiting one
`VirtualConnectedSocket::send` call per guest iovec. HTTP/2's first HEADERS
iovec therefore became visible to the peer before the second iovec crossed the
host boundary, even though both belonged to one guest `writev`. The peer could
respond and close between those sends, producing `NGHTTP2_REFUSED_STREAM` for
the second stream.

## Fix

WASIX now coalesces stream-socket iovecs before crossing the scalar virtual
socket API, preserving the ordering and visibility of one guest `writev` as
one host send. Allocation failure returns `ENOMEM`. Datagram size limits remain
unchanged. The existing partial-write fixture now asserts that both small
iovecs cross in one host operation even when the peer closes after that send.

No HTTP/2 or task-queue behavior changed in Edge. Experiments that deferred
GOAWAY handling or changed read pausing did not solve the failure and were
removed.

## Verification

- `stream-tcp-writev-partial`: `20/20` direct fixture runs passed.
- `stream-tcp-writev`: `20/20` direct fixture runs passed.
- Focused HTTP/2 test with tracing: `300/300` passed after the WASIX fix.
- Focused HTTP/2 test after removing diagnostics: `100/100` passed.
- CI-equivalent `test-wasix-v8-only TEST_JOBS=1`: `1676 passed, 0 failed`.
- CI-equivalent `test-wasix-v8-intl`: all supported locale tests passed; the
  existing old-ICU compatibility skip remained expected.

The Rust integration harness could not provision its historical
`sysroot-v2026-05-12.1` from the local machine. The actual C fixtures were
therefore compiled and exercised directly with the rebuilt release Wasmer in
addition to running the complete Edge CI targets.

## Acceptance gates

- The skip list reflects a platform capability boundary, not a timing
  workaround or product-code conditional.
- The focused HTTP/2 test passes repeatedly without runtime changes.
- The CI-equivalent V8 WASIX and locale lanes finish with zero failures.
- No files under `lib/` change.
