// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/rand_util.h"

#include <windows.h>

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <limits>

#include "base/check.h"

// Prototype for ProcessPrng.
// See: https://learn.microsoft.com/en-us/windows/win32/seccng/processprng
extern "C" {
BOOL WINAPI ProcessPrng(PBYTE pbData, SIZE_T cbData);
}

namespace base {

namespace internal {

// boringssl 已移出闭包 (patches/09): 两个桩保持签名 (rand_util.h 声明,
// fuchsia/posix 变体不复存在), Windows 一律走 ProcessPrng。
void ConfigureBoringSSLBackedRandBytesFieldTrial() {}

bool UseBoringSSLForRandBytes() {
  return false;
}

}  // namespace internal

namespace {

// Import bcryptprimitives!ProcessPrng rather than cryptbase!RtlGenRandom to
// avoid opening a handle to \\Device\KsecDD in the renderer.
decltype(&ProcessPrng) GetProcessPrng() {
  HMODULE hmod = LoadLibraryW(L"bcryptprimitives.dll");
  CHECK(hmod);
  decltype(&ProcessPrng) process_prng_fn =
      reinterpret_cast<decltype(&ProcessPrng)>(
          GetProcAddress(hmod, "ProcessPrng"));
  CHECK(process_prng_fn);
  return process_prng_fn;
}

void RandBytesInternal(span<uint8_t> output, bool avoid_allocation) {
  static decltype(&ProcessPrng) process_prng_fn = GetProcessPrng();
  BOOL success =
      process_prng_fn(static_cast<BYTE*>(output.data()), output.size());
  // ProcessPrng is documented to always return TRUE.
  CHECK(success);
}

}  // namespace

void RandBytes(span<uint8_t> output) {
  RandBytesInternal(output, /*avoid_allocation=*/false);
}

namespace internal {

double RandDoubleAvoidAllocation() {
  uint64_t number;
  RandBytesInternal(byte_span_from_ref(number),
                    /*avoid_allocation=*/true);
  // This transformation is explained in rand_util.cc.
  return (number >> 11) * 0x1.0p-53;
}

}  // namespace internal

}  // namespace base
