#ifndef EDGE_BYTECODE_CACHE_H_
#define EDGE_BYTECODE_CACHE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Sidecar bytecode caches: engine-serialized compiled code stored next to the
// source file ("app.js" -> "app.js.v8b" / "app.js.qjsb"). The payload is
// opaque engine data (V8 code cache / QuickJS JS_WriteObject bytecode); this
// module owns the container format and its validation only.
namespace edge_bytecode_cache {

// Bump whenever the bytes handed to the engine compiler for a given source
// file change shape (CJS wrapper text, parameter list, shebang handling, ...).
constexpr uint32_t kFormatVersion = 1;

// Compile shape of the payload; a sidecar is only consumed by the exact shape
// that produced it.
// bit 0: CJS function-compile with params (exports, require, module,
//        __filename, __dirname).
constexpr uint32_t kFlagCjsFunctionV1 = 1u << 0;
// bit 1: ES module compile (ModuleWrap / module shape).
constexpr uint32_t kFlagEsmModuleV1 = 1u << 1;

// Suffix appended to the full source filename. Empty when the active NAPI
// provider has no bytecode-cache support.
const char* SidecarSuffix();

// Engine identity baked into sidecar headers, e.g. "v8-11.9.169.7-node.0" or
// "qjs-ng-0.14.0". Empty disables the cache entirely.
const std::string& EngineCacheTag();

uint64_t Fnv1a64(const void* data, size_t size);

// The CLI calls this when --no-bytecode-cache is in effect or the mode should
// never touch sidecars (e.g. --check).
void SetEnabledFromCli(bool enabled);

// True when the provider supports caching, the CLI did not disable it, and
// EDGE_BYTECODE_CACHE is not set to a falsy value.
bool Enabled();

std::string SidecarPathForSource(const std::string& source_path);

// Reads <source_path><suffix> and validates it against the exact source text
// about to be compiled and the expected compile shape. False (and empty
// payload) on any mismatch; never throws.
bool ReadSidecar(const std::string& source_path,
                 std::string_view source_utf8,
                 uint32_t expected_flags,
                 std::vector<uint8_t>* payload_out);

// Atomically writes the sidecar next to the source (temp file + rename).
// Failures (read-only filesystem, permissions, ...) are silent: returns
// false, never throws.
bool WriteSidecar(const std::string& source_path,
                  std::string_view source_utf8,
                  uint32_t flags,
                  const uint8_t* payload,
                  size_t payload_size);

bool RemoveSidecar(const std::string& source_path);

// Serialization helpers exposed for tests.
std::vector<uint8_t> EncodeSidecar(std::string_view engine_tag,
                                   std::string_view source_utf8,
                                   uint32_t flags,
                                   uint64_t filename_hash,
                                   const uint8_t* payload,
                                   size_t payload_size);
bool DecodeSidecar(const uint8_t* data,
                   size_t size,
                   std::string_view engine_tag,
                   std::string_view source_utf8,
                   uint32_t expected_flags,
                   uint64_t expected_filename_hash,
                   std::vector<uint8_t>* payload_out);

}  // namespace edge_bytecode_cache

#endif  // EDGE_BYTECODE_CACHE_H_
