'use strict';

// Explicit resource management (`using` / `await using`) on the EdgeJS QuickJS
// provider. The engine gained this syntax with the quickjs-ng v0.15.1 bump
// (ECO-382); this exercises the core disposal semantics end to end through the
// edge binary, complementing the node stream dispose tests.
//
// Self-contained (uses only the `assert` builtin) so it can run directly via
// `make test-quickjs-lang` without the node-test harness. A non-zero exit
// signals failure; the async guard below fails the run if the async block is
// skipped or rejects.

const assert = require('assert');

// Well-known disposal symbols exist.
assert.strictEqual(typeof Symbol.dispose, 'symbol');
assert.strictEqual(typeof Symbol.asyncDispose, 'symbol');

// 1. Synchronous `using` disposes in reverse (LIFO) order at scope exit.
{
  const order = [];
  {
    using a = { [Symbol.dispose]() { order.push('a'); } };
    using b = { [Symbol.dispose]() { order.push('b'); } };
    order.push('body');
  }
  assert.deepStrictEqual(order, ['body', 'b', 'a']);
}

// 2. Disposal still runs when the block exits via a thrown error.
{
  const order = [];
  assert.throws(() => {
    using a = { [Symbol.dispose]() { order.push('dispose-a'); } };
    order.push('before-throw');
    throw new Error('boom');
  }, /boom/);
  assert.deepStrictEqual(order, ['before-throw', 'dispose-a']);
}

// 3. `null`/`undefined` resources are allowed and simply skipped.
{
  let disposed = false;
  {
    using a = null;
    using b = undefined;
    using c = { [Symbol.dispose]() { disposed = true; } };
  }
  assert.strictEqual(disposed, true);
}

// 4. When both the body and a disposer throw, the disposal error is surfaced as
//    a SuppressedError wrapping the original.
{
  assert.throws(() => {
    using a = { [Symbol.dispose]() { throw new Error('dispose-error'); } };
    throw new Error('body-error');
  }, (err) => {
    assert.strictEqual(err.name, 'SuppressedError');
    assert.strictEqual(err.error.message, 'dispose-error');
    assert.strictEqual(err.suppressed.message, 'body-error');
    return true;
  });
}

// 5. `await using` awaits async disposal, in LIFO order, after the body.
async function asyncDisposal() {
  const order = [];
  {
    await using a = {
      async [Symbol.asyncDispose]() {
        await Promise.resolve();
        order.push('a');
      },
    };
    await using b = {
      [Symbol.asyncDispose]() { order.push('b'); },
    };
    order.push('body');
  }
  assert.deepStrictEqual(order, ['body', 'b', 'a']);
}

// 6. `await using` disposal runs on an async throw too.
async function asyncDisposalOnThrow() {
  const order = [];
  await assert.rejects(async () => {
    await using a = {
      async [Symbol.asyncDispose]() { order.push('dispose'); },
    };
    order.push('before-throw');
    throw new Error('async-boom');
  }, /async-boom/);
  assert.deepStrictEqual(order, ['before-throw', 'dispose']);
}

// Guard: fail the process if the async block does not run to completion.
let asyncCompleted = false;
process.on('exit', () => {
  if (!asyncCompleted) {
    console.error('async explicit-resource-management block did not complete');
    process.exit(1);
  }
});

(async () => {
  await asyncDisposal();
  await asyncDisposalOnThrow();
  asyncCompleted = true;
  console.log('explicit resource management: OK');
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
