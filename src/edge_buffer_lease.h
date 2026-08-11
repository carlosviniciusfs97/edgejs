#ifndef EDGE_BUFFER_LEASE_H_
#define EDGE_BUFFER_LEASE_H_

#include <cstddef>
#include <cstdint>
#include <limits>

#include "node_api.h"
#include "unofficial_napi.h"

inline size_t EdgeTypedArrayElementSize(napi_typedarray_type type) {
  switch (type) {
  case napi_int16_array:
  case napi_uint16_array:
  case napi_float16_array:
    return 2;
  case napi_int32_array:
  case napi_uint32_array:
  case napi_float32_array:
    return 4;
  case napi_float64_array:
  case napi_bigint64_array:
  case napi_biguint64_array:
    return 8;
  default:
    return 1;
  }
}

inline bool EdgeGetBinaryByteLength(napi_env env, napi_value value,
                                    size_t *length_out) {
  if (env == nullptr || value == nullptr || length_out == nullptr)
    return false;
  *length_out = 0;

  bool matches = false;
  if (napi_is_buffer(env, value, &matches) == napi_ok && matches) {
    return napi_get_buffer_info(env, value, nullptr, length_out) == napi_ok;
  }
  if (napi_is_typedarray(env, value, &matches) == napi_ok && matches) {
    napi_typedarray_type type = napi_uint8_array;
    size_t element_length = 0;
    if (napi_get_typedarray_info(env, value, &type, &element_length, nullptr,
                                 nullptr, nullptr) != napi_ok) {
      return false;
    }
    const size_t element_size = EdgeTypedArrayElementSize(type);
    if (element_length > std::numeric_limits<size_t>::max() / element_size)
      return false;
    *length_out = element_length * element_size;
    return true;
  }
  if (napi_is_dataview(env, value, &matches) == napi_ok && matches) {
    return napi_get_dataview_info(env, value, length_out, nullptr, nullptr,
                                  nullptr) == napi_ok;
  }
  if (napi_is_arraybuffer(env, value, &matches) == napi_ok && matches) {
    return napi_get_arraybuffer_info(env, value, nullptr, length_out) ==
           napi_ok;
  }

  // Providers may expose SharedArrayBuffer through the ArrayBuffer-info
  // operation even though standard N-API has no separate type predicate.
  return napi_get_arraybuffer_info(env, value, nullptr, length_out) == napi_ok;
}

class EdgeBufferLease {
public:
  EdgeBufferLease() = default;
  EdgeBufferLease(const EdgeBufferLease &) = delete;
  EdgeBufferLease &operator=(const EdgeBufferLease &) = delete;

  ~EdgeBufferLease() { (void)Release(false); }

  bool Acquire(napi_env env, napi_value value,
               unofficial_napi_buffer_access_mode mode) {
    size_t byte_length = 0;
    return EdgeGetBinaryByteLength(env, value, &byte_length) &&
           Acquire(env, value, 0, byte_length, mode);
  }

  bool Acquire(napi_env env, napi_value value, size_t byte_offset,
               size_t byte_length, unofficial_napi_buffer_access_mode mode) {
    if (lease_ != nullptr)
      return false;
    void *data = nullptr;
    unofficial_napi_buffer_lease lease = nullptr;
    if (unofficial_napi_acquire_buffer_lease(env, value, byte_offset,
                                             byte_length, mode, &lease,
                                             &data) != napi_ok) {
      return false;
    }
    env_ = env;
    lease_ = lease;
    data_ =
        data != nullptr ? static_cast<uint8_t *>(data) : ZeroLengthSentinel();
    size_ = byte_length;
    return true;
  }

  bool Release(bool modified) {
    if (lease_ == nullptr)
      return true;
    unofficial_napi_buffer_lease lease = lease_;
    lease_ = nullptr;
    data_ = nullptr;
    size_ = 0;
    return unofficial_napi_release_buffer_lease(env_, lease, modified) ==
           napi_ok;
  }

  uint8_t *data() { return data_; }
  const uint8_t *data() const { return data_; }
  size_t size() const { return size_; }

private:
  static uint8_t *ZeroLengthSentinel() {
    static uint8_t sentinel = 0;
    return &sentinel;
  }

  napi_env env_ = nullptr;
  unofficial_napi_buffer_lease lease_ = nullptr;
  uint8_t *data_ = nullptr;
  size_t size_ = 0;
};

#endif // EDGE_BUFFER_LEASE_H_
