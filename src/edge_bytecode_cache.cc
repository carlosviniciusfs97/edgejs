#include "edge_bytecode_cache.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace edge_bytecode_cache {
namespace {

constexpr char kMagic[8] = {'E', 'D', 'G', 'E', 'J', 'S', 'B', 'C'};
constexpr size_t kHeaderSize = 64;

std::atomic<bool> g_cli_enabled{true};

bool IsFalsyEnvValue(const char* value) {
  if (value == nullptr || value[0] == '\0') return false;
  std::string normalized(value);
  for (char& ch : normalized) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return normalized == "0" || normalized == "false" || normalized == "no" ||
         normalized == "off";
}

bool TraceEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("EDGE_BYTECODE_CACHE_TRACE");
    return value != nullptr && value[0] != '\0' && !IsFalsyEnvValue(value);
  }();
  return enabled;
}

void Trace(const char* event, const std::string& path, const char* detail = nullptr) {
  if (!TraceEnabled()) return;
  if (detail != nullptr) {
    std::fprintf(stderr, "[edge-bytecode-cache] %s %s (%s)\n", event, path.c_str(), detail);
  } else {
    std::fprintf(stderr, "[edge-bytecode-cache] %s %s\n", event, path.c_str());
  }
}

void WriteU32(std::vector<uint8_t>* out, size_t offset, uint32_t value) {
  (*out)[offset + 0] = static_cast<uint8_t>(value);
  (*out)[offset + 1] = static_cast<uint8_t>(value >> 8);
  (*out)[offset + 2] = static_cast<uint8_t>(value >> 16);
  (*out)[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void WriteU64(std::vector<uint8_t>* out, size_t offset, uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    (*out)[offset + i] = static_cast<uint8_t>(value >> (8 * i));
  }
}

uint32_t ReadU32(const uint8_t* data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) |
         (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) |
         (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint64_t ReadU64(const uint8_t* data, size_t offset) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<uint64_t>(data[offset + i]) << (8 * i);
  }
  return value;
}

// QuickJS bytecode embeds the compile-time filename in its debug info, so a
// sidecar from another location would surface stale paths in stack traces;
// keying on the filename forces a recompile instead. V8 code caches carry no
// filename (it only enters ScriptOrigin) and stay relocatable.
uint64_t FilenameHashForSource(const std::string& source_path) {
#if defined(EDGE_NAPI_QUICKJS)
  return Fnv1a64(source_path.data(), source_path.size());
#else
  (void)source_path;
  return 0;
#endif
}

}  // namespace

const char* SidecarSuffix() {
#if defined(EDGE_BUNDLED_NAPI_V8)
  return ".v8b";
#elif defined(EDGE_NAPI_QUICKJS)
  return ".qjsb";
#else
  return "";
#endif
}

const std::string& EngineCacheTag() {
  static const std::string tag = [] {
#if defined(EDGE_BUNDLED_NAPI_V8) && defined(EDGE_EMBEDDED_V8_VERSION)
    return std::string("v8-") + EDGE_EMBEDDED_V8_VERSION;
#elif defined(EDGE_NAPI_QUICKJS) && defined(EDGE_EMBEDDED_QUICKJS_VERSION)
    return std::string("qjs-ng-") + EDGE_EMBEDDED_QUICKJS_VERSION;
#else
    return std::string();
#endif
  }();
  return tag;
}

uint64_t Fnv1a64(const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint64_t hash = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

void SetEnabledFromCli(bool enabled) {
  g_cli_enabled.store(enabled, std::memory_order_relaxed);
}

bool Enabled() {
  static const bool env_enabled =
      !IsFalsyEnvValue(std::getenv("EDGE_BYTECODE_CACHE"));
  return env_enabled && !EngineCacheTag().empty() &&
         g_cli_enabled.load(std::memory_order_relaxed);
}

std::string SidecarPathForSource(const std::string& source_path) {
  return source_path + SidecarSuffix();
}

std::vector<uint8_t> EncodeSidecar(std::string_view engine_tag,
                                   std::string_view source_utf8,
                                   uint64_t filename_hash,
                                   const uint8_t* payload,
                                   size_t payload_size) {
  std::vector<uint8_t> out(kHeaderSize + engine_tag.size() + payload_size);
  std::memcpy(out.data(), kMagic, sizeof(kMagic));
  WriteU32(&out, 8, kFormatVersion);
  WriteU32(&out, 12, kFlagCjsFunctionV1);
  WriteU32(&out, 16, static_cast<uint32_t>(engine_tag.size()));
  WriteU64(&out, 20, source_utf8.size());
  WriteU64(&out, 28, Fnv1a64(source_utf8.data(), source_utf8.size()));
  WriteU64(&out, 36, filename_hash);
  WriteU64(&out, 44, payload_size);
  WriteU64(&out, 52, Fnv1a64(payload, payload_size));
  WriteU32(&out, 60, 0);
  std::memcpy(out.data() + kHeaderSize, engine_tag.data(), engine_tag.size());
  if (payload_size > 0) {
    std::memcpy(out.data() + kHeaderSize + engine_tag.size(), payload, payload_size);
  }
  return out;
}

bool DecodeSidecar(const uint8_t* data,
                   size_t size,
                   std::string_view engine_tag,
                   std::string_view source_utf8,
                   uint64_t expected_filename_hash,
                   std::vector<uint8_t>* payload_out) {
  if (payload_out == nullptr) return false;
  payload_out->clear();
  if (data == nullptr || size < kHeaderSize) return false;
  if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) return false;
  if (ReadU32(data, 8) != kFormatVersion) return false;
  if (ReadU32(data, 12) != kFlagCjsFunctionV1) return false;

  const uint64_t tag_len = ReadU32(data, 16);
  const uint64_t source_len = ReadU64(data, 20);
  const uint64_t source_hash = ReadU64(data, 28);
  const uint64_t filename_hash = ReadU64(data, 36);
  const uint64_t payload_len = ReadU64(data, 44);
  const uint64_t payload_hash = ReadU64(data, 52);

  if (size != kHeaderSize + tag_len + payload_len) return false;
  if (tag_len != engine_tag.size() ||
      std::memcmp(data + kHeaderSize, engine_tag.data(), engine_tag.size()) != 0) {
    return false;
  }
  if (source_len != source_utf8.size() ||
      source_hash != Fnv1a64(source_utf8.data(), source_utf8.size())) {
    return false;
  }
  if (filename_hash != 0 && filename_hash != expected_filename_hash) return false;

  const uint8_t* payload = data + kHeaderSize + tag_len;
  if (payload_hash != Fnv1a64(payload, payload_len)) return false;

  payload_out->assign(payload, payload + payload_len);
  return true;
}

bool ReadSidecar(const std::string& source_path,
                 std::string_view source_utf8,
                 std::vector<uint8_t>* payload_out) {
  if (payload_out == nullptr) return false;
  payload_out->clear();
  if (!Enabled()) return false;

  const std::string sidecar_path = SidecarPathForSource(source_path);
  std::ifstream in(sidecar_path, std::ios::binary);
  if (!in.is_open()) return false;

  std::vector<uint8_t> contents((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
  if (in.bad()) {
    Trace("miss", sidecar_path, "read-error");
    return false;
  }
  if (!DecodeSidecar(contents.data(), contents.size(), EngineCacheTag(),
                     source_utf8, FilenameHashForSource(source_path),
                     payload_out)) {
    Trace("miss", sidecar_path, "invalid-or-stale");
    return false;
  }
  Trace("hit", sidecar_path);
  return true;
}

bool WriteSidecar(const std::string& source_path,
                  std::string_view source_utf8,
                  const uint8_t* payload,
                  size_t payload_size) {
  if (!Enabled() || payload == nullptr || payload_size == 0) return false;

  const std::string sidecar_path = SidecarPathForSource(source_path);
  const std::vector<uint8_t> contents =
      EncodeSidecar(EngineCacheTag(), source_utf8,
                    FilenameHashForSource(source_path), payload, payload_size);

#if defined(_WIN32)
  const int pid = _getpid();
#else
  const int pid = static_cast<int>(getpid());
#endif
  const std::string tmp_path = sidecar_path + "." + std::to_string(pid) + ".tmp";

  std::error_code ec;
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      Trace("write-failed", sidecar_path, "open");
      return false;
    }
    out.write(reinterpret_cast<const char*>(contents.data()),
              static_cast<std::streamsize>(contents.size()));
    out.flush();
    if (!out.good()) {
      out.close();
      std::filesystem::remove(tmp_path, ec);
      Trace("write-failed", sidecar_path, "write");
      return false;
    }
  }

  std::filesystem::rename(tmp_path, sidecar_path, ec);
  if (ec) {
    // Windows rename does not replace an existing destination.
    std::filesystem::remove(sidecar_path, ec);
    std::filesystem::rename(tmp_path, sidecar_path, ec);
    if (ec) {
      std::filesystem::remove(tmp_path, ec);
      Trace("write-failed", sidecar_path, "rename");
      return false;
    }
  }
  Trace("write", sidecar_path);
  return true;
}

bool RemoveSidecar(const std::string& source_path) {
  const std::string sidecar_path = SidecarPathForSource(source_path);
  std::error_code ec;
  const bool removed = std::filesystem::remove(sidecar_path, ec);
  if (removed) Trace("remove", sidecar_path);
  return removed && !ec;
}

}  // namespace edge_bytecode_cache
