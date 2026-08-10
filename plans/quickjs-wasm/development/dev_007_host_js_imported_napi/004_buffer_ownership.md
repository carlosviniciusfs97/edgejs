# Host-JavaScript Buffer Ownership

The imported backend has two physically separate memories: JavaScript
ArrayBuffers in the host engine and native pointers in the Edge WebAssembly
guest. Treating a copied pointer as if both sides shared one backing store is
the source of the earlier corruption, stale-state, and performance bugs.

The implementation follows three ownership rules:

1. Persistent control state that both JavaScript and native code mutate is
   created as a typed-array view over guest WebAssembly memory. There is one
   backing store and therefore no synchronization protocol.
2. Bulk data owned by JavaScript stays host-owned. Native code acquires an
   exact byte range with an explicit read, write, or read/write mode and
   releases it at the end of the operation.
3. Standard N-API raw pointers remain a compatibility fallback. Their mirrored
   snapshots are synchronized only at JavaScript/native re-entry boundaries;
   new hot paths must not depend on an unbounded mirrored pointer lifetime.

## Synchronous compression continuation

Node's synchronous zlib loop reuses one input Buffer while repeatedly supplying
small output Buffers. Acquiring the remaining input range on every iteration
made decompression quadratic: a large archive was copied again for every 16 KiB
of output.

The native compression handle now retains one read-only input snapshot only
when the output was exhausted. Reuse requires all of the following:

- the same JavaScript object identity;
- the exact offset after the bytes consumed by the preceding write;
- the exact remaining length reported by the compression context; and
- another synchronous write on the same handle.

Any mismatch, asynchronous write, completed input, non-full output, error, or
close releases the snapshot. This preserves the existing Node library contract
without modifying `lib/zlib.js`, while reducing the large Next.js archive's
gunzip time from roughly 54 seconds to under one second in the browser test.

## Regression gates

- `git diff -- lib` must remain empty.
- The imported-module validator must reject embedded engines and legacy import
  layouts.
- `edge --version` and `node --version` must both pass.
- The browser Next.js gate must fetch, install offline, start `pnpm dev`, and
  render the preview.
- The SDK must be built from `/Users/syrusakbary/Development/wasmer` with exnref
  WebAssembly exceptions.
