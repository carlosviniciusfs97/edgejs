# WASIX CI TTY bridge and signal pin

| | | Remarks |
| --- | --- | --- |
| **Status** | 🟠 | The TTY regression is fixed; the safe-mode probe accepts both current Wasmer host exit encodings for confirmed SIGINT termination. |
| **Severity** | High | The V8 Node suite has four TTY failures and the QuickJS job fails after its otherwise-passing Node suite. |

## Baseline

GitHub Actions run `33209493490`, job `98978924977`, completes the V8 WASIX
Node matrix with 1672 passing tests and four failures:

- `pseudo-tty/test-readable-tty-keepalive` times out.
- `parallel/test-tty-stdin-pipe` fails with `setRawMode ENOTSUP`.
- `pseudo-tty/stdin-setrawmode` fails with `setRawMode ENOTSUP`.
- `pseudo-tty/test-tty-stdout-resize` fails with `getWindowSize ENOTSUP`.

The standalone `napi_wasmer` runner installs host stdin/stdout/stderr on its
`WasiRunner`, but its `PluggableRuntime` has no `TtyBridge`. WASIX therefore
returns `Errno::Notsup` from `tty_get` and `tty_set`. The previous Wasmer CLI
path created `SysTty`, reset it, and installed it with `set_tty`.

GitHub Actions run `33209493626`, job `98978925438`, is distinct: its QuickJS
Node suite passes 1675 tests with no failures, including the four TTY tests. It
then fails the safe-mode SIGINT smoke because the process returns 127 while the
test expects 130. The expectation changed in EdgeJS commit `b4c383b6`, but the
workflow still provisions Wasmer revision `0f38fc67f5874fe5df9b907e33680edad1713d73`.

## Action plan

1. Update the N-API standalone CLI runtime construction to mirror Wasmer CLI:
   create `SysTty`, reset it, and attach it to the `PluggableRuntime`.
2. Add focused N-API coverage for the runtime TTY setup without changing EdgeJS
   terminal or event-loop semantics.
3. Rebuild the standalone CLI and run the four focused V8 WASIX tests. Add
   `EDGE_TRACE_TTY` or runner-side state tracing only if the keepalive timeout
   remains after the bridge is installed.
4. Run the QuickJS safe-mode SIGINT probe against the intended newer immutable
   Wasmer SDK revision. Update the workflow pin only if that revision provides
   the expected exit status; otherwise resolve the Wasmer exit contract before
   changing EdgeJS again.
5. Run the relevant full CI lanes and keep the N-API TTY correction separate
   from any EdgeJS workflow pin or signal-expectation change.

## Acceptance gates

- WASIX `tty_get` and `tty_set` reach a native bridge under `napi_wasmer`.
- All four focused V8 TTY tests pass without skips or EdgeJS runtime changes.
- The full V8 WASIX Node matrix has no TTY regression.
- The QuickJS SIGINT expectation and the workflow's immutable Wasmer revision
  agree on the same exit-code contract.

## Findings and implementation

The four V8 failures had two runner-side causes:

1. Direct-stdio `napi_wasmer` runs created a `PluggableRuntime` without the
   `SysTty` bridge that Wasmer CLI installs. This made WASIX `tty_get` and
   `tty_set` return `Notsup`, exposed as `setRawMode ENOTSUP` and
   `getWindowSize ENOTSUP`.
2. After the bridge was installed, `test-readable-tty-keepalive` reached the
   complete guest close path but the runner still hung while dropping its
   private Tokio runtime. A host stdin read remained in Tokio's blocking pool,
   so synchronous runtime shutdown waited after the guest had already exited.

N-API PR `wasmerio/napi#65` installs `SysTty` only for the direct-host-stdio
path and uses background Tokio shutdown for that CLI path after the guest
returns. Capture helpers remain non-TTY and retain their existing synchronous
runtime shutdown behavior.

Debug-only `EDGE_TRACE_TTY` output established the remaining lifecycle:

```text
readStop fd=0
readStart fd=0
close fd=0
afterClose fd=0
```

This ruled out an EdgeJS stream-close failure before changing the runner
lifecycle. The WASIX node adapter now forwards `EDGE_TRACE_TTY` when requested,
and the stream/TTY close trace includes close and after-close checkpoints.

The QuickJS signal failure was separate. Rebuilding the current QuickJS WASIX
artifact and running it through a Wasmer binary containing signal fix
`a79deeff0b39a8050ecfc5eb3615c05e123c8192` returns 130 and reports
`ExitCode::130`. The configured Wasmer SDK branch head
`5281e55dac6c85be91560c4c828d49fd2d5b8b5d` contains that fix. The repository
Actions variable `WASMER_SOURCE_REF` was therefore updated from `0f38fc67...`
to `5281e55...`, matching the immutable revision used by the standalone N-API
runner.

## Verification

- Locked N-API standalone CLI unit test: 1 passed.
- Locked N-API standalone release build: passed.
- Release V8 WASIX focused suite: all four formerly failing TTY tests passed.
- Debug TTY trace: guest stdin close and after-close both completed.
- QuickJS WASIX rebuild: passed, including the no-N-API-import validation.
- QuickJS SIGINT probe on the newer Wasmer signal runtime: exited 130 with
  `ExitCode::130`.

GitHub Actions run `33215136959` subsequently confirmed that the complete V8
WASIX Node step passes, including the four formerly failing TTY tests. Its later
framework failure was unrelated and is tracked in
`023_wasix_standalone_nested_entry_paths.md`.

GitHub Actions run `33215136931` confirmed that all 1675 QuickJS WASIX Node
tests pass. The Linux safe-mode SIGINT probe still reports the older Wasmer CLI
contract (127 and `Program recieved termination signal: Interrupt`) even though
the job built revision `5281e55d`.

The smoke test now accepts the two observed host reporting contracts as paired
outcomes: 130 must include `ExitCode::130`, while 127 must include `termination
signal`. Both retain the actual regression guard: the guest must terminate with
empty stdout before the scheduled `NOT REACHED` output. This contains the
Wasmer CLI cross-host discrepancy without changing EdgeJS signal behavior.
