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
// file change shape (CJS wrapper text, parameter list, shebang handling, ...)
// or the container/payload encoding changes (v2: XXH3 instead of FNV-1a;
// v3: payloads lost the in-provider source-hash prefix; v4: engine-agnostic
// 48-byte header — engine-specific validation lives inside the QuickJS
// payload itself, mirroring V8's CachedData).
constexpr uint32_t kFormatVersion = 4;

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

// XXH3-64 over arbitrary bytes (container hashing).
uint64_t Hash64(const void* data, size_t size);

// The CLI calls this when --no-bytecode-cache is in effect or the mode should
// never touch sidecars (e.g. --check).
void SetEnabledFromCli(bool enabled);

// True when the provider supports caching, the CLI did not disable it, and
// EDGE_BYTECODE_CACHE is not set to a falsy value.
bool Enabled();

// True when EDGE_BYTECODE_CACHE_TRACE is set (shared by the builtins cache).
bool TraceEnabled();

std::string SidecarPathForSource(const std::string& source_path);

// A validated sidecar: the whole file is read once and the payload is a view
// into that buffer (no intermediate copies).
struct SidecarPayload {
  std::vector<uint8_t> file_bytes;
  size_t payload_offset = 0;
  size_t payload_size = 0;
  const uint8_t* data() const { return file_bytes.data() + payload_offset; }
};

// Reads <source_path><suffix> and validates it against the exact source text
// about to be compiled and the expected compile shape. False (and empty
// payload) on any mismatch; never throws.
bool ReadSidecar(const std::string& source_path,
                 std::string_view source_utf8,
                 uint32_t expected_flags,
                 SidecarPayload* out);

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
                                   const uint8_t* payload,
                                   size_t payload_size);
bool DecodeSidecar(const uint8_t* data,
                   size_t size,
                   std::string_view engine_tag,
                   std::string_view source_utf8,
                   uint32_t expected_flags,
                   size_t* payload_offset_out,
                   size_t* payload_size_out);

// vm cachedData wrapper. Engine payloads self-validate integrity (V8
// CachedData; QuickJS via the provider's QJSB header) but QuickJS cannot
// detect bytecode compiled from different SOURCE. User-facing vm buffers
// (vm.Script/compileFunction/SourceTextModule cachedData) on QuickJS
// therefore carry an Edge-owned 12-byte prefix [QJSC + XXH3(source)] that is
// validated and stripped before the bytes reach the engine. On V8 both
// helpers are pass-throughs (offset 0), keeping byte-parity with Node's
// CachedData. Sidecar/builtins payloads carry no Edge prefix — their
// containers already record the source hash.
bool VmCachedDataNeedsWrapper();

// Prefix + payload as a fresh buffer (pass-through copy on V8).
std::vector<uint8_t> WrapVmCachedData(uint64_t source_hash,
                                      const uint8_t* payload,
                                      size_t payload_size);

// Validates the prefix against the expected source hash and returns the raw
// payload span. False means wrong/missing prefix or wrong source — callers
// report cachedData as rejected without touching the engine. On V8 always
// true with the identity span.
bool UnwrapVmCachedData(uint64_t expected_source_hash,
                        const uint8_t* data,
                        size_t size,
                        size_t* payload_offset_out,
                        size_t* payload_size_out);

}  // namespace edge_bytecode_cache

#endif  // EDGE_BYTECODE_CACHE_H_
