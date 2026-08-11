# EdgeJS Runtime Node Compatibility

| | | Remarks |
| --- | --- | --- |
| **Status** | ▶️ | Registry for Node compatibility issues owned by EdgeJS runtime/bootstrap code. |
| **Severity** | Low | Documentation registry only; individual issue pages carry runtime severity. |

These notes track Node compatibility work that belongs in EdgeJS runtime source
or JavaScript bootstrap code. They are separate from QuickJS N-API internals:
when a behavior is really part of the Node runtime surface, it should be solved
in EdgeJS itself rather than hidden behind removed N-API compatibility files.

- [Minimal Intl fallback](003_minimal_intl_fallback.md)
- [Native inspector stub](004_native_inspector_stub.md)
- [Stream wrapper unwrap fallback](010_stream_wrapper_unwrap_fallback.md)
- [Native buffer lease regressions](013_native_buffer_lease_regressions.md)
