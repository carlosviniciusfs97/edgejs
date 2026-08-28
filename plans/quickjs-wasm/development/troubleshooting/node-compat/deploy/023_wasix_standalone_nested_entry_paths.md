# WASIX standalone nested entry paths

| | | Remarks |
| --- | --- | --- |
| **Status** | 🟢 | The standalone N-API runner now preserves paths relative to an existing guest mount. |
| **Severity** | High | Astro SSR and Etherpad fail to start in the V8 WASIX framework suite. |

## Baseline

GitHub Actions run `33215136959`, job `98996961409`, passes the complete V8
WASIX Node test step, including the four TTY cases tracked in
`022_wasix_ci_tty_bridge_and_signal_pin.md`. It then fails the framework step:

- `js-astro-ssr-standalone` asks EdgeJS to run `dist/server/entry.mjs`, but the
  guest receives `/app/entry.mjs` and cannot find the module.
- `js-etherpad` asks EdgeJS to run `src/node/server.js`, but the guest receives
  `/app/server.js` and cannot find the module.

The framework runner already mounts the project directory at `/app` and sets
the guest working directory to `/app`. The standalone N-API CLI canonicalizes
the first guest argument on the host, notices that an `/app` mount already
exists, but still replaces the argument with `/app/<basename>`. That discards
every directory component below the mounted project root.

## Action plan

1. Keep the first-argument host-file detection used by the standalone CLI.
2. When the host file is covered by an explicit mount, map its path relative to
   that mount's guest root instead of reducing it to a basename.
3. Preserve the existing fallback for callers without a covering mount: mount
   the script's parent at `/app` and pass `/app/<basename>`.
4. Add focused N-API unit coverage for nested paths and the fallback mapping.
5. Rebuild the locked release CLI and rerun the affected Astro SSR and Etherpad
   V8 WASIX cases before updating the EdgeJS submodule pointer.

## Acceptance gates

- `dist/server/entry.mjs` under a project mounted at `/app` reaches the guest as
  `/app/dist/server/entry.mjs`.
- `src/node/server.js` reaches the guest as `/app/src/node/server.js`.
- Single-file invocations without an explicit covering mount retain their
  automatic `/app` mount behavior.
- No EdgeJS runtime or framework adapter semantics change.

## Implementation

N-API PR `wasmerio/napi#66` now checks whether the canonical host entry file is
covered by an existing mount. When it is, the CLI translates the file relative
to that mount's guest path. For example:

```text
<project>/dist/server/entry.mjs + <project>:/app
  -> /app/dist/server/entry.mjs
```

If multiple mounts cover the entry, the most specific host mount wins. The
pre-existing automatic mount behavior is unchanged when no mount covers the
file. EdgeJS only advances the N-API submodule pointer.

## Verification

- Locked standalone N-API CLI unit tests: 4 passed.
- Locked standalone N-API CLI release build: passed.
- Focused `js-astro-ssr-standalone` matrix: Node and EdgeJS V8 WASIX passed;
  the SSR home route returned HTTP 200 through `dist/server/entry.mjs`.
- Focused `js-etherpad` matrix: Node and EdgeJS V8 WASIX passed; `/health` and
  `/` returned HTTP 200 through `src/node/server.js`.
