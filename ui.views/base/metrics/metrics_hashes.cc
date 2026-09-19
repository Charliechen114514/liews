// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/metrics/metrics_hashes.h"

#include <windows.h>

#include <bcrypt.h>

#include <string.h>

#include <array>
#include <string_view>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/numerics/byte_conversions.h"

#pragma comment(lib, "bcrypt.lib")

namespace base {

namespace {

// boringssl 已移出闭包 (patches/09): Windows CNG 摘要。
bool CngDigest(LPCWSTR algorithm,
               const uint8_t* data,
               size_t size,
               uint8_t* out,
               ULONG out_len) {
  BCRYPT_ALG_HANDLE provider;
  if (!BCRYPT_SUCCESS(
          BCryptOpenAlgorithmProvider(&provider, algorithm, nullptr, 0))) {
    return false;
  }
  NTSTATUS status = BCryptHash(provider, nullptr, 0,
                               const_cast<PUCHAR>(data),
                               static_cast<ULONG>(size), out, out_len);
  BCryptCloseAlgorithmProvider(provider, 0);
  return BCRYPT_SUCCESS(status);
}

}  // namespace

uint64_t HashMetricName(std::string_view name) {
  // Corresponding Python code for quick look up:
  //
  //   import struct
  //   import hashlib
  //   struct.unpack('>Q', hashlib.md5(name.encode('utf-8')).digest()[:8])[0]
  //
  std::array<uint8_t, 16> hash;
  CHECK(CngDigest(BCRYPT_MD5_ALGORITHM,
                  reinterpret_cast<const uint8_t*>(name.data()), name.size(),
                  hash.data(), static_cast<ULONG>(hash.size())));
  return U64FromBigEndian(base::span(hash).first<8>());
}

uint32_t HashMetricNameAs32Bits(std::string_view name) {
  std::array<uint8_t, 16> hash;
  CHECK(CngDigest(BCRYPT_MD5_ALGORITHM,
                  reinterpret_cast<const uint8_t*>(name.data()), name.size(),
                  hash.data(), static_cast<ULONG>(hash.size())));
  return U32FromBigEndian(base::span(hash).first<4>());
}

uint32_t ParseMetricHashTo32Bits(uint64_t hash) {
  return (hash >> 32);
}

uint32_t HashFieldTrialName(std::string_view name) {
  // SHA-1 is designed to produce a uniformly random spread in its output space,
  // even for nearly-identical inputs.
  std::array<uint8_t, 20> hash;
  CHECK(CngDigest(BCRYPT_SHA1_ALGORITHM,
                  reinterpret_cast<const uint8_t*>(name.data()), name.size(),
                  hash.data(), static_cast<ULONG>(hash.size())));
  return U32FromLittleEndian(base::span(hash).first<4>());
}

}  // namespace base
