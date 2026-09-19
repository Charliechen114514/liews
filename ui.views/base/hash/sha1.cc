// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/hash/sha1.h"

#include <windows.h>

#include <bcrypt.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "base/check.h"
#include "base/containers/span.h"

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

SHA1Digest SHA1Hash(span<const uint8_t> data) {
  SHA1Digest digest;
  CHECK(CngDigest(BCRYPT_SHA1_ALGORITHM, data.data(), data.size(),
                  digest.data(), static_cast<ULONG>(digest.size())));
  return digest;
}

std::string SHA1HashString(std::string_view str) {
  std::string digest(kSHA1Length, '\0');
  CHECK(CngDigest(BCRYPT_SHA1_ALGORITHM,
                  reinterpret_cast<const uint8_t*>(str.data()), str.size(),
                  reinterpret_cast<uint8_t*>(digest.data()), kSHA1Length));
  return digest;
}

}  // namespace base
