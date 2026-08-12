# Node Test: QuickJS Compatibility Failures

| | | Remarks |
| --- | --- | --- |
| **Status** | ▶️ | Registry for native QuickJS Node compatibility test issue pages. |
| **Severity** | High | Node compatibility failures remain significant until the linked issue pages close. |

The issue pages are canonical. Keep failure details, diagnosis, and known
limitations on the individual page for each problem.

## Source

```text
/Users/sadhbh/src/dev/edgejs/test-only.log
```

## Issues

| Status | Severity | Issue | Topic |
| --- | --- | --- | --- |
| ▶️ | Medium | [001_buffer_limits_and_deprecations.md](001_buffer_limits_and_deprecations.md) | Buffer limits and deprecation parity |
| ▶️ | Medium | [002_console_inspect_and_stack_formatting.md](002_console_inspect_and_stack_formatting.md) | Console inspect and stack formatting |
| ▶️ | High | [003_node_test_public_api_exports.md](003_node_test_public_api_exports.md) | `node:test` public API exports |
| ▶️ | Medium | [004_diagnostics_channel_module_loader.md](004_diagnostics_channel_module_loader.md) | Diagnostics channel module loader events |
| ▶️ | Medium | [005_diagnostics_channel_async_context.md](005_diagnostics_channel_async_context.md) | Diagnostics channel async context |
| 🟢 | Low | [006_eventemitter_asyncresource_private_fields.md](006_eventemitter_asyncresource_private_fields.md) | EventEmitterAsyncResource private-field errors |
| ▶️ | High | [007_fetch_response_body_and_proxy_env.md](007_fetch_response_body_and_proxy_env.md) | Fetch Response body and HTTP proxy env |
| 🟠 | High | [008_https_proxy_tunnel_errors.md](008_https_proxy_tunnel_errors.md) | HTTPS proxy tunnel errors |
| 🟢 | Medium | [009_http_timers_and_header_limits.md](009_http_timers_and_header_limits.md) | HTTP timers and header limits |
| ▶️ | Low | [010_os_constants_and_userinfo.md](010_os_constants_and_userinfo.md) | OS constants and userInfo errors |
| ▶️ | Medium | [011_fastutf8stream_sync_wait.md](011_fastutf8stream_sync_wait.md) | FastUtf8Stream synchronous wait |
| ▶️ | High | [012_explicit_resource_management_syntax.md](012_explicit_resource_management_syntax.md) | Explicit resource management syntax |
| ▶️ | Medium | [013_stream_missing_builtins_and_async_iterators.md](013_stream_missing_builtins_and_async_iterators.md) | Stream missing builtins and async iterators |
| 🟢 | Medium | [014_string_decoder_utf8_boundaries.md](014_string_decoder_utf8_boundaries.md) | StringDecoder UTF-8 boundaries |
| ▶️ | Medium | [015_url_and_data_url_validation.md](015_url_and_data_url_validation.md) | URL and data URL validation |
| 🟢 | Medium | [016_whatwg_url_inspect_and_searchparams.md](016_whatwg_url_inspect_and_searchparams.md) | WHATWG URL inspect and search params |
| ▶️ | High | [017_http2_native_lifecycle_crashes.md](017_http2_native_lifecycle_crashes.md) | HTTP/2 native lifecycle crashes |
| 🟢 | High | [018_tls_securecontext_sni_lifetime.md](018_tls_securecontext_sni_lifetime.md) | TLS SecureContext, SNI, and setKeyCert lifetime |
